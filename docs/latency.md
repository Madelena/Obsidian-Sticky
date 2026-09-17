# Latency

Where the seconds go between releasing the side button and reading the saved
note, how every number was taken, and what is left to win. Figures are
measured on this device over serial unless they are labelled an estimate.

## How to measure it again

The firmware instruments itself, so a serial capture is the whole method.

- `main/net/http.cpp`, in `exchange()`, logs one line per HTTP round trip:
  `hop N: connect A ms, upload B ms (C bytes, D kB/s), answer E ms`.
  - `connect` is DNS plus TCP plus the TLS handshake.
  - `upload` is the request body going out.
  - `answer` is the wait from the last byte sent to the first byte of the
    reply, which is the server thinking.
  - A redirect is a second hop with its own line, so the extra cost is visible
    rather than folded into the first.
- `main/net/http.cpp`, in `warm_one()`, logs `warm HOST: N ms, holding` for
  each connection opened during recording.
- `main/ui/display.cpp`, in `push()`, logs `full refresh took N ms` and
  `partial refresh took N ms`, timed around the blocking panel call.
- `main/app/pipeline.cpp` logs `Recorded N ms` when the button comes up.

Open COM3 at 115200 with any serial reader, record one note, and grep the log
for `hop`, `refresh took`, `warm` and `Recorded`. Opening the port resets the
device, so a capture always begins at the boot banner and the thing you want
to observe has to happen after the port is open. `docs/developing.md` has the
rest of the serial traps.

Conditions behind every number below: 2.4 GHz WPA2 at about -50 dBm, Groq for
transcription, Anthropic for cleanup, Obsidian's Local REST API on the LAN.

## Measured costs

| Thing | Measured |
| --- | --- |
| Full e-ink refresh | 1936 to 1961 ms, blocking |
| Partial e-ink refresh | 874 to 948 ms, blocking |
| TLS handshake to api.groq.com | 1771, 1803, 2110, 2119, 2240 ms |
| TLS handshake to api.anthropic.com | 1741, 1917, 1970 ms |
| Reused warm connection | 2 to 3 ms |
| Upload throughput over TLS, fresh socket | 100, 122, 124, 132, 139, 204, 205 kB/s across runs |
| Upload throughput over TLS, socket reused after a POST | 29, 38 kB/s |
| Groq trivial GET /models reply | 289 to 594 ms |
| Groq transcription reply, 12 to 13 s of audio | 1092 to 1662 ms |
| Anthropic messages reply | 911 to 943 ms |
| Obsidian on the LAN, per hop | connect 6 to 45 ms, answer 47 to 132 ms |
| Obsidian 307 redirect, the extra hop | about 120 ms |
| Raw audio on the wire | exactly 32000 bytes per second |

The audio rate is arithmetic, not a measurement: `main/audio/clip.cpp` writes
16 kHz mono 16-bit PCM into a 44-byte WAV header, so a clip costs 32000 bytes
for every second held.

## Measure it, do not estimate it

A research pass done before any capture estimated upload throughput at about
50 kB/s and concluded that compressing the audio was the top priority. The
measurement said 100 to 204 kB/s, two to four times better, and put the
payload at only 13 percent of the end to end budget. The panel, which the
estimate had not looked at, was 46 percent.

Acting on the estimate would have started with a codec change carrying format
risk, provider-acceptance risk and real work, and would have bought back a
seventh of the time that drawing the screen once instead of three times bought
back for a day of edits. Measure first. This document exists so nobody has to
take that on faith a second time.

## The original budget

Release of the button to the final screen, 14675 ms total.

| Part | Cost | Share |
| --- | --- | --- |
| Panel refreshes | 6726 ms | 46 percent |
| TLS handshakes | 3512 ms | 24 percent |
| Server thinking | 2524 ms | 17 percent |
| Payload on the wire | 1913 ms | 13 percent |

## The three changes

Made in this order, each measured before the next.

1. **One full refresh per note instead of three.** The old flow drew the raw
   transcript, then the cleaned text, then the saved note. Each was a blocking
   1.95 s full refresh and two of them were replaced moments later by the
   next. `process()` in `main/app/pipeline.cpp` now draws cheap status lines
   ("Transcribing", "Cleaning up") for feedback and paints the note exactly
   once, after it has been saved.
2. **Warm the TLS connections during recording.** Both handshakes depend on
   nothing the microphone produces, so `record_and_process()` fires
   `http::warm_async()` as soon as the button goes down. Connect fell from
   1771 ms to 3 ms on the Groq host and from 1741 ms to 2 ms on the Anthropic
   host.
3. **Draw on a render task.** The panel is on SPI2 and the network is on the
   radio; they were serialized only because one task did both. The caller now
   rotates the canvas, hands the waveform to the `render` task, and returns to
   the network while the panel settles.

## Results

Same style of note each time.

