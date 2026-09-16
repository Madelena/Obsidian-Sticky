# Design

What the device looks like, how it behaves, and why each of those went the
way it did. Read it before changing anything the user sees or hears, so that
a deliberate choice is not undone by accident. The mechanism behind these
decisions is in `docs/architecture.md`; this file is the intent.

## The governing principle

Single purpose. In Madelena's words: "I definitely want this to be a
single-purpose device that I can simply pick up, speak, and remember."

There are no pages, no menus, and no touch interface. The GT911 touch
controller is on the board and is deliberately left uninitialized and powered
down, with `PIN_TOUCH_EN` and `PIN_TOUCH_RST` driven low at boot in
`board::init()`. Followup, the comparable community firmware for this board,
was explicitly rejected as a model because "the UX is already too
complicated."

Any proposal that adds a mode, a menu or a second screen is working against
this. That includes the tempting ones.

## The screen

One screen, 800x480, one bit per pixel. Top to bottom: a status band, an
optional single caption line, then the note body. The constants are at the
top of `main/ui/screen.cpp`.

| Band | Geometry | Carries |
| --- | --- | --- |
| Status | 76 px tall, closed by a 2 px rule inset to the margins | The state, left, in bold 30 px. Page indicator, Wi-Fi and battery, right, in 22 px |
| Caption | One 22 px line at y 88, hidden when empty | A failure reason, or a note that the raw text was saved |
| Body | y 88 to y 468, or below the caption when one is showing | The note, wrapped and paged |

Side margins are 36 px throughout, and the body keeps 12 px clear above and
below. While recording, the right side of the status band is replaced by a
220 px level meter. A caption costs the body one line by pushing `body_top()`
down; an empty caption gives the space straight back.

A footer band used to sit at the bottom carrying the firmware version and the
IP address. It was dropped in `aff9c7d`: both already live on the info
screen, and the space is worth more as note text. The version is now stamped
at the foot of `screen::show_message()`, so it appears on the info screen and
nowhere in normal use. The IP address is left to whichever caller wants it,
which is why that function does not repeat it.

The note body is paged, never scrolled continuously. E-ink cannot animate, so
a scroll would be a sequence of full-page repaints with nothing gained.

## Text sizes

Four, selectable as `text_size` on the settings page.

| Setting | Face in code | Nominal | Lines per page |
| --- | --- | --- | --- |
| small | `font::body()` | 30 px | 9 |
| medium (default) | `font::large()` | 40 px | 7 |
| large | `font::xlarge()` | 52 px | 5 |
| xlarge | `font::xxlarge()` | 64 px | 4 |

Madelena revised the scale twice while using the device, and the second
revision is
why the first two columns do not line up. A 22 px face was originally the
smallest choice and was dropped as "impractically small" for a device read at
arm's length on a fridge or a desk. Every label then shifted down one step,
so what had been Medium became Small, while the internal face names stayed
where they were. The face named `large` is the setting named `medium`.
`note_face()` in `main/ui/screen.cpp` is the whole mapping. Do not rename
either side to make them agree: the setting strings are stored in NVS and the
face names are in the generated headers.

The default is medium, which puts the same physical size on screen as the
previous default did under its old name. The 22 px face itself was not
removed, it is still the caption and status-band face.

Line pitch is 1.10 times the glyph box, emitted by `tools/gen_font.py` into
each face's `line_height`. It was tightened from 1.15 to fit more lines. The
lines-per-page arithmetic charges the last line only its glyph box rather
than a full pitch, which is what fits a fifth 52 px line into the band; the
formula and the per-face numbers are in `docs/hardware.md`.

The Latin faces are Atkinson Hyperlegible, chosen for legibility at a glance
and because its licence is OFL. They are baked to 1-bit bitmaps at build time
by `tools/gen_font.py`, ASCII only, U+0020 to U+007E. Everything above U+007F
is rasterized at draw time from a Noto Sans TC subset in the `font` flash
partition, so Chinese notes are readable on screen and not merely correct in
the vault. Without that partition `text::prepare()` folds those code points
to ASCII approximations rather than failing.

## Feedback, which is the heart of the interaction

Madelena's requirement, verbatim: the user "should see results as soon as
possible" and "should get some sort of feedback loop so they know their
action is working as intended."

The device is used eyes-free at the moment of capture, so the buzzer carries
the interaction and the screen carries the result. The cues are in
`main/board/buzzer.cpp`:

| Cue | Sound | Means |
| --- | --- | --- |
| `cue_start()` | Rising pair, 1800 then 2400 Hz | Recording has started |
| `cue_stop()` | One 2000 Hz tone | Recording has stopped |
| `cue_saved()` | High double, 2800 Hz twice | The note is in the vault |
| `cue_error()` | One low 900 Hz tone, 250 ms | A stage failed |

The `beep` setting switches all four off, and `buzzer::beep()` still spends
the duration as a delay when it is off, so timing does not change with the
sound.

