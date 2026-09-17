# Architecture

How the firmware is put together and why it is put together that way. The
module map, which is the what, is in `CLAUDE.md`; this file is the reasoning
behind the parts that are not obvious from reading one file.

## Tasks

`app_main` in `main/main.cpp` only orders the bring-up and then returns.
Everything after boot runs on these tasks.

| Task | Created by | Stack, priority | Owns |
| --- | --- | --- | --- |
| `pipeline` | `pipeline::start()` | 20480, 5 | The state machine, every drawing decision, the buzzer, NVS writes, mic start and stop |
| `capture` | `record_and_process()` | 4096, 10 | The I2S PDM RX channel, for the length of one recording |
| `uploader` | `pipeline::start()` | 16384, 4 | Transcribing finished segments while the recording is still running |
| `render` | `display::init()` | 4096, 4 | SPI2 and the panel; the only task that waits out a waveform |
| `warm` | `http::warm_async()` | 8192, 4 | Opening TLS connections during recording |
| `touch` | `touch::init()` | 3072, 4 | I2C0, the GT911, and turning a finger into a swipe |
| httpd | `portal::start()` | 16384, the HTTP server's own | The settings page, the JSON API, the three Test routes |

Rules that follow from the split:

- **Only `render` touches the panel.** Other tasks rotate the canvas and hand
  a waveform over; `display.cpp` serializes that behind `s_refresh_mutex`.
- **Only `capture` reads the microphone.** `pipeline` starts and stops the
  channel around it and never reads samples itself.
- **Button callbacks touch nothing.** They run in the button component's timer
  context, so `main/app/input.cpp` does one `xQueueSend` and returns. A
  display refresh or an I2C read from there deadlocks or trips the watchdog.
- **Only `touch` holds the GT911.** The task owns the power pin, the I2C0 bus
  and the device handle outright, and `touch::set_enabled()` only sets an
  atomic that the task acts on. Nothing is locked because nothing else is a
  candidate to touch it. That also keeps the 120 ms reset sequence off the
  pipeline task, which is the one whose latency is measured.
- **The httpd task draws.** `POST /api/show` and a settings save both call
  into `screen`, which is the one place a task other than `pipeline` paints.
  The panel handoff is safe through the refresh mutex, and the canvas behind
  it is serialized by `s_mutex` in `screen.cpp`, which every public entry
  point takes. A new drawing caller needs nothing beyond going through
  `screen`.

## The pipeline state machine

`IDLE -> RECORDING -> TRANSCRIBING -> CLEANING -> SAVING`, in
`main/app/pipeline.cpp`, which is the only place stages are sequenced.

- **RECORDING** runs while `input::ai_pressed()` holds, or until the next tap
  when a tap latched it. There is no length cap: `clip` is a ring, and a
  segment's samples are freed once it has become text, so what bounds a
  recording is how far the uploader falls behind rather than how much PSRAM
  there is. A latched recording stops itself at ten minutes. Anything under
  300 ms is discarded as a fumble.
- **TRANSCRIBING** needs Wi-Fi, and waits up to 20 s for it before failing. By
  the time it runs, most of the audio is usually already text: the `uploader`
  task has been sending finished segments throughout the recording, so this
  stage normally drains only the tail.
- **CLEANING** is skipped when `llm_on` is off. A failed cleanup is not a
  failed note: the raw transcript is saved and a caption says so. Only a
  cleanup that succeeds replaces the text.
- **SAVING** appends to the daily note or creates a file, then paints the note
  once.

A failure calls `fail()`, which records the stage in `s_retry_stage` and puts
"Press Down to retry" on screen. Down then resumes from that stage.

| Failed stage | Down retries from | Because |
| --- | --- | --- |
| Transcribe | the segments not yet transcribed, still in PSRAM | Only the part that failed has to be sent again |
| Save | the text, still in `s_pending_text` | The upload does not repeat |

Cleanup has no retry stage of its own, because its failure path has already
produced a saveable note.

A Transcribe retry is idempotent: a segment that became text has left the
queue, so re-entering `process()` picks up where it stopped. The second such
retry gives up on the failing segment instead, saves what did transcribe, and
captions how many parts are missing.

