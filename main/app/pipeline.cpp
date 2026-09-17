// =============================================================================
// PIPELINE
// =============================================================================
// State machine: IDLE -> RECORDING -> TRANSCRIBING -> CLEANING -> SAVING.
// A failed transcribe or save keeps the clip and text so Down retries the
// stage; a failed cleanup falls back to the raw transcript.
#include "app/pipeline.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "app/input.h"
#include "app/power.h"
#include "app/settings.h"
#include "audio/clip.h"
#include "audio/pdm_mic.h"
#include "board/battery.h"
#include "board/board.h"
#include "board/buzzer.h"
#include "board/touch.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http.h"
#include "net/llm_client.h"
#include "net/obsidian_client.h"
#include "net/stt_client.h"
#include "net/wifi.h"
#include "nvs.h"
#include "portal/portal.h"
#include "ui/display.h"
#include "ui/icons.h"
#include "ui/screen.h"

namespace pipeline {
namespace {

constexpr const char *kTag = "pipeline";
constexpr uint32_t kMinRecordingMs = 300;
constexpr uint32_t kWifiWaitMs = 20000;

// Released before this, the press was a tap and the recording latches; held
// past it, the release stops the recording the way it always has. Timed from
// the event, not from the record loop, which starts a good quarter second
// later behind the microphone settling and the start cue.
constexpr int64_t kLatchUs = 500 * 1000;

// A second tap this soon after the first is taking the recording back rather
// than ending it. Explicit, because leaning on kMinRecordingMs would make a
// 200 to 400 ms double tap a coin flip between cancelling and saving.
constexpr int64_t kCancelUs = 1200 * 1000;

// Nothing physical ends a latched recording, so a pocket press would record
// and upload until the battery died.
constexpr int64_t kLatchCeilingUs = 600LL * 1000 * 1000;


// Where a segment is allowed to end. Two thresholds rather than one, so a
// single quiet block inside a sentence cannot open a silent run.
//
// Multiples of the room's own noise floor, which clip.cpp measures afresh for
// every recording, rather than levels. A kitchen and a quiet study differ by
// more than speech differs from silence in either of them, so a threshold
// fitted to one drops whole notes in the other: this microphone reads a
// whisper at 139 and ordinary speech at 466, both nothing against full scale.
constexpr uint32_t kSilenceMultiple = 4;   // Under this much of the floor: a pause
constexpr uint32_t kSpeechMultiple = 8;    // Over this much: unmistakably talking

// The lowest bar of the three, and the one with the least room in it. In the
// recording these were fitted to, the loudest block of a silent stretch
// reached 102 and the loudest block of a whisper only 122, so the two nearly
// touch. It sits between them because skipping wrongly loses what somebody
// said, while sending wrongly costs one request and a chance of the model
// inventing a line over silence.
constexpr uint32_t kSendMultiple = 3;

// Longer than one 512-sample read, which is 32 ms.
constexpr uint32_t kCueGuardMs = 40;

constexpr uint32_t kHangoverBlocks = 20;   // 640 ms, the usual end-of-utterance

constexpr uint32_t kBlocksPerSecond = clip::kSampleRateHz / clip::kBlockSamples;
// Short, because a segment is how often the screen can show new words and a
// twenty second wait does not read as live. Groq bills a minimum of ten
// seconds per request, so a cut below that pays for silence, but at roughly
// four cents an hour the waste is worth less than the feedback.
constexpr uint32_t kMinSegmentBlocks = 8 * kBlocksPerSecond;
constexpr uint32_t kMaxSegmentBlocks = 12 * kBlocksPerSecond;
constexpr uint32_t kQuietSearchBlocks = 2 * kBlocksPerSecond;

constexpr int kSegmentTries = 3;
constexpr uint32_t kSegmentRetryMs = 1000;

// One stretch of the ring waiting to become text, in absolute sample offsets.
struct Segment {
    uint32_t start;
    uint32_t end;
    uint16_t peak;     // Loudest block in the range, for the log
    bool worth_sending;  // Cleared the floor, so something was said
};

// Walks the block energy as it arrives and decides where segments end. Lives
// across the whole recording on the pipeline task, reset for each one.
struct Detector {
    uint32_t next_block = 0;     // First block not yet looked at
    uint32_t start_block = 0;    // First block of the segment being built
    uint32_t silence_block = 0;  // Where the current silent run began
    bool in_silence = true;
    uint16_t peak = 0;
};

Detector s_detector;
// Pending segments, oldest first. Shared: the detector pushes from the
// pipeline task while the uploader task takes from the front.
std::vector<Segment> s_segments;
std::vector<std::string> s_texts;
std::mutex s_segments_mutex;
std::string s_upload_error;
int s_transcribe_attempts = 0;
// Bumped by the uploader whenever a segment becomes text, so the record loop
// can notice new words without taking the mutex on every 50 ms pass.
std::atomic<uint16_t> s_texts_ready{0};
// What the body is showing outside a recording, so a cancelled one can put
// the previous note back rather than leaving half a transcript on screen.
std::string s_shown_note;

SemaphoreHandle_t s_upload_go = nullptr;
std::atomic<bool> s_upload_stop{true};
std::atomic<bool> s_uploader_idle{true};

constexpr const char *kSetupSsid = "Sticky-Setup";
constexpr const char *kNvsNamespace = "sticky";
constexpr const char *kNvsNoteKey = "last_note";
constexpr size_t kNvsNoteBytes = 3900;  // nvs_set_str caps a string near 4 KB
constexpr uint32_t kBatteryPollSeconds = 60;

enum class Stage { None, Transcribe, Save };

Stage s_retry_stage = Stage::None;
std::string s_pending_text;
// Anything the recording itself needs to tell the user, carried to the
// caption of the saved note because process() is what finally draws one.
std::string s_record_note;
std::atomic<bool> s_capture_stop{false};
std::atomic<bool> s_capture_done{true};
std::atomic<bool> s_capture_gate{false};
bool s_setup_mode = false;
bool s_radio_off = false;
bool s_info_showing = false;

// Buckets the charge the way the gauge icon does, so the panel repaints on a
// change the user can see rather than on every percent. A full refresh costs
// about a second, so this bucket is what bounds how often an idle device
// paints at all.
int battery_step()
{
    return icons::battery_step(battery::percent());
}


// Formats the wall clock as HH:MM, or a placeholder before SNTP.
std::string clock_text()
{
    if (!wifi::time_synced()) {
        return "--:--";
    }
    const time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    char text[8];
    std::strftime(text, sizeof(text), "%H:%M", &local);
    return text;
}

// Loads the last saved note from NVS so it survives reboots.
std::string load_last_note()
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return "";
    }
    size_t length = 0;
    std::string note;
    if (nvs_get_str(handle, kNvsNoteKey, nullptr, &length) == ESP_OK && length > 1) {
        note.resize(length);
        nvs_get_str(handle, kNvsNoteKey, note.data(), &length);
        note.resize(length - 1);
    }
    nvs_close(handle);
    return note;
}

