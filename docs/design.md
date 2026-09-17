# Design

What the device looks like, how it behaves, and why each of those went the
way it did. Read it before changing anything the user sees or hears, so that
a deliberate choice is not undone by accident. The mechanism behind these
decisions is in `docs/architecture.md`; this file is the intent.

## The governing principle

Single purpose. In Madelena's words: "I definitely want this to be a
single-purpose device that I can simply pick up, speak, and remember."

There are no menus, no modes and no second screen. The GT911 touch
controller sat unused for most of this project's life for exactly that
reason, and it is now asked to do one thing: scroll a note too long to fit.
It is powered down the rest of the time, and `main/board/touch.cpp` is what
enforces that. Followup, the comparable community firmware for this board,
was explicitly rejected as a model because "the UX is already too
complicated."

Any proposal that adds a mode, a menu or a second screen is working against
this. That includes the tempting ones, and a live touch panel makes more of
them tempting than before.

## The screen

One screen, 800x480, one bit per pixel. Top to bottom: the note body, an
optional single caption line, then the status band along the foot. The
constants are at the top of `main/ui/screen.cpp`.

| Band | Geometry | Carries |
| --- | --- | --- |
| Body | y 28 to 20 down to y 406, the top depending on the face, the bottom on the caption | The note, wrapped, and a position bar in the right margin when it overflows |
| Caption | One 22 px line at y 375, hidden when empty | A failure reason, or a note that the raw text was saved |
| Status | One 30 px line at y 418, clearing the bottom edge by 24 px, no rule over it | The state, left, as a word in bold 30 px or nothing at all. The marks, right, packed against the margin: the cell in a 52 px box, the rest in 34 px ones |

Margins are 36 px on the top, left and right alike, and the note keeps 12 px
clear of the bar below it. The head margin is measured to the ink and not to
the glyph box, so the y the note is drawn at moves with the face, from 27 for
the smallest to 17 for the largest; `docs/hardware.md` has the arithmetic.

The bar clears the bottom edge by 24 px, two thirds of the note's margin,
because it is chrome and should sit tighter than the thing it describes. It
was 18 px, which put the marks closer to the edge than the note ever comes to
the sides and read as though the bar were falling off the page. 24 px is as
far as it can rise: the 52 px face gives up its fifth line at 25, and
`docs/hardware.md` has that arithmetic. It has no height of its own:
`bar_top()` places the one line of 30 px bold by that bottom margin and
everything else in the bar centers on it. The band it used to be was a
leftover from the rule that closed it, and sizing one cost the note 7 px for
nothing. While recording, the right side of the status band is replaced by a level
meter. A caption costs the body one line by pulling `body_bottom()` up; an
empty caption gives the space straight back.

The meter is twenty cells, each 5 px by 26 px with its corners clipped, filled
from the left. It is not one bar that fills: a bar reads as progress towards
something, and a recording is not going anywhere. Anything audible lights the
first cell, so hearing you faintly never looks like hearing nothing.

It shows the loudest 32 ms block of the last second, logarithmically, between
the room's own noise floor and the loudest the room has lately been. Both ends
move, and `main/audio/clip.cpp` re-measures them for every recording. Three
attempts at fixed ends failed, each fitted to one voice at one distance in one
room: this microphone reads a whisper at 139 and ordinary speech at 466, which
are both indistinguishable from nothing against a full scale of 32768. The
silence detector takes its thresholds from the same floor, so the bar and the
cuts cannot disagree about what the room sounds like.

The band sits on the bottom edge so the note starts at the top of the page,
where the eye already is. The caption stays beside the band rather than beside
the note, because a failure reason belongs with the failure title that names
it.

The rule that used to close the status band was dropped along with the words
in it. With the band down to three small marks there is nothing left to fence
off, and the note runs clean to the edge of the page.

A footer band used to sit at the bottom carrying the firmware version and the
IP address. It was dropped in `aff9c7d`: both already live on the info
screen, and the space is worth more as note text. The version is now stamped
at the foot of every titled page, so it appears on the info screen and
nowhere in normal use. The IP address is left to whichever caller wants it,
which is why that function does not repeat it.

The note body moves one screen at a time, never with the finger. E-ink cannot
animate, so text dragged under a fingertip would be a sequence of full-page
repaints at about a second each. A swipe therefore commits to a whole screen
of travel and costs one partial refresh, and it keeps a line of overlap so
the reader does not lose their place. The 8 px bar in the right margin is the
only cue that there is more note below, so it is drawn whenever there is.