| Build | Release to saved | Release to final screen |
| --- | --- | --- |
| Original | 12.9 s | 14.9 s |
| After 1 and 2 | 8.1 s | 10.1 s |
| After 3 | 6.9 s | 8.9 s |

## What is left

Of the 6.9 s from release to saved:

| Part | Cost | Share |
| --- | --- | --- |
| Uploading the audio | 3870 ms | 57 percent |
| Groq transcribing | 1092 ms | 16 percent |
| Claude cleanup | 921 ms | 14 percent |
| Waiting out an in-flight meter refresh | 667 ms | 10 percent |
| Obsidian save | 190 ms | 3 percent |
| TLS handshakes | 12 ms | 0 percent |

The meter line is the cost of the partial refresh that was already running
when the button was released: the first draw after that has to wait for the
shared rotation buffer.

## Why segment uploads do not reuse a socket

Handing a socket back after a POST, so a recording's segments all ride one
connection, looks like free money: the handshake is 1.8 to 2.2 s and a reused
connection connects in 3 ms. It was built, and it made every note slower.

| Build | Payload | Upload | Rate |
| --- | --- | --- | --- |
| Before segmenting, fresh socket | 1983909 B | 14933 ms | 132 kB/s |
| Segments, socket reused after a POST | 756133 B | 25503 ms | 29 kB/s |
| Segments, socket reused after a POST | 719269 B | 18621 ms | 38 kB/s |
| Segments, fresh socket each time | 771493 B | 5541 ms | 139 kB/s |
| Segments, fresh socket each time | 782757 B | 6377 ms | 122 kB/s |
| Segments, fresh socket each time | 120229 B | 586 ms | 205 kB/s |

A reused socket uploads at roughly a third the rate. The handshake it saves is
about 2 s; the throughput it costs is around 20 s on a segment this size. The
122 kB/s row paid a full 1953 ms handshake and still finished three times
sooner than the 38 kB/s row that connected in 3 ms.

The mechanism is not known, and guessing at one would be worse than saying so.
Two explanations were ruled out. Slow start is not it: solving for a fixed
startup cost across the 132 and 38 kB/s rows gives a negative upload time, so
the rate itself differs rather than the ramp. Recording load is not it either,
since the 139 kB/s row went out while the capture task was running and the
panel was refreshing every second.

Two traps this hid behind, worth knowing before anyone tries it again:

- The slow rows are ordinary-looking. `connect 3 ms` and `HTTP 200` are what a
  working warm connection looks like, and only the kB/s column says otherwise.
- At 29 kB/s the upload is slower than the 32 kB/s the microphone produces, so
  segments can never get ahead and the whole feature silently does nothing.
  That reads as "chunking does not work" rather than "the socket is slow", and
  it very nearly bought a codec nobody needed.

## What segmenting changed

The table above describes a serial pipeline that no longer exists for
anything longer than one segment. The audio is now cut at pauses and each
piece is transcribed while the user is still talking, so at release the only
audio still to send is the tail since the last cut, and most of the Groq
thinking time has already been spent.

That takes the top two rows out of the post-release budget for a long note
and leaves the tail upload in their place, bounded by the segment length
rather than the recording length. A ninety-second note used to send 2.88 MB
after the button came up; it now sends whatever is left of the last segment,
which is at most about 12 seconds of audio and usually half that.

Measured on a 52 second note against a 62 second one on the previous build,
both with cleanup on:

| Build | Note | Release to saved |
| --- | --- | --- |
| Before segmenting | 62 s | 17.8 s |
| Segmenting, fresh socket per segment | 52 s | 11.0 s |

About 5 s of that 11 was waiting out a segment still in flight, because the
button came up 3.5 s after a cut. That is not wasted work, it is transcription
that had to happen anyway. The win that matters is the shape rather than the
size: the old cost grows with the length of the recording, and this one is
bounded by roughly one segment plus the tail however long you talk. A five
minute note is about 73 s of upload on the old build and about 15 s on this
one.

The every-twentieth-partial promotion to a full refresh is also suppressed
while recording, which removes about fifteen two-second panel stalls from a
five minute recording. The meter still updates every second.

## Remaining levers, honestly costed

1. **Compress the audio.** Still the biggest remaining win on a short note,
   and still the most expensive to take: format risk on both providers and
   real encoder work. It matters much less for a long one now that the upload
   overlaps the speaking. The investigation and why it was deferred are in
   `docs/architecture.md` under "Codecs".
2. **Make the final draw partial instead of full.** About 1.1 s, at the cost
   of ghosting on the one screen the user actually sits and reads. Not an
   obvious trade.
3. **Cut the first segment sooner.** A recording shorter than the fifteen
   second minimum is still one upload after release, exactly as before, so
   the shortest notes saw none of the gain above. Lowering the floor would
   cost Groq's ten-second minimum billing on every piece.
