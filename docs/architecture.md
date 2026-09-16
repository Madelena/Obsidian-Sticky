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
| `render` | `display::init()` | 4096, 4 | SPI2 and the panel; the only task that waits out a waveform |
| `warm` | `http::warm_async()` | 8192, 4 | Opening TLS connections during recording |
| httpd | `portal::start()` | 16384, the HTTP server's own | The settings page, the JSON API, the three Test routes |

Rules that follow from the split:

- **Only `render` touches the panel.** Other tasks rotate the canvas and hand
  a waveform over; `display.cpp` serializes that behind `s_refresh_mutex`.
- **Only `capture` reads the microphone.** `pipeline` starts and stops the
  channel around it and never reads samples itself.
- **Button callbacks touch nothing.** They run in the button component's timer
  context, so `main/app/input.cpp` does one `xQueueSend` and returns. A
  display refresh or an I2C read from there deadlocks or trips the watchdog.
- **The httpd task draws.** `POST /api/show` and a settings save both call
  into `screen`, which is the one place a task other than `pipeline` paints.
  The panel handoff is safe because it goes through the refresh mutex, but the
  canvas itself has no lock, so a portal draw landing in the middle of a
  pipeline draw would interleave. In practice the user is doing one or the
  other. Anything that adds a third drawing caller should fix this properly.

## The pipeline state machine

`IDLE -> RECORDING -> TRANSCRIBING -> CLEANING -> SAVING`, in
`main/app/pipeline.cpp`, which is the only place stages are sequenced.

- **RECORDING** runs while `input::ai_pressed()` holds, capped at 90 seconds
  by `clip::kMaxSamples`. Anything under 300 ms is discarded as a fumble.
- **TRANSCRIBING** needs Wi-Fi, and waits up to 20 s for it before failing.
- **CLEANING** is skipped when `llm_on` is off. A failed cleanup is not a
  failed note: the raw transcript is saved and a caption says so. Only a
  cleanup that succeeds replaces the text.
- **SAVING** appends to the daily note or creates a file, then paints the note
  once.

A failure calls `fail()`, which records the stage in `s_retry_stage` and puts
"Press Down to retry" on screen. Down then resumes from that stage.

| Failed stage | Down retries from | Because |
| --- | --- | --- |
| Transcribe | the audio, still in PSRAM | Nothing has to be spoken again |
| Save | the text, still in `s_pending_text` | The upload does not repeat |

Cleanup has no retry stage of its own, because its failure path has already
produced a saveable note.

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
- the battery percentage bucketed to 5 percent steps
- charge direction
- USB presence

If any differs from what is on screen, one `screen::redraw()` runs. The point
is that the battery reading and the Wi-Fi indicator cannot each decide to
paint the panel on their own: a full refresh costs about a second, and two
owners means two of them. The 5 percent bucket is what bounds how often an
idle device paints at all. The trackers keep moving while the info screen is
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
  so `init()` forces it back to `medium` instead of letting
  `screen::note_face()` pick blind. `apply_json()` rejects a bad value from
  the portal outright, which is the different case: there a person can be told.

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