## The status band is pictures, not words

The note is the point of the screen, so everything above it that is not the
note competes with it. The band is therefore drawn rather than written, and
only the states that need naming keep a word.

| Shows | Material Symbols glyph |
| --- | --- |
| Sleeping | `bedtime` |
| Wi-Fi connected | `wifi` |
| Wi-Fi stopped | `wifi_off` |
| Wi-Fi lost | `signal_wifi_bad` |
| Charge flowing in | `bolt` |
| Battery, seven fills | `battery_android_frame_1` to `_6` and `_full` |
| Battery under 10 percent | `battery_android_alert` |
| Gauge did not answer | `battery_android_question` |

Ready has no mark at all. It is the state the device is in almost all the
time, and the cleanest thing a status band can say about a device that is
simply waiting is nothing. Every other state is worth reading and stays as
text in the same 30 px bold, so the band is empty exactly when there is
nothing to report:

- Working: Connecting Wi-Fi, Listening M:SS, Transcribing, Transcribing n/N,
  Cleaning up, Saving.
- Done: Saved HH:MM, which is a receipt and stays until the next thing
  happens, so the band is rarely blank in normal use.
- Nothing to save: No speech detected.
- Failed, each with a reason in the caption and "Press Down to retry.":
  No Wi-Fi, Transcribe failed, Save failed, Microphone error.
- Waiting on the user: Retry with Down.

Sleeping keeps its moon rather than a word, because that screen outlives the
power being cut and has to say by itself that the device is off rather than
frozen. The moon sits at the left of the right-hand cluster and not at the
left of the bar, which is kept clear for whatever the status has to say.

The marks are Material Symbols rather than shapes drawn by hand, because
Google already solved legibility at this size and a set that was designed
together reads as a set. `tools/gen_icons.py` bakes them to 1-bit at 34 px,
outlined rather than filled and at weight 500: a panel with no anti-aliasing has nothing to soften a hairline
with, and 400 breaks up under the threshold.

Sleeping is a crescent moon and not a sleeping face, which is what the design
started from. Material has no sleeping face, `sentiment_calm` reads as calm
rather than asleep, and the moon is the one mark here that is unmistakable
from across a room. That last point is the argument: this is a device on a
fridge. `mood` was baked for Ready before Ready lost its mark, and was dropped
from `tools/gen_icons.py` with it rather than left in the binary.

There is no mark for a cable on its own. A `usb` trident sat beside the bolt
for a while, and it had to go: USB is the only way to charge this board, so
next to a bolt it said the same thing twice, and on its own it could not say
the thing it looked like it was saying. The only fact behind it is VBUS on
`PIN_EXTERNAL_POWER`, which a dumb charger raises exactly as a computer does,
and the pins that could tell those apart are the microphone's.
`docs/hardware.md` has the detail.

What is left is one mark for the one fact worth showing: the bolt, whenever
charge is flowing into the pack. A full battery on a lead shows nothing, which
is honest, because a full battery on a lead is doing nothing. `battery::on_usb()`
came out of the idle repaint check with the mark, so plugging into a full pack
no longer costs a refresh for a screen that would not change.

The cluster is packed against the right margin rather than laid into fixed
slots, so the row closes up when the cable comes out instead of leaving a hole
where the power mark was. `icons::draw_power()` returns whether it drew, which
is what lets `draw_status()` place the next mark without repeating the rule
for when there is one at all. Right to left the order is battery, power,
aerial, moon, so the reading that changes most often keeps the margin.

The cell shows no number. It uses the whole `battery_android_frame` family,
which is a seven step fill, and lands on the nearest of them, so each drawing
covers 14 points of charge:

| Percent | Icon |
| --- | --- |
| 93 to 100 | `_full` |
| 79 to 92 | `_6` |
| 65 to 78 | `_5` |
| 50 to 64 | `_4` |
| 36 to 49 | `_3` |
| 22 to 35 | `_2` |
| 10 to 21 | `_1` |
| under 10 | `_alert` |

`battery_step()` is the scaling `(percent * 7 + 50) / 100`, which is the
nearest seventh with halves rounded up, rather than a ladder of thresholds
that would have to be kept in step with the family by hand. The warning mark
takes everything under 10 rather than sharing a step with `_1`.

It used to be four quarters taken from every other member, because adjacent
fills differ by about 4 px and are hard to tell apart across a room. The
finer scale was chosen anyway: a gauge that moves is worth more than one you
can read exactly, and the exact figure is on the info screen for when you want
it. A precise reading is a thing you go and look up, not a thing you glance
at.