## Segmenting the audio

No OpenAI-compatible transcription endpoint takes a live audio stream, so the
recording is cut into finished WAVs and each is posted on its own while the
user keeps talking. That turns the upload from the largest post-release cost
into something that mostly happens for free during the recording.

The detector lives on the pipeline task and reads the per-block RMS that
`clip::append()` folds in on the capture task. It cuts where the room goes
quiet, on a two-threshold Schmitt trigger with 640 ms of hangover, and at
the midpoint of the pause rather than its start, so both sides of the seam
keep padding and no audio is sent twice. A segment has to reach roughly 8
seconds before a cut is allowed, and a cut is forced at about 12 if the room
never falls quiet, at the quietest block in the trailing two seconds so it
lands between words where it can. Segment length is a feedback decision, not
a network one: it is how often new words can reach the screen. Groq bills a
minimum of ten seconds per request, so the shorter segments pay for a little
silence, which at roughly four cents an hour is worth less than the feedback.

Cross-segment context is deliberately not passed as the endpoint's `prompt`.
Whisper echoes prompts on low-content audio, and `llm_client::clean()`
already runs over the whole concatenation, where it can see both sides of
every seam at once.

## Warm connections

The subtlest thing in the firmware, in `main/net/http.cpp`.

A TLS handshake to a public API measures about 1.8 to 2.2 s on this chip, and
it depends on nothing the microphone produces. So `record_and_process()` calls
`http::warm_async()` when the button goes down, and the requests that follow
the release find the sockets already open.

How it works:

1. Two slots, `s_warm[kWarmSlots]`, keyed by host. Two is exactly what the
   transcription host and the cleanup host need.
2. `warm_one()` opens a plain GET to the same URL the real request will use,
   reads the body, and deliberately does not close the socket.
3. `claim_warm()` matches a later URL to a slot by host and hands the handle
   over. `esp_http_client_set_url` only drops the connection when the host
   changes, so the real request may use a different path, method and headers
   on the same host and still ride the open socket.
4. If the server had already dropped the connection, the request fails at the
   transport. `request()` and `post_wav()` both catch that, log
   `warm connection was stale, reconnecting`, and build a fresh client. The
   worst case is therefore exactly the old behaviour, one handshake paid late.

Two consequences worth holding on to:

- `warm_one()` cannot use `exchange()`, even though the flow is nearly
  identical, because `exchange()` closes the connection when it is done.
  Closing the socket is the one thing a warm-up must not do.
- A slot is claimed once and never handed back. Parking a socket after a POST
  and reusing it for the next segment was tried and measured three times
  slower than a fresh connection, so every segment pays its own handshake.
  `docs/latency.md` has the numbers under "Why segment uploads do not reuse a
  socket". Do not try it again without re-reading that section.
- A parked slot is dropped after 30 seconds. `esp_http_client_open()` against
  a peer that has hung up succeeds locally and only fails at
  `fetch_headers()`, so claiming a dead socket wastes the entire upload
  before the retry starts. Half of a typical 60 second server idle timeout is
  the safe side of that trade.
- `pipeline::process()` calls `http::wait_warm(4000)` before its first
  request, so a request never races the handshake it is trying to skip. The
  bound is never worse than one handshake: if warming has not finished, the
  request simply pays for its own.

`http::drop_warm()` releases both slots, and a recording too short to send
calls it so nothing is left holding a socket.

## The render task

The panel blocks its caller for the whole waveform, about 1.9 s full and 0.9 s
partial (see `docs/latency.md`). The panel and the radio share nothing, so
that wait belongs on its own task.

The discipline in `main/ui/display.cpp`:

- **Rotation happens on the caller's task**, not the render task. It costs a
  few milliseconds and it is what keeps frame ordering exact: the bytes in
  `s_rotated` always belong to the frame the caller just drew.
- **A new draw waits for the previous waveform** before touching `s_rotated`,
  through `wait_idle_locked()`. There is one rotation buffer, and overwriting
  it mid-transfer would tear the frame on the panel.