// Stores the last saved note; NVS strings cap near 4 KB so long notes are cut.
void store_last_note(const std::string &note)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    // Back off the cut to a code point boundary, or the reloaded note ends in
    // a stray U+FFFD from next_code_point() in ui/text.cpp. Three bytes per
    // CJK glyph means a note in Chinese reaches this long before one in
    // English does.
    size_t cut = note.size() < kNvsNoteBytes ? note.size() : kNvsNoteBytes;
    while (cut > 0 && (static_cast<unsigned char>(note[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    nvs_set_str(handle, kNvsNoteKey, note.substr(0, cut).c_str());
    nvs_commit(handle);
    nvs_close(handle);
}

// Capture task: copies mic samples into the clip until told to stop.
void capture_task(void *)
{
    int16_t buffer[512];
    while (!s_capture_stop.load()) {
        size_t got = 0;
        if (pdm_mic::read(buffer, 512, got, 100) != ESP_OK) {
            break;
        }
        // Read and drop until the start cue has finished sounding, because
        // the microphone hears the buzzer beside it at a level no voice
        // reaches. Draining the channel rather than opening it late is what
        // keeps the audio current: a reader that waited would find the DMA
        // ring holding its 192 ms of beep the moment it started.
        if (!s_capture_gate.load()) {
            continue;
        }
        if (got > 0 && !clip::append(buffer, got)) {
            break;
        }
    }
    s_capture_done.store(true);
    vTaskDelete(nullptr);
}

// Counts the segments still waiting to become text.
size_t pending_segments()
{
    std::lock_guard<std::mutex> lock(s_segments_mutex);
    return s_segments.size();
}

// Queues a finished segment, converting the detector's blocks to samples.
void push_segment(uint32_t start_block, uint32_t end_block, uint16_t peak)
{
    const uint32_t floor = clip::noise_floor();
    const Segment segment = {static_cast<uint32_t>(start_block * clip::kBlockSamples),
                             static_cast<uint32_t>(end_block * clip::kBlockSamples), peak,
                             peak >= floor * kSendMultiple};
    ESP_LOGI(kTag, "Cut a %lu ms segment, peak %u, floor %lu%s",
             static_cast<unsigned long>((segment.end - segment.start) * 1000 / clip::kSampleRateHz),
             peak, static_cast<unsigned long>(floor), segment.worth_sending ? "" : ", silent");
    std::lock_guard<std::mutex> lock(s_segments_mutex);
    s_segments.push_back(segment);
}

// Best of a bad job when the room never falls quiet: the least loud block in
// the trailing two seconds, so a forced cut lands between words where it can.
uint32_t quietest_block(uint32_t now)
{
    const uint32_t first = now > kQuietSearchBlocks ? now - kQuietSearchBlocks : 0;
    uint32_t best = now;
    uint16_t best_rms = UINT16_MAX;
    for (uint32_t block = first; block <= now; ++block) {
        const uint16_t rms = clip::block_rms(block);
        if (rms < best_rms) {
            best_rms = rms;
            best = block;
        }
    }
    return best;
}

// Walks whatever block energy has arrived since the last call and cuts a
// segment where the room goes quiet, or at kMaxSegmentBlocks if it never does.
void detect_segments()
{
    Detector &d = s_detector;
    const uint32_t have = clip::blocks();
    for (; d.next_block < have; ++d.next_block) {
        const uint16_t rms = clip::block_rms(d.next_block);
        if (rms > d.peak) {
            d.peak = rms;
        }
        // clip.cpp tracks this for the meter too, so there is one estimate of
        // the room rather than two that can drift apart.
        const uint32_t floor = clip::noise_floor();
        if (d.in_silence) {
            if (rms > floor * kSpeechMultiple) {
                d.in_silence = false;
            }
        } else if (rms < floor * kSilenceMultiple) {
            d.in_silence = true;
            d.silence_block = d.next_block;
        }

        uint32_t cut = 0;
        if (d.in_silence && d.next_block - d.silence_block >= kHangoverBlocks) {
            // Just into the pause, so the segment ending here keeps trailing
            // room and the one starting here keeps the rest. Nothing overlaps,
            // so no word is transcribed twice. Capped rather than halfway
            // through, because the midpoint of a long silence walks forward as
            // the silence grows and the forced cut beats it to the end.
            cut = d.silence_block +
                  std::min((d.next_block - d.silence_block) / 2, kHangoverBlocks);
            if (cut - d.start_block < kMinSegmentBlocks) {
                cut = 0;
            }
        }
        if (cut == 0 && d.next_block - d.start_block >= kMaxSegmentBlocks) {
            cut = quietest_block(d.next_block);
        }
        if (cut > d.start_block) {
            push_segment(d.start_block, cut, d.peak);
            d.start_block = cut;
            d.peak = 0;
        }
    }
}

// Queues whatever the recording ended on, however short.
void push_tail_segment()
{
    detect_segments();
    const uint32_t start = static_cast<uint32_t>(s_detector.start_block * clip::kBlockSamples);
    const uint32_t end = clip::recorded();
    if (end > start) {
        const uint32_t floor = clip::noise_floor();
        ESP_LOGI(kTag, "Tail segment %lu ms, peak %u, floor %lu, %u already sent",
                 static_cast<unsigned long>((end - start) * 1000 / clip::kSampleRateHz),
                 s_detector.peak, static_cast<unsigned long>(floor),
                 static_cast<unsigned>(s_texts.size()));
        std::lock_guard<std::mutex> lock(s_segments_mutex);
        s_segments.push_back(
            {start, end, s_detector.peak, s_detector.peak >= floor * kSendMultiple});
    }
}

// Transcribes the oldest pending segment and frees its audio. False means it
// has run out of tries, and the segment stays at the head of the queue so the
// drain after release, or Down, can have another go.
bool upload_front_segment()
{
    Segment segment = {};
    {
        std::lock_guard<std::mutex> lock(s_segments_mutex);
        if (s_segments.empty()) {
            return true;
        }
        segment = s_segments.front();
    }
    std::string text;
    // Sending silence buys a transcript of the model guessing, and Groq bills
    // ten seconds for it whatever its real length.
    if (segment.worth_sending) {
        stt_client::Result stt;
        for (int attempt = 0; attempt < kSegmentTries; ++attempt) {
            stt = stt_client::transcribe(segment.start, segment.end - segment.start);
            if (stt.ok || !stt.retryable) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kSegmentRetryMs));
        }
        if (!stt.ok) {
            s_upload_error = stt.error;
            return false;
        }
        text = stt.text;
    }
    {
        std::lock_guard<std::mutex> lock(s_segments_mutex);
        s_segments.erase(s_segments.begin());
        s_texts.push_back(text);
    }
    s_texts_ready.fetch_add(1);
    // Only now, and only from here: post_wav replays its body on a redirect or
    // a stale socket, so the samples have to outlive the whole call.
    clip::release_to(segment.end);
    return true;
}

// Joins the segment transcripts back into one note. A space only where both
// sides are ASCII, because Chinese runs together and a seam there is not the
// word boundary it is in English. Takes the lock because the record loop
// calls this while the uploader may still be appending.
std::string join_segments()
{
    std::lock_guard<std::mutex> lock(s_segments_mutex);
    std::string out;
    for (const std::string &part : s_texts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty() && static_cast<unsigned char>(out.back()) < 0x80 &&
            static_cast<unsigned char>(part.front()) < 0x80) {
            out += ' ';
        }
        out += part;
    }
    return out;
}