The two marks that are not a reading, `_alert` and `_question`, come from the
plain `battery_android` family rather than the frame one. `frame_alert` and
`frame_question` draw a cell that looks full with the mark outside it, so a
flat battery would read as a charged one. The plain pair draw an empty cell
with the mark inside, which is what those states mean.

The cell is baked at 52 px against 34 px for everything else, both because its
fill is a reading where the others are a yes or no, and because the frame's
stroke only lands on whole pixels at certain sizes. Between 44 and 48 the
horizontal strokes rasterize a pixel thicker than the vertical ones and the
outline reads as lopsided; 50 to 54 is the nearest even band.

`icons::battery_step()` is both the drawing and the repaint trigger, so the
panel cannot paint for a change too small to see. A full discharge now crosses
seven thresholds rather than four, still well under the twenty it crossed when
the bar carried a number.

Every icon is centered on its own ink, which `tools/gen_icons.py` gets by
cropping each glyph to its bounds and letting `blit()` halve the result. It
used to center the em squares instead, on the reasoning that a family's
members would then never shift against each other. That was the wrong thing
to protect: the squares are all the same but the ink inside them is not, so
the marks sat up to 2.5 px apart on a line where they are seen together,
to spare the three Wi-Fi states a 1 px shift between states that are never
seen at once. Centering the ink puts the whole cluster within half a pixel,
which is the rounding of an odd height against an even one and nothing more.

## Text sizes

Four faces, selectable as `text_size` on the settings page, plus `auto`.

| Setting | Face in code | Called | Cap height | Box | Lines per screen |
| --- | --- | --- | --- | --- | --- |
| small | `font::body()` | 30 px | 22 px | 38 px | 9 |
| medium | `font::large()` | 40 px | 29 px | 50 px | 7 |
| large | `font::xlarge()` | 52 px | 38 px | 66 px | 5 |
| xlarge | `font::xxlarge()` | 64 px | 47 px | 82 px | 4 |

The px name in the third column is what the rest of these docs call each face
and is Inter's nominal size, kept because it is the established vocabulary.
The fixed quantities are the last three columns; the nominal size a family is
actually baked at varies, and is in each generated header's banner.

The settings page names each option by its line count alone, not by a pixel
size. Lines per screen is the thing the reader is actually choosing, and a
pixel size now means nothing to them: it is neither what any family is baked
at nor what the letters measure. Do not put it back.

`auto` is the default. It wraps the note at each face from largest down and
keeps the first that shows the whole thing at once, so a four-word note fills
the screen and a paragraph stays readable. A note too long even for the 30 px
face lands there and scrolls. It costs up to four wraps of the same text,
which is arithmetic over glyph widths and nowhere near the cost of the
refresh that follows. `layout_note()` in `main/ui/screen.cpp` is the whole of
it.

Auto is the better default for a device you glance at, because the thing that
should set the text size is the note, not a setting chosen once for an
average note that does not exist. The four fixed sizes stay for anyone whose
eyes want a floor or a ceiling.

Madelena revised the scale twice while using the device, and the second
revision is why the first two columns do not line up. A 22 px face was
originally the smallest choice and was dropped as "impractically small" for a
device read at arm's length on a fridge or a desk. Every label then shifted
down one step, so what had been Medium became Small, while the internal face
names stayed where they were. The face named `large` is the setting named
`medium`.
`note_face()` in `main/ui/screen.cpp` is the whole mapping. Do not rename
either side to make them agree: the setting strings are stored in NVS and the
face names are in the generated headers.

The default is medium, which puts the same physical size on screen as the
previous default did under its old name. The 22 px face itself was not
removed, it is still the caption and status-band face.

Line pitch is a fixed number per face in the `FACES` table of
`tools/gen_font.py`, baked into each face's `line_height`. It started as 1.10
times the glyph box, tightened from 1.15 to fit more lines, and was frozen at
those values when the families became switchable. The
lines-per-page arithmetic charges the last line only its glyph box rather
than a full pitch, which is what fits a fifth 52 px line into the band; the
formula and the per-face numbers are in `docs/hardware.md`.

### The Latin family is a setting, and every family is the same size

Five families, chosen as `text_font` on the settings page, all OFL:

