// =============================================================================
// PIPELINE
// =============================================================================
// State machine: IDLE -> RECORDING -> TRANSCRIBING -> CLEANING -> SAVING.
// A failed transcribe or save keeps the clip and text so Down retries the
// stage; a failed cleanup falls back to the raw transcript.
#include "app/pipeline.h"

#include <atomic>
#include <cstdio>
#include <ctime>
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
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/http.h"
#include "net/llm_client.h"
#include "net/obsidian_client.h"
#include "net/stt_client.h"
#include "net/wifi.h"
#include "nvs.h"
#include "portal/portal.h"
#include "ui/display.h"
#include "ui/screen.h"

namespace pipeline {
namespace {

constexpr const char *kTag = "pipeline";
constexpr uint32_t kMinRecordingMs = 300;
constexpr uint32_t kWifiWaitMs = 20000;
constexpr const char *kSetupSsid = "Sticky-Setup";
constexpr const char *kNvsNamespace = "sticky";
constexpr const char *kNvsNoteKey = "last_note";
constexpr uint32_t kBatteryPollSeconds = 60;
constexpr int kBatteryStepPercent = 5;

enum class Stage { None, Transcribe, Save };

Stage s_retry_stage = Stage::None;
std::string s_pending_text;
std::atomic<bool> s_capture_stop{false};
std::atomic<bool> s_capture_done{true};
bool s_setup_mode = false;
bool s_radio_off = false;
bool s_info_showing = false;

// Buckets the charge so the panel is repainted on a visible change rather
// than on every percent, and keeps an unreadable gauge distinct from a flat
// one. A full refresh costs about a second, so this bucket is what bounds how
// often an idle device paints at all.
int battery_step()
{
    const int percent = battery::percent();
    return percent < 0 ? -1 : percent / kBatteryStepPercent;
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
    nvs_set_str(handle, kNvsNoteKey, note.substr(0, 3900).c_str());
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
        if (got > 0 && !clip::append(buffer, got)) {
            break;
        }
    }
    s_capture_done.store(true);
    vTaskDelete(nullptr);
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
    wifi::connect_async(s.wifi_ssid, s.wifi_pass);
    ESP_LOGI(kTag, "Wi-Fi back on, rejoining %s", s.wifi_ssid.c_str());
}

// Runs transcribe, optional cleanup, and save from the given stage.
void process(Stage from)
{
    const settings::Values s = settings::get();
    std::string note_caption;
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
        screen::show("Transcribing");
        const stt_client::Result stt = stt_client::transcribe();
        if (!stt.ok) {
            wifi::set_low_latency(false);
            fail(Stage::Transcribe, "Transcribe failed", stt.error);
            return;
        }
        s_pending_text = stt.text;

        if (s.llm_on) {
            // A status line rather than the transcript: drawing text costs a
            // full refresh of about two seconds, and the cleaned version would
            // replace it moments later. The note is drawn once, at the end.
            screen::show("Cleaning up");
            const llm_client::Result llm = llm_client::clean(s_pending_text);
            if (llm.ok) {
                s_pending_text = llm.text;
            } else {
                note_caption = "Cleanup failed (" + llm.error + "), saved the raw text";
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
    screen::set_note(s_pending_text);
    screen::set_caption(note_caption);
    screen::show("Saved " + clock_text(), -1, true);
    buzzer::cue_saved();
    store_last_note(s_pending_text);
}

// Records while the AI button is held, then runs the pipeline.
void record_and_process()
{
    power::note_activity();
    clip::reset();
    if (pdm_mic::start() != ESP_OK) {
        fail(Stage::None, "Microphone error", "The PDM microphone did not start.");
        return;
    }
    s_capture_stop.store(false);
    s_capture_done.store(false);
    if (xTaskCreate(capture_task, "capture", 4096, nullptr, 10, nullptr) != pdPASS) {
        pdm_mic::stop();
        fail(Stage::None, "Microphone error", "Could not start the capture task.");
        return;
    }
    buzzer::cue_start();

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

    // Meter and elapsed time once a second; partial refreshes are slower than
    // that, so the loop never tries to keep up with the audio.
    uint32_t last_shown_s = UINT32_MAX;
    while (input::ai_pressed() && clip::sample_count() < clip::kMaxSamples) {
        const uint32_t elapsed_s = clip::duration_ms() / 1000;
        if (elapsed_s != last_shown_s) {
            last_shown_s = elapsed_s;
            char status[32];
            std::snprintf(status, sizeof(status), "Listening %lu:%02lu",
                          static_cast<unsigned long>(elapsed_s / 60), static_cast<unsigned long>(elapsed_s % 60));
            screen::show(status, clip::level_percent(clip::kSampleRateHz / 4));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    s_capture_stop.store(true);
    while (!s_capture_done.load()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    pdm_mic::stop();
    buzzer::cue_stop();

    // Drain the release event so it is not treated as a new press later.
    input::Event drained = input::Event::None;
    while (input::wait(drained, pdMS_TO_TICKS(50))) {
    }

    if (clip::duration_ms() < kMinRecordingMs) {
        http::drop_warm();
        screen::show("Ready");
        return;
    }
    ESP_LOGI(kTag, "Recorded %lu ms", static_cast<unsigned long>(clip::duration_ms()));
    process(Stage::Transcribe);
}

// Shows network and target details, and stays until a button dismisses it.
void show_info()
{
    const settings::Values s = settings::get();
    const int percent = battery::percent();
    std::vector<std::string> lines = {
        "Wi-Fi: " + (wifi::connected() ? s.wifi_ssid : std::string("not connected")),
        "Battery: " + (percent >= 0 ? std::to_string(percent) + "%" : std::string("n/a")) +
            (battery::charging() ? " charging" : (battery::on_usb() ? " on USB" : "")),
        "Saving to: " + (s.obs_mode == "daily" ? std::string("daily note") : "new notes in " + s.obs_folder),
        "Hold Up 3 s to power off. Hold Down 3 s for setup mode.",
    };
    if (wifi::connected()) {
        lines.insert(lines.begin() + 1,
                     "IP address: " + wifi::ip() + " (visit address for settings)");
    }
    screen::show_message("Obsidian Sticky", lines);
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

// Renders the sleep screen and enters deep sleep.
void go_to_sleep()
{
    screen::show("Sleeping", -1, true);
    display::sleep();
    power::deep_sleep();
}

// Task body: boot decisions, then the event loop.
void run(void *)
{
    screen::set_note(load_last_note());
    screen::set_caption("");
    power::note_activity();
    // Seed the cache before anything draws, or the first screen reports a
    // gauge that has never been read.
    battery::poll();

    const bool wake_recording = board::woke_from_button() && input::ai_pressed();
    if (!settings::wifi_configured()) {
        enter_setup();
    } else {
        const settings::Values s = settings::get();
        wifi::connect_async(s.wifi_ssid, s.wifi_pass);
        wifi::start_sntp(s.tz);
        portal::start(false);
        if (wake_recording) {
            record_and_process();
        } else {
            screen::show("Ready", -1, true);
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
                screen::show(s_retry_stage == Stage::None ? "Ready" : "Retry with Down", -1, true);
                continue;
            }
        }
        switch (event) {
        case input::Event::AiDown:
            if (!s_setup_mode) {
                record_and_process();
            }
            break;
        // Up and Down page through a long note; at the top Up opens the info
        // screen, and after a failure Down retries instead of paging.
        case input::Event::UpClick:
            if (!s_setup_mode && !screen::scroll(-1)) {
                show_info();
            }
            break;
        case input::Event::UpHeld:
            screen::show_message("Powering off", {"Press the side button to turn back on."});
            display::sleep();
            board::power_off();
            break;
        case input::Event::DownClick:
            if (s_setup_mode) {
                break;
            }
            if (s_retry_stage != Stage::None) {
                process(s_retry_stage);
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
        case input::Event::AiUp:
        case input::Event::None:
            break;
        }
    }
}

}  // namespace


esp_err_t start()
{
    // 20 KB covers TLS handshakes inside the HTTP clients.
    return xTaskCreate(run, "pipeline", 20480, nullptr, 5, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace pipeline