// Trims a transcript to the end, on a code point boundary. Only the last
// screenful can be seen anyway, and layout_note() in ui/screen.cpp wraps the
// whole note once per face to lay it out, which on a ten minute recording
// would be thousands of characters re-wrapped four times every second.
std::string live_tail(const std::string &text)
{
    constexpr size_t kTailBytes = 1500;
    if (text.size() <= kTailBytes) {
        return text;
    }
    size_t cut = text.size() - kTailBytes;
    while (cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        ++cut;
    }
    return text.substr(cut);
}

// Uploader task: turns finished segments into text while the user is still
// talking, so the only upload left at release is the tail. Parked on a
// semaphore between recordings rather than created per recording, because a
// 16 KB internal allocation is exactly what fails after a long uptime.
void uploader_task(void *)
{
    while (true) {
        xSemaphoreTake(s_upload_go, portMAX_DELAY);
        // As process() does, so the first segment does not race the warm-up
        // into holding a second TLS session to the same host.
        http::wait_warm(4000);
        while (!s_upload_stop.load()) {
            if (pending_segments() == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            if (!upload_front_segment()) {
                // A ring releases only from the tail, so nothing behind a
                // failed segment can be freed and carrying on would be
                // pointless. The backlog absorbs the rest of the recording
                // and the drain after release retries from here.
                ESP_LOGW(kTag, "Segment upload stopped: %s", s_upload_error.c_str());
                break;
            }
        }
        s_uploader_idle.store(true);
    }
}

// Reports whether a transcript holds anything a person actually said. A silent
// clip comes back empty or as a stray full stop, and handing that to the
// cleanup model makes it answer as an assistant rather than clean anything.
bool has_speech(const std::string &text)
{
    for (unsigned char c : text) {
        // Anything non-ASCII is a byte of a CJK glyph, which is speech.
        if (c >= 0x80 || std::isalnum(c) != 0) {
            return true;
        }
    }
    return false;
}

// Shows a stage failure and remembers where to resume on Down.
void fail(Stage stage, const char *title, const std::string &reason)
{
    s_retry_stage = stage;
    screen::set_caption(reason + "  Press Down to retry.");
    screen::show(title, -1, true);
    buzzer::cue_error();
    ESP_LOGW(kTag, "%s: %s", title, reason.c_str());
}

// Restarts the station after the idle timeout stopped it; otherwise a no-op.
void wake_radio()
{
    if (!s_radio_off) {
        return;
    }
    const settings::Values s = settings::get();
    s_radio_off = false;
    screen::set_radio_off(false);
    wifi::set_hostname(s.device_name);
    wifi::connect_async(s.wifi_ssid, s.wifi_pass);
    ESP_LOGI(kTag, "Wi-Fi back on, rejoining %s", s.wifi_ssid.c_str());
}

// Runs transcribe, optional cleanup, and save from the given stage.
void process(Stage from)
{
    const settings::Values s = settings::get();
    std::string note_caption = s_record_note;
    // Ahead of the wait_connected below, which would only time out against a
    // stopped radio and fail the note with "No Wi-Fi".
    wake_radio();
    wifi::set_low_latency(true);
    // Never longer than one handshake: if warming has not finished, the
    // request below simply pays for its own.
    http::wait_warm(4000);

    if (from == Stage::Transcribe) {
        if (!wifi::connected()) {
            screen::show("Connecting Wi-Fi");
            if (!wifi::wait_connected(kWifiWaitMs)) {
                wifi::set_low_latency(false);
                fail(Stage::Transcribe, "No Wi-Fi", "Could not join " + s.wifi_ssid + ".");
                return;
            }
        }
        // Drains what the uploader did not finish during the recording. Down
        // re-enters here, and that is safe because a segment already turned
        // into text has left the queue.
        ++s_transcribe_attempts;
        bool complete = true;
        while (pending_segments() > 0) {
            char status[32];
            std::snprintf(status, sizeof(status), "Transcribing %u/%u",
                          static_cast<unsigned>(s_texts.size() + 1),
                          static_cast<unsigned>(s_texts.size() + pending_segments()));
            screen::show(status);
            if (!upload_front_segment()) {
                complete = false;
                break;
            }
        }
        const std::string joined = join_segments();
        if (!complete) {
            // Everything behind the failure is unreachable, because the ring
            // releases only from the tail. Holding nine good minutes hostage
            // to one bad segment is the worse failure, so the second time
            // through here saves what there is and says what is missing.
            if (s_transcribe_attempts < 2 || !has_speech(joined)) {
                wifi::set_low_latency(false);
                fail(Stage::Transcribe, "Transcribe failed", s_upload_error);
                return;
            }
            const size_t missing = pending_segments();
            char said[80];
            std::snprintf(said, sizeof(said), "Transcription failed, %u of %u parts missing.",
                          static_cast<unsigned>(missing),
                          static_cast<unsigned>(s_texts.size() + missing));
            note_caption = note_caption.empty() ? said : note_caption + "  " + said;
            ESP_LOGW(kTag, "Saving without %u parts: %s", static_cast<unsigned>(missing),
                     s_upload_error.c_str());
        }
        if (!has_speech(joined)) {
            wifi::set_low_latency(false);
            s_retry_stage = Stage::None;
            screen::set_caption("");
            // Nothing was said, so nothing replaces what was already there.
            screen::set_note(s_shown_note);
            screen::show("No speech detected", -1, true);
            buzzer::cue_error();
            ESP_LOGI(kTag, "Empty transcript, nothing saved");
            return;
        }
        s_pending_text = joined;

        if (s.llm_on) {
            // A status line rather than the transcript: drawing text costs a
            // full refresh of about two seconds, and the cleaned version would
            // replace it moments later. The note is drawn once, at the end.
            screen::show("Cleaning up");
            const llm_client::Result llm = llm_client::clean(s_pending_text);
            if (llm.ok) {
                s_pending_text = llm.text;
            } else {
                const std::string said = "Cleanup failed (" + llm.error + "), saved the raw text";
                note_caption = note_caption.empty() ? said : note_caption + "  " + said;
                ESP_LOGW(kTag, "Cleanup failed: %s", llm.error.c_str());
            }
        }
    } else {
        screen::show("Saving");
    }
    const obsidian_client::Result saved = obsidian_client::save(s_pending_text);
    wifi::set_low_latency(false);
    if (!saved.ok) {
        fail(Stage::Save, "Save failed", saved.error);
        return;
    }

    s_retry_stage = Stage::None;
    s_shown_note = s_pending_text;
    screen::set_note(s_pending_text);
    screen::set_caption(note_caption);
    screen::show("Saved " + clock_text(), -1, true);
    buzzer::cue_saved();
    store_last_note(s_pending_text);
}

// How the record loop ended. Only Cancelled throws the audio away; the other
// three all save whatever was said.
enum class Stop { Released, Cancelled, Ceiling };

// Meters the recording until it ends, one way or another. started_us is when
// the press arrived, which is what the latch threshold is measured against.
// allow_latch is false on the deep-sleep wake path, where the button has
// already been held through boot and a release cannot be read as a tap.
Stop record_loop(bool allow_latch, int64_t started_us)
{
    bool latched = false;
    uint32_t next_draw_s = 0;
    uint16_t shown_texts = s_texts_ready.load();
    // The capture task stopping by itself means the ring filled or the
    // microphone failed. Watching the task rather than a sample count is what
    // keeps this correct once segments are released: the resident count then
    // shrinks, so a count test would never fire.
    while (!s_capture_done.load()) {
        const int64_t elapsed_us = esp_timer_get_time() - started_us;
        if (!latched) {
            // A GPIO level read, so the release lands at once even while a
            // panel refresh still has this task blocked.
            if (!input::ai_pressed()) {
                if (!allow_latch || elapsed_us >= kLatchUs) {
                    return Stop::Released;
                }
                latched = true;
                buzzer::cue_latched();
                // The tap's own release is on its way into the queue and is
                // not a stop. Anything else queued behind it is stale too.
                input::Event stale = input::Event::None;
                while (input::wait(stale, pdMS_TO_TICKS(30))) {
                }
            }
        } else {
            input::Event event = input::Event::None;
            if (input::wait(event, 0)) {
                // A second tap is the stop. A level poll would miss one short
                // enough to fall entirely inside a blocked refresh.
                if (event == input::Event::AiDown) {
                    return elapsed_us < kCancelUs ? Stop::Cancelled : Stop::Released;
                }
                // Without this a latched recording could not be abandoned at
                // all. It is the hold and not the click because the click is
                // how the previous note is scrolled, and someone reaching for
                // that must not throw away what is being recorded. Power off
                // is unreachable here anyway, so the hold is free to mean this.
                if (event == input::Event::UpHeld) {
                    return Stop::Cancelled;
                }
            }
            if (elapsed_us >= kLatchCeilingUs) {
                return Stop::Ceiling;
            }
        }

        // Cheap: it walks only the blocks that arrived since the last pass,
        // and the block energy was folded in by the capture task already.
        detect_segments();

        // Words land in the body as they come back. This is the one place the
        // note is drawn before it is saved, and it is free: the meter is
        // already forcing a partial refresh every second, so the transcript
        // rides one that was happening anyway.
        const uint16_t ready = s_texts_ready.load();
        if (ready != shown_texts) {
            shown_texts = ready;
            screen::set_note(live_tail(join_segments()), true);
        }

        // Every second, latched or held. The seconds counter and the level
        // meter are the only evidence the microphone is still hearing you,
        // and with no hand on the button there is nothing else to go on.
        const uint32_t elapsed_s = clip::duration_ms() / 1000;
        if (elapsed_s >= next_draw_s) {
            next_draw_s = elapsed_s + 1;
            // Talking is activity. Without this a recording longer than
            // sleep_min leaves the timer expired the moment the loop ends.
            power::note_activity();
            char status[32];
            std::snprintf(status, sizeof(status), "Listening %lu:%02lu",
                          static_cast<unsigned long>(elapsed_s / 60),
                          static_cast<unsigned long>(elapsed_s % 60));
            // A full second, because that is how often this draws. A shorter
            // window samples a quarter of the audio and misses most of the
            // loud syllables: measured against the same speech, a 250 ms
            // window peaked at 272 where the second held 466.
            screen::show(status, clip::level_percent(clip::kSampleRateHz));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return Stop::Released;
}

// Records until the button or the buffer ends it, then runs the pipeline.
void record_and_process(bool allow_latch)
{
    const int64_t started_us = esp_timer_get_time();
    power::note_activity();
    clip::reset();
    s_record_note.clear();
    s_upload_error.clear();
    s_transcribe_attempts = 0;
    s_detector = Detector{};
    {
        std::lock_guard<std::mutex> lock(s_segments_mutex);
        s_segments.clear();
        s_texts.clear();
    }
    if (pdm_mic::start() != ESP_OK) {
        fail(Stage::None, "Microphone error", "The PDM microphone did not start.");
        return;
    }
    s_capture_stop.store(false);
    s_capture_done.store(false);
    s_capture_gate.store(false);
    if (xTaskCreate(capture_task, "capture", 4096, nullptr, 10, nullptr) != pdPASS) {
        pdm_mic::stop();
        fail(Stage::None, "Microphone error", "Could not start the capture task.");
        return;
    }
    buzzer::cue_start();
    // One read longer than the cue, so the buffer that was in flight while it
    // sounded is dropped instead of appended with the tail of the beep in it.
    vTaskDelay(pdMS_TO_TICKS(kCueGuardMs));
    s_capture_gate.store(true);

    // The TLS handshakes cost about 1.8 seconds each and depend on nothing the
    // microphone produces, so they run while the user is still speaking.
    const settings::Values s = settings::get();
    std::vector<http::WarmTarget> targets;
    if (!s.stt_url.empty() && !s.stt_key.empty()) {
        targets.push_back({s.stt_url, http::is_private_host(s.stt_url)});
    }
    if (s.llm_on && !s.llm_url.empty() && !s.llm_key.empty()) {
        targets.push_back({s.llm_url, http::is_private_host(s.llm_url)});
    }
    http::warm_async(targets);

    // Segments go up while the user is still talking, which is the whole
    // point, but they need the throughput: at the default power save a 25
    // second segment can take longer to send than it took to say.
    wifi::set_low_latency(true);
    s_upload_stop.store(false);
    s_uploader_idle.store(false);
    xSemaphoreGive(s_upload_go);

    display::set_promotion(false);
    const Stop stop = record_loop(allow_latch, started_us);
    display::set_promotion(true);

    s_capture_stop.store(true);
    while (!s_capture_done.load()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    pdm_mic::stop();
    // Before the uploader handshake below, which can take as long as one
    // upload. The ear is the confirmation that the button worked, and it
    // cannot be made to wait on the network.
    buzzer::cue_stop();

    // Hand the segment queue back before anything else touches it, mirroring
    // the capture handshake above. One upload may still be in flight, and
    // waiting it out is not wasted work.
    s_upload_stop.store(true);
    if (!s_uploader_idle.load()) {
        // Otherwise the panel sits on "Listening" until that upload lands.
        screen::show("Transcribing");
    }
    while (!s_uploader_idle.load()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    push_tail_segment();

    // Both of these are endings the user did not ask for, so they earn a line
    // on the note rather than the silence the old 90 second cap kept.
    if (stop == Stop::Ceiling) {
        s_record_note = "Recording stopped at the ten minute limit.";
        ESP_LOGW(kTag, "Latched recording reached the ceiling");
    } else if (clip::overflowed()) {
        s_record_note = "Recording stopped: uploads could not keep up.";
        ESP_LOGW(kTag, "Backlog full after %lu ms", static_cast<unsigned long>(clip::duration_ms()));
    }

    // Drain the release event so it is not treated as a new press later.
    input::Event drained = input::Event::None;
    while (input::wait(drained, pdMS_TO_TICKS(50))) {
    }

    if (stop == Stop::Cancelled || clip::duration_ms() < kMinRecordingMs) {
        wifi::set_low_latency(false);
        http::drop_warm();
        // Put back whatever the body held before the live transcript replaced
        // it, so abandoning a recording does not also take the last note off
        // the screen.
        screen::set_note(s_shown_note);
        // Every other ending draws the note full, which clears the ghosting a
        // recording's partials left behind. This one has no note to draw, so
        // a cancel asks for the full refresh itself. A fumble does not: it is
        // over in a third of a second and has nothing to clear.
        screen::show_ready(stop == Stop::Cancelled);
        ESP_LOGI(kTag, "Discarded %lu ms", static_cast<unsigned long>(clip::duration_ms()));
        return;
    }
    ESP_LOGI(kTag, "Recorded %lu ms", static_cast<unsigned long>(clip::duration_ms()));
    process(Stage::Transcribe);
    // The pipeline itself is not idle time, and the screen it leaves behind is
    // one the user reads. Restart the clock rather than sleeping on top of it.
    power::note_activity();
}

// Shows network and target details, and stays until a button dismisses it.
void show_info()
{
    const settings::Values s = settings::get();
    const int percent = battery::percent();
    std::vector<screen::InfoRow> rows = {
        {"Wi-Fi", wifi::connected() ? s.wifi_ssid : std::string("not connected")},
        {"Battery", (percent >= 0 ? std::to_string(percent) + "%" : std::string("n/a")) +
                        (battery::charging() ? " charging" : (battery::on_usb() ? " on USB" : ""))},
        {"Saving to",
         s.obs_mode == "daily" ? std::string("daily note") : "new notes in " + s.obs_folder},
    };
    std::vector<std::string> paragraphs = {
        "Hold Up 3 s to power off.",
        "Hold Down 3 s for setup mode.",
    };
    if (wifi::connected()) {
        rows.insert(rows.begin() + 1, {"IP address", wifi::ip()});
        // The hostname the router was actually given, not the device name it
        // came from: those differ whenever the name is not plain ASCII. The
        // address above is the fallback for a router that does not serve it.
        paragraphs.push_back("Visit http://" + wifi::hostname() + " for settings.");
    }
    screen::show_info(s.device_name, rows, paragraphs);
    s_info_showing = true;
}

// Switches to SoftAP with the captive portal until reboot.
void enter_setup()
{
    s_setup_mode = true;
    portal::stop();
    wifi::stop();
    wifi::start_ap(kSetupSsid);
    portal::start(true);
    screen::show_message("Setup mode", {
        std::string("1. Join the Wi-Fi network \"") + kSetupSsid + "\" on your phone or computer.",
        "2. Open http://192.168.4.1 in a browser.",
        "3. Enter Wi-Fi, speech, and Obsidian settings, then press Save and restart.",
    });
}

// Powers the touch panel only while a swipe would do something: a note that
// overflows the screen, showing on the screen that scrolls. Called once per
// pass of the event loop, so it converges after anything that redraws.
void sync_touch()
{
    touch::set_enabled(!s_setup_mode && !s_info_showing && screen::scrollable());
}

// Renders the sleep screen and enters deep sleep.
void go_to_sleep()
{
    screen::show_asleep(true);
    display::sleep();
    power::deep_sleep();
}

// Task body: boot decisions, then the event loop.
void run(void *)
{
    s_shown_note = load_last_note();
    screen::set_note(s_shown_note);
    screen::set_caption("");
    power::note_activity();
    // Seed the cache before anything draws, or the first screen reports a
    // gauge that has never been read.
    battery::poll();

    // The cable moved while the panel was holding the sleep screen. Redraw the
    // bar and go straight back down: nothing else about the device changed,
    // and bringing the radio up would cost more than the picture is worth.
    if (board::woke_from_power()) {
        go_to_sleep();
    }

    const bool wake_recording = board::woke_from_button() && input::ai_pressed();
    if (!settings::wifi_configured()) {
        enter_setup();
    } else {
        const settings::Values s = settings::get();
        // Before the association, because the hostname travels with the DHCP
        // request and a later change waits for the next lease.
        wifi::set_hostname(s.device_name);
        wifi::connect_async(s.wifi_ssid, s.wifi_pass);
        wifi::start_sntp(s.tz);
        portal::start(false);
        if (wake_recording) {
            // No latching here. The button has been held right through boot,
            // so the release that follows says nothing about how long it was
            // down, and a wake press would latch a recording nobody wanted.
            record_and_process(false);
        } else {
            screen::show_ready(true);
        }
    }

    // Holding the side button is how the device is switched on, so it is still
    // down when the button component takes its first scan and queues a press
    // nobody meant. Waking from deep sleep is the deliberate exception: there
    // the hold is the recording, and record_and_process has drained it already.
    if (!wake_recording) {
        while (input::ai_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        input::Event stale = input::Event::None;
        while (input::wait(stale, 0)) {
        }
    }

    // One check owns every idle repaint, so the Wi-Fi indicator and the
    // battery reading cannot each decide to paint the panel on their own.
    bool shown_link = wifi::connected();
    bool shown_radio_off = s_radio_off;
    int shown_battery = battery_step();
    bool shown_charging = battery::charging();
    bool shown_usb = battery::on_usb();
    uint32_t seconds_since_poll = 0;

    while (true) {
        sync_touch();
        input::Event event = input::Event::None;
        if (!input::wait(event, pdMS_TO_TICKS(1000))) {
            const settings::Values s = settings::get();
            if (!s_setup_mode && power::idle_expired(s.sleep_min)) {
                go_to_sleep();
            }
            // Setup mode owns the panel with its instructions, and the gauge
            // is not worth waking the bus for while nobody can see it.
            if (s_setup_mode) {
                continue;
            }
            // Sits below that guard on purpose: in setup mode the portal is
            // the only way back in, so stopping the radio would strand us.
            if (!s_radio_off && power::idle_expired(s.wifi_idle_min)) {
                // A parked socket is dead the moment the station goes down,
                // and holding it would keep its TLS session in internal RAM.
                http::drop_warm();
                wifi::stop();
                s_radio_off = true;
                screen::set_radio_off(true);
                ESP_LOGI(kTag, "Wi-Fi stopped after %d idle minutes", s.wifi_idle_min);
            }
            if (++seconds_since_poll >= kBatteryPollSeconds) {
                seconds_since_poll = 0;
                battery::poll();
            }
            if (wifi::connected() != shown_link || s_radio_off != shown_radio_off ||
                battery_step() != shown_battery || battery::charging() != shown_charging ||
                battery::on_usb() != shown_usb) {
                shown_link = wifi::connected();
                shown_radio_off = s_radio_off;
                shown_battery = battery_step();
                shown_charging = battery::charging();
                shown_usb = battery::on_usb();
                // Trackers still move while the info screen is up, so
                // dismissing it does not trigger a second repaint.
                if (!s_info_showing) {
                    screen::redraw();
                }
            }
            continue;
        }
        power::note_activity();
        wake_radio();
        // The info screen stays up until a button dismisses it, so the first
        // press after it appears returns to the note instead of acting. A
        // record press is the exception: it goes straight to recording.
        if (s_info_showing) {
            s_info_showing = false;
            if (event != input::Event::AiDown) {
                if (s_retry_stage == Stage::None) {
                    screen::show_ready(true);
                } else {
                    screen::show("Retry with Down", -1, true);
                }
                continue;
            }
        }
        switch (event) {
        case input::Event::AiDown:
            if (!s_setup_mode) {
                record_and_process(true);
            }
            break;
        // HOTFIX: Up and Down still scroll, which the touch panel was meant to
        // take over. Remove both scroll calls here once a device is seen
        // reporting a touch: every unit tested so far has a GT911 with no
        // configuration loaded, and a swipe-only build cannot scroll at all on
        // one. See docs/hardware.md, "The GT911 reports no configuration".
        case input::Event::UpClick:
            if (!s_setup_mode && !screen::scroll(-1)) {
                show_info();
            }
            break;
        case input::Event::UpHeld:
            // Two screens: the first is a cheap partial that acknowledges the
            // hold, the second is the image the panel keeps once the rail is
            // gone, so it describes the finished state rather than the act.
            screen::show("Powering off");
            screen::show_message("Powered off", {"Hold the side (AI) button to turn back on."});
            display::sleep();
            board::power_off();
            break;
        case input::Event::DownClick:
            if (s_setup_mode) {
                break;
            }
            if (s_retry_stage != Stage::None) {
                process(s_retry_stage);
                power::note_activity();
            } else {
                screen::scroll(1);
            }
            break;
        case input::Event::DownHeld:
            if (s_setup_mode) {
                esp_restart();
            }
            enter_setup();
            break;
        // The finger carries the text with it, so a swipe up shows what was
        // below the last visible line.
        case input::Event::SwipeUp:
            screen::scroll(1);
            break;
        case input::Event::SwipeDown:
            screen::scroll(-1);
            break;
        case input::Event::AiUp:
        case input::Event::None:
            break;
        }
    }
}

}  // namespace


esp_err_t start()
{
    // Both tasks are made once and kept. Creating the uploader per recording
    // would put a 16 KB internal allocation in the path of every note, and
    // that is the allocation that fails after a long uptime.
    s_upload_go = xSemaphoreCreateBinary();
    if (s_upload_go == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(uploader_task, "uploader", 16384, nullptr, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    // 20 KB covers TLS handshakes inside the HTTP clients.
    return xTaskCreate(run, "pipeline", 20480, nullptr, 5, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace pipeline