| Setting | Family | Why |
| --- | --- | --- |
| `inter` | Inter | The default. A neo-grotesque, and the crispest at the 22 px face once the rasterizer has thresholded the anti-aliasing away. |
| `atkinson` | Atkinson Hyperlegible | Drawn by the Braille Institute for low vision. Wide apertures, a slashed zero, the original choice here. |
| `opensans` | Open Sans | The humanist sans. Warmer and more open than Inter without giving up much width. |
| `literata` | Literata | The serif, drawn by TypeTogether for e-readers, so its strokes stay even at 1 bit. |
| `shantell` | Shantell Sans | The handwriting face, for a device that is a sticky note. |

Source Serif 4 was baked and then dropped: next to Literata it read as the
same idea done slightly lighter, and one serif is enough.

Inter replaced Atkinson as the default because Atkinson's letterforms, and its
slashed zero in particular, read as clinical in prose. Atkinson stays because
its legibility is the point for anyone who needs it.

Nominal point size is the wrong knob and is not used. The same number means
something different in each family: at 30 px Literata's glyph box is 46 px
against Inter's 38 px, yet both put a 22 px capital on the screen, and the
capital is what the eye reads as size. So `tools/gen_font.py` is given a
target cap height and solves for the point size that hits it, per family and
per weight. Atkinson came out visibly small for years because it shared a
nominal number with a face whose capitals are taller.

Every family also bakes into the same fixed glyph box, baseline row and line
pitch, listed in that tool's `FACES` table. That is what keeps the
lines-per-screen column above true for all five: `Font::height` and
`Font::line_height` no longer vary, so `fitting_lines()` and `auto` return the
same answer whichever family is active. The boxes are the derived minimum that
holds every family's ink, so adding a sixth may require widening one, and the
generator refuses to emit a clipped glyph rather than shipping one. The order
of `Family` in `main/ui/font.h` and of `kFamilies` in `main/ui/font.cpp` must
match, and `kFonts` in `main/app/settings.cpp` holds the accepted names.

Shantell is pinned at `BNCE 45` and `INFM 25`, so its glyphs sit at slightly
different heights and the line looks written rather than set. The bounce is
what drove the 52 px and 64 px boxes one pixel wider than the other families
needed, because a bounced descender hangs lower.

All five are baked to 1-bit bitmaps at build time, ASCII only, U+0020 to
U+007E, at about 120 KB per family. Everything above U+007F ignores the
setting and is rasterized at draw time from a Noto Sans TC subset in the
`font` flash partition, so Chinese notes are readable on screen and not merely
correct in the vault. The partition holds one font and no more, so the Latin
choice is deliberately independent of it rather than paired with it. Without
that partition `text::prepare()` folds those code points to ASCII
approximations rather than failing.

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
| `cue_latched()` | One short 2600 Hz blip | The tap latched, the hand can come off |
| `cue_stop()` | One 2000 Hz tone | Recording has stopped |
| `cue_saved()` | High double, 2800 Hz twice | The note is in the vault |
| `cue_error()` | One low 900 Hz tone, 250 ms | A stage failed |

The `beep` setting switches all five off, and `buzzer::beep()` still spends
the duration as a delay when it is off, so timing does not change with the
sound.

`cue_latched()` is the one cue that exists purely to answer a question the
screen cannot answer fast enough. A tap and a short hold begin identically,
and the screen only redraws every five seconds once latched, so without a
sound there is no way to tell a recording that is still running from one that
stopped the moment the finger lifted.

On screen the state progresses through plain verbs: "Listening 0:07" with a
level meter, then "Transcribing", then "Cleaning up" when that is enabled,
then "Saved 14:32". Nouns and jargon are avoided.

A clip with nothing spoken in it ends at "No speech detected", keeping the
previous note on screen. It stops before the cleanup model, because an empty
transcript is a blank prompt and a chat model answers a blank prompt by
introducing itself; the device once saved "I'm ready to help!" as a note.
`has_speech()` in `main/app/pipeline.cpp` is the guard, and it counts any
non-ASCII byte as speech so a Chinese note is never mistaken for silence.

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

Three buttons and one gesture, no chords.