On screen the state progresses through plain verbs: "Listening 0:07" with a
level meter, then "Transcribing", then "Cleaning up" when that is enabled,
then "Saved 14:32". Nouns and jargon are avoided.

### Why the note is drawn once

There is a tension here that shaped the current design and should be
understood before anyone changes it. Drawing costs real time: a full refresh
blocks for about 1.95 s and a partial for about 0.88 s, both measured. The
firmware used to draw the note three times per capture, twice with text that
was replaced moments later. `process()` in `main/app/pipeline.cpp` now draws
the note exactly once, after it is safely in the vault, and spends cheap
partial status lines in between.

That is why a status word appears while cleanup runs rather than the raw
transcript. Putting the transcript back there is not a small change, it is
about two seconds added to every note. The measurements are in
`docs/latency.md`.

## Controls

Three buttons, no chords.

| Input | Action |
| --- | --- |
| Hold the side button | Record. Recording starts on the press itself, not after a long-press threshold, so the microphone is live with no delay. |
| Release the side button | Stop and run the pipeline. A clip under 300 ms is discarded as a fumble. |
| Press Up | Page back through a long note. At the top of the note it opens the info screen instead. |
| Hold Up 3 s | Power off. |
| Press Down | Page forward. After a failure it retries the failed stage instead. |
| Hold Down 3 s | Enter setup mode, or restart when already in it. |

Power off lives on Up for a hardware reason, not a design one: the side
button cannot carry a long press at all, because holding it is how recording
works, and the release drain at the end of `record_and_process()` swallows
anything queued behind it. `docs/hardware.md` has the full account.

### The info screen

It has no timeout. It previously dismissed itself after 12 seconds and the
owner rejected that: "There should not be a timeout moving from the status
screens back to the Notes screen. It should just stay at the screen the user
has chosen." It now stays until a button dismisses it.

Two consequences in the event loop in `main/app/pipeline.cpp`:

- A press of the side button records rather than merely dismissing, because
  that is plainly what pressing it means. Every other button dismisses and
  does nothing else.
- Idle repaints are suppressed while `s_info_showing` holds, so a battery
  change cannot pull the screen away from someone reading it. The trackers
  keep moving underneath, so dismissing it does not then trigger a repaint of
  its own.

It shows the Wi-Fi network, the IP address once with the hint that it is also
the settings page, the battery, where notes are being saved, and the firmware
version. The address used to appear three times on that screen and was cut
back to one.

## Wording

Small choices that were made deliberately.

- The Wi-Fi indicator reads "Wi-Fi", "Wi-Fi off" or "No Wi-Fi". "off" reads
  as chosen and "No" reads as a fault, and the shared prefix keeps the right
  edge of the band from jumping between the three.
- The battery shows "CHG" only when charge is actually flowing into the pack
  and "USB" when a cable is attached but the pack is full. Conflating those
  was a bug; `docs/hardware.md` explains why the gauge's discharge bit cannot
  tell them apart.
- A failure puts the stage in the title and the reason plus "Press Down to
  retry." in the caption, so the screen says both what broke and what to do.

## Failure is part of the design

Nothing a person said out loud should ever be lost to a transient error.

- The recorded audio stays in PSRAM after a failure, so a failed
  transcription retries without speaking again.
- A failed save retries from the text, so the upload does not repeat.
- A failed cleanup saves the raw transcript rather than discarding the note,
  and says so in the caption. It is not a failed note, so it has no retry
  stage of its own.
- The last saved note is kept in NVS, so it is still on screen after a reboot
  or a wake from deep sleep.

## What e-ink makes you design differently

- The image survives with the power off, which is the whole reason the device
  works as a sticky note. Render what the user should see, then sleep.
- Every repaint costs about a second and some power, so the idle loop
  repaints only when something visible actually changed: the Wi-Fi state, the
  radio-off state, a five percent step of battery, the charge direction, or
  USB presence. One consolidated check owns that decision, so two subsystems
  cannot each decide to paint.
- Partial refreshes accumulate ghosting, so `refresh_partial()` promotes every
  twentieth to a full one, and the screens the user sits and reads ask for a
  full refresh outright.
- There are no animations, spinners or progress bars. A changing word is the
  progress indicator.

## The setup page

One HTML page, `main/portal/index.html`, with no framework and no external
requests. It is served as a captive portal on the device's own hotspot during
setup and on the LAN address afterwards. Decisions worth keeping:

- Each service has a Test button that makes a real request and reports the
  real error, rather than validating the shape of a URL.
- Those buttons are disabled while the page is served from the hotspot, where
  the device has no internet, and the page says why instead of leaving them
  dead.
- Saved secrets are never sent back to the browser. A blank secret field
  means keep the stored one, and the placeholder says so.
- Saving settings redraws the device screen immediately, so a change such as
  text size is visible at once rather than at the next capture. The redraw is
  skipped in captive mode, where the setup instructions own the panel and
  there is no note behind them.