- **`display::sleep()` waits too.** The image left on the panel when power
  drops is the one the user keeps reading, so the controller must never be put
  to sleep with a waveform still running.
- `refresh_partial()` promotes every 20th call to a full refresh, because
  e-ink accumulates ghosting across partials.

## One check owns every idle repaint

The idle branch of the `pipeline` event loop holds a single consolidated
comparison, and it is the only thing that repaints a resting device. It tracks
five things at once:

- Wi-Fi link state
- radio-off state, after the Wi-Fi idle timeout stopped the station
- the battery percentage bucketed by `icons::battery_step()`
- charge direction
- USB presence

If any differs from what is on screen, one `screen::redraw()` runs. The point
is that the battery reading and the Wi-Fi indicator cannot each decide to
paint the panel on their own: a full refresh costs about a second, and two
owners means two of them. That bucket is the same one the gauge icon draws
from, so the panel never paints for a change too small to see, and it is what
bounds how often an idle device paints at all. It went from twenty 5 percent
buckets to the icon's own steps when the bar stopped showing a number, which
is seven repaints over a full discharge rather than twenty. The trackers keep moving while the info screen is
up, so dismissing it does not trigger a second repaint.

## Settings

`main/app/settings.cpp` is NVS backed, edited through the captive portal, and
read everywhere through a mutex-guarded copy.

- **NVS keys are capped at 15 characters.** The table in `kStrings` is the
  authority; a longer key silently fails to store.
- **Secrets are never sent back to the browser.** `to_json()` emits
  `<key>_set` as a boolean for a secret field, and `apply_json()` treats a
  blank secret as "keep what is stored".
- **Unknown enumerated values fall back to a default** rather than leaving the
  renderer to guess. A `text_size` written by other firmware has no face here,
  so `init()` forces it back to `auto` instead of letting
  `screen::layout_note()` pick blind. `apply_json()` rejects a bad value from
  the portal outright, which is the different case: there a person can be told.

## The touch panel is powered on demand

`sync_touch()` runs once per pass of the pipeline event loop and asks for one
thing: is a note that overflows the screen showing on the screen that
scrolls. Nothing else decides, the same way one check owns every idle
repaint.

- **Powering it costs about 120 ms**, spent on the touch task, and only on a
  transition. The loop calls `set_enabled()` every second with the same
  answer and that is a store to an atomic.
- **A failed bring-up latches.** If the GT911 does not answer once, it is not
  asked again until the next boot, so a board with a dead panel does not pay
  the reset sequence on every long note. A controller that answers but has no
  configuration loaded is not a failure by this definition: it is powered and
  polled, and simply never reports. The start-up line says which it is.
- **Disable releases the bus and floats the pins.** The controller is
  unpowered afterwards, and a pull-up left driving an unpowered chip is a
  leak, which is why `float_pin()` clears what `gpio_reset_pin()` sets.

## Codecs

Someone will ask why 32000 bytes per second of raw PCM goes up the wire when
the upload is now the largest remaining cost. This is what the investigation
found, and why it was deferred.

- **Espressif's `esp_audio_codec` has encoders** for ADPCM, G.711, Opus, AAC,
  LC3 and SBC. It has no encoder for FLAC or MP3; both are decode only.
- **The only maintained-looking MP3 encoder port for ESP-IDF 5** is
  github.com/anton-malakhov/mp3_shine_esp32, last touched in 2023 and carrying
  no clear license, which is a poor fit for an MIT repository.
- **Providers dispatch on the multipart filename extension**, so any codec
  change must also change the filename and the content type in `post_wav()` in
  `main/net/http.cpp`, not just the bytes.
- **Provider acceptance differs.** Groq accepts flac, mp3, mp4, mpeg, mpga,
  m4a, ogg, wav and webm, and recommends FLAC. OpenAI no longer lists flac or
  ogg.
- **ADPCM and G.711 u-law ride inside a WAV container**, as format tags 0x0011
  and 0x0007, so the filename would not have to change. But neither provider
  documents non-PCM WAV, so acceptance is unverified and only a real request
  would settle it.
- **Encoding cost is not the obstacle.** Every candidate runs many times
  faster than real time on one core. Format acceptance and effort are.