| Input | Action |
| --- | --- |
| Hold the side button | Record. Recording starts on the press itself, not after a long-press threshold, so the microphone is live with no delay. The first fifth of a second is dropped rather than kept, because that is the start cue sounding and the microphone hears it. |
| Release the side button | Stop and run the pipeline, when the press lasted past 500 ms. A clip under 300 ms is discarded as a fumble. |
| Tap the side button | Latch. Released inside 500 ms the recording keeps running with no hand on it, until the button is tapped again. |
| Tap it again | Stop and run the pipeline. Within 1.2 s of the first tap it discards instead, which is how a fumble is taken back. |
| Hold Up 3 s while latched | Abandon the recording. Nothing is saved. |
| Swipe up on the glass | Show the next screen of a long note. |
| Swipe down on the glass | Show the previous screen. |
| Press Up | Scroll back through a long note. At the top it opens the info screen instead. |
| Hold Up 3 s | Power off. The panel keeps a "Powered off" page telling you to hold the side button to come back. |
| Press Down | Scroll on. After a failure it retries the failed stage instead. |
| Hold Down 3 s | Enter setup mode, or restart when already in it. |

The side button carries both gestures rather than a setting choosing between
them, because a setting would mean picking one and living with it, and the
two suit different notes: a sentence is a hold, a paragraph is a tap. Nothing
has to be configured and nothing has to be learned to keep the old behaviour.

The 500 ms threshold is measured from the button event, not from the start of
the record loop, which begins a good quarter second later behind the
microphone settling and the start cue. Measuring from the loop would read a
700 ms hold as a tap.

Cancelling is the hold on Up rather than the click, because the click is how
the previous note is scrolled and someone reaching for that must not throw
away what is being recorded. Power off is unreachable during a recording
anyway, so the hold is free to mean this.

A latched recording stops itself after ten minutes. Nothing physical ends
one, so without a ceiling a press in a pocket would record and upload until
the battery was flat. A held recording needs no ceiling because the hand is
the limit.

Up and Down were meant to stop scrolling once the swipe took over, freeing
them for something else. They still scroll, because no unit has yet been seen
reporting a touch: the GT911 in every one tested comes up with no
configuration loaded and never reports a coordinate. `docs/hardware.md` has
the evidence. Until one does, removing the buttons would leave a long note
with no way to scroll at all, so the HOTFIX in `main/app/pipeline.cpp` keeps
them and names the condition for taking them out.

Power off lives on Up for a hardware reason, not a design one: the side
button cannot carry a long press at all, because holding it is how recording
works. `record_loop()` reads the raw GPIO level rather than the event queue
for exactly that reason, so a release registers even while a panel refresh
has the pipeline task blocked. `docs/hardware.md` has the full account.

### The swipe

Implemented and unverified. Everything below is the intended design, and the
gesture has never run on a working controller.

The panel is powered only while a swipe would do something: a note that
overflows, showing on the screen that scrolls. A note that fits leaves the
controller unpowered, which is most notes under `auto`. That is a power
decision first, since the controller draws milliamps and this device is meant
to sit on a fridge, but it is also the honest one: an input that cannot act
should not be listening.

A gesture has to travel an eighth of the screen and be more vertical than
horizontal before it counts. The device is picked up by its glass, and a grab
must not scroll the note out from under the person holding it.

The text should follow the finger, as it does everywhere else: swiping up
brings up what was below the last visible line. `kInvertY` in
`main/board/touch.cpp` is the one constant that decides this, because the
controller's own axis does not have to agree with the way the canvas reaches
the glass. Which way it belongs is a guess until a coordinate arrives.

### Switching on

Holding the side button is how the device is switched on, which means the
button is still down when the firmware starts. That press is deliberately
thrown away: the device comes up ready and records only when the button is
pressed again. Recording something nobody meant to say, on the way to a
vault, is the worse failure.

Waking from deep sleep is the exception, and the reason the distinction
exists. There the hold *is* the recording, so the microphone is live
immediately and "pick up and speak" survives the idle timeout. `pipeline.cpp`
tells the two apart by the wake cause, not by the button.

A wake recording cannot latch, and `record_and_process()` is told so. The
button has been held right through boot, so the release that follows says
nothing about how long it was down, and every wake press would latch a
recording nobody asked for.

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

The facts are a borderless two column table: Wi-Fi, IP address, Battery and
Saving to, with one column set by the widest label so the values line up
without a rule to carry the eye across. Below it, after a gap, the
instructions are one short paragraph each rather than a sentence run together,
because they are three separate things a person might want to do.

The settings address is the last of those paragraphs rather than a
parenthesis in the IP row. A table cell holding an instruction is what made
the screen look untidy, and the row it was in now holds nothing but the
address. It offers the hostname, not the IP, because that is the name worth
typing; the IP is still in the table right above it for a router that does not
serve DHCP names in local DNS.

There is no rule under the heading. The heading is 40 px bold against 30 px
regular below it, and a step that size separates the two without a line. The
version stamp at the foot takes the same 24 px bottom margin as the status bar
on the note page, being the same thing: one line of chrome on the floor.

Keep the paragraphs in their order. The page truncates at the bottom to
protect the stamp, and only one value can realistically wrap, the folder in
Saving to, which costs the last paragraph. That is the settings address,
whose IP is in the table two lines above it, so the page loses the line it
can most afford. Putting either Hold instruction last would lose a recovery
step instead.

Its heading is the `device_name` setting, which is also the hostname the
device gives the router, so a household with two of these can tell them apart
on the shelf and in the client list. On a router that registers DHCP names in
local DNS this makes the settings page reachable at http://thename/ rather
than an address nobody remembers, which is worth more than it sounds for a
device whose IP is otherwise only visible on its own info screen.

The footer is deliberately not renamed with it: it reads
"Obsidian Sticky" and the version, because a screen should always be able to
say what is running on it, and a device called "Fridge" that cannot name its
own firmware is no use in a bug report. `draw_stamp()` in
`main/ui/screen.cpp` stamps every full-screen page that way, including the
powered-off one.

## Wording

Small choices that were made deliberately.

- The icons are packed against the right margin, so a state with nothing to
  say takes up no room. The cell is the anchor and never moves.
- The bolt means charge is actually flowing into the pack and the plug means
  a cable is attached but the pack is full. Conflating those was a bug;
  `docs/hardware.md` explains why the gauge's discharge bit cannot tell them
  apart.
- A failure puts the stage in the title and the reason plus "Press Down to
  retry." in the caption, so the screen says both what broke and what to do.

## The transcript arrives while you are still talking

The note is still drawn once *after* it is saved, but during a recording the
body now fills in as each segment comes back, a few seconds behind your voice.
That is the point of segmenting: without it the feature is invisible and the
device looks like it is doing nothing for minutes at a time.

This does not cost a refresh. The level meter already forces a partial refresh
every second while recording, so the transcript rides one that was happening
anyway, and `set_note(text, tail)` pins the view to the newest words rather
than the top. The rule below about drawing the note once still holds, because
it was always about not *adding* refreshes to the path between releasing the
button and reading the note.

Only the last 1500 bytes are handed to the screen. Nothing else could be seen,
and `layout_note()` wraps the whole note once per face to size it, which on a
ten minute recording would mean re-wrapping thousands of characters four times
a second on the task that also runs the silence detector.

What is on screen mid-recording is the raw transcript. Cleanup runs once at
the end, over the whole note, so the text visibly tidies itself when the note
is saved. Abandoning a recording puts the previous note back, and so does "No
speech detected", because neither of those saved anything.

## Failure is part of the design

Nothing a person said out loud should ever be lost to a transient error.

- Transcribed text is kept, and audio that has not yet been transcribed stays
  in PSRAM, so a failed transcription retries only the part that failed.
  Audio that has already become text is released, which is what lets a
  recording run past ninety seconds at all.
- A failed save retries from the text, so the upload does not repeat.
- A failed cleanup saves the raw transcript rather than discarding the note,
  and says so in the caption. It is not a failed note, so it has no retry
  stage of its own.
- A segment that fails twice stops holding the note hostage. The second time
  through the retry, whatever did transcribe is saved and the caption says
  how many parts are missing. Withholding nine good minutes over one bad
  segment is the worse failure, and a ten-minute recording makes it possible
  in a way a ninety-second one never did.
- The last saved note is kept in NVS, so it is still on screen after a reboot
  or a wake from deep sleep. NVS caps a string near 4 KB, so a long note is
  cut at a code point boundary for that copy alone; the vault has all of it.

Segments are released strictly oldest first, because a ring can only free
from its tail. That is why a failed segment stops the uploader rather than
letting it carry on: nothing behind the failure could be freed anyway, and
the backlog absorbs the rest of the recording until the drain retries it.

## What e-ink makes you design differently

- The image survives with the power off, which is the whole reason the device
  works as a sticky note. Render what the user should see, then sleep.
- Every repaint costs about a second and some power, so the idle loop
  repaints only when something visible actually changed: the Wi-Fi state, the
  radio-off state, a step of the battery icon, the charge direction, or USB
  presence. One consolidated check owns that decision, so two subsystems
  cannot each decide to paint.
- Dropping the battery percentage bought most of an idle device's refreshes
  back. A full discharge used to cross twenty 5 percent buckets and now
  crosses the gauge icon's seven, so the battery asks for around a third of
  the repaints it did when the bar carried a number.
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
