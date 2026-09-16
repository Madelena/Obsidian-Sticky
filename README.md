# Obsidian Sticky

Voice notes to your Obsidian vault, from a Seeed Studio reTerminal Sticky.

> **Disclaimer.** Every line of code in this repository was written by Claude
> (Claude Fable 5.1, with Claude Opus doing the compile-fix loop and the docs),
> directed by a person who does not read C++ and cannot vouch for what it
> does. It runs on a real device on my desk and has saved real notes, but
> treat it as a hobby experiment, not a product. It drives the board's power
> latch, the e-ink panel, and deep sleep directly, and it sends your audio to
> whichever API you configure. Flash it at your own risk. I accept no
> liability for bricked hardware, lost notes, surprise API bills, or anything
> else it does or fails to do. If something looks wrong in the code, it
> probably is; please open an issue.

## What it does

Pick the device up, hold the side button, say what you are thinking, and let
go. The Sticky records from its built-in microphone, uploads the clip to a
speech-to-text service (Groq Whisper by default), optionally runs the
transcript through a small language model to fix punctuation and obvious
mis-hearings, and writes the result straight into your Obsidian vault through
the Local REST API community plugin. The note lands as a timestamped line in
today's daily note, or as a brand new note in a folder you choose. The e-ink
screen shows the text it saved and stays readable with the power off, so the
device works as a sticky note on your desk until you pick it up again.

## How to use it

| Action | What happens |
| --- | --- |
| Hold the side (AI) button | Starts recording. The screen shows a level meter and the elapsed time. |
| Release the side button | Stops recording and runs transcribe, optional cleanup, and save. |
| Press Up | Scrolls back through a note too long to fit. At the top it shows the info screen instead: Wi-Fi network, IP address, battery, where notes are being saved, and the firmware version. It stays up until you press a button, which returns you to the note. |
| Press Down | Scrolls on through a long note. After a failure it retries the failed stage instead: a failed transcribe retries from the audio, which is still in memory, and a failed save retries from the text, so nothing has to be spoken again. |
| Swipe up or down on the screen | Also scrolls a long note, on a device whose touch controller reports touches. See the note below. |
| Hold Down for 3 seconds | Enters setup mode. The device stops its normal work, starts the "Sticky-Setup" Wi-Fi network, and shows the setup instructions. Holding Down again in setup mode restarts the device. |
| Hold Up for 3 seconds | Powers off, leaving a "Powered off" page on the screen. Hold the side button to turn it back on. The side button cannot power off, because holding it is how you record. |

Recordings shorter than about a third of a second are discarded, and a single
recording stops at 90 seconds. Holding the button without speaking ends at
"No speech detected" and leaves the previous note alone, so nothing is saved.
Switching the device on does not start a recording, even though you switch it
on by holding the same button; press it again once the screen says Ready.

The status band tracks the work, and the note itself is drawn once, when it is
safely in your vault, because every redraw of this screen costs a second or
two. A note longer than the screen scrolls a screenful at a time, and the bar
in the right margin shows where you are.

Swipe scrolling is built but unproven. On every unit tested, the GT911 touch
controller comes up with no configuration loaded and never reports a touch,
so the buttons remain the way to scroll. `docs/hardware.md` records exactly
what was measured and what was ruled out.

After ten idle minutes the device goes into deep sleep and the screen keeps
showing the last note. Press the side button to wake it. If you keep the
button held down while it wakes, it starts recording as soon as the
microphone is up, so a wake-and-speak gesture works as one motion. The idle
timeout is configurable, and setting it to 0 disables sleep entirely.

## What you need

- A speech-to-text endpoint and key. Any OpenAI-compatible
  `/audio/transcriptions` endpoint works.
  - [Groq](https://console.groq.com/) with `whisper-large-v3-turbo` is the
    default and is the fastest of the three.
  - OpenAI with `whisper-1` or `gpt-4o-transcribe`.
  - A local Whisper server such as `whisper.cpp` or `faster-whisper-server`.
    A plain `http://` address is simplest. An `https://` address on a private
    host (`192.168.x.x`, `10.x.x.x`, `172.x.x.x`, `localhost`, `.local`,
    `.lan`) is accepted with a self-signed certificate; public hosts are
    verified against the built-in certificate bundle.
- Optionally, an [Anthropic](https://console.anthropic.com/) key for the
  cleanup pass, or any OpenAI-compatible chat-completions endpoint and key.
  Cleanup is off by default and the firmware saves the raw transcript if the
  cleanup call fails, so it never costs you a note.
- Obsidian, with the
  [Local REST API](https://github.com/coddingtonbear/obsidian-local-rest-api)
  community plugin installed and enabled.
  - In the plugin settings, enable the non-encrypted HTTP server and note the
    port, which is 27123 by default. Copy the API key from the same page.
  - For daily-note mode you also need the
    [Local REST API - Periodic Notes](https://github.com/coddingtonbear/obsidian-local-rest-api-periodic-notes)
    companion plugin, which is what provides the `/periodic/daily/` route,
    plus the Periodic Notes or Daily Notes plugin that actually creates the
    note. Without the companion plugin the device reports "Periodic Notes
    plugin missing".
- The computer running Obsidian has to be switched on, with Obsidian open,
  and on the same network as the Sticky. The device talks to your vault
  directly over the LAN. There is no cloud service in between.
- A 2.4 GHz Wi-Fi network. The ESP32-S3 has no 5 GHz radio.
- Optionally, the Chinese font partition, if you want notes in Chinese,
  Japanese or Korean to be readable on the screen. It is one extra build step
  and one extra flash, described in
  [Chinese and other scripts](#chinese-and-other-scripts) below. Without it
  the firmware still works and still saves those notes correctly to Obsidian;
  only the screen falls back to ASCII.

## First-time setup

### Install from your browser

If the firmware is not on your device yet, the quickest route is
[the web installer](https://madelena.github.io/Obsidian-Sticky/). No toolchain,
no downloads.

1. Plug the Sticky into your computer over USB-C, using the port your system
   lists as "CH343".
2. Open <https://madelena.github.io/Obsidian-Sticky/> in Chrome or Edge on a
   desktop and click **Install**. Other browsers cannot talk to USB devices.
3. Join the open Wi-Fi network named **Sticky-Setup**.
4. Open **http://192.168.4.1** and fill in the settings form.

The web install writes the CJK font partition too, so nothing extra is needed
for Chinese, Japanese or Korean. Every
[GitHub Release](https://github.com/Madelena/Obsidian-Sticky/releases) also
carries the individual `.bin` files and a merged image you can write at `0x0`
with `esptool` yourself.

### Setting the device up

1. Turn the device on by pressing the side button. With no Wi-Fi configured
   it goes straight into setup mode.
2. On your phone or laptop, join the open Wi-Fi network named
   **Sticky-Setup**. Most devices will pop the captive-portal page open by
   themselves.
3. If the page does not open, browse to **http://192.168.4.1**.
4. Fill in the form:
   - Wi-Fi network name and password for your home network.
   - Speech endpoint URL, model, and API key.
   - Optionally tick the cleanup box and fill in the language model section.
   - Obsidian's Local REST API address, for example
     `http://192.168.1.20:27123`, and the API key from the plugin settings.
   - Choose daily-note or new-note mode, and set the time zone.
5. Press **Save and restart**. The device reboots, joins your network, and
   shows the ready face.
6. From a computer or phone on your normal network, open the address shown
   on the info screen (press Up) and use the three **Test** buttons. Each one
   makes a real request: the speech test lists models, the language model
   test asks for a one-word reply, and the Obsidian test reads the vault
   root and, in daily mode, checks that the Periodic Notes route answers.
   The buttons are disabled while the page is served from the setup hotspot,
   because the device has no internet there.

The settings page keeps running after setup. Once the device is on your
network, open the address shown on the info screen (press Up) in a browser to
change anything without going back to setup mode. Saved API keys are never
sent back to the browser, and leaving a key field blank keeps the stored one.
The same server accepts `POST /api/show` with a plain UTF-8 body and puts that
text on the screen, which is handy for checking fonts and layout:

```
curl -X POST http://<device-ip>/api/show --data-binary "Hello 你好"
```

## Settings reference

| Key | Default | Meaning |
| --- | --- | --- |
| `wifi_ssid` | empty | Name of the 2.4 GHz network to join. Empty means the device boots into setup mode. |
| `wifi_pass` | empty | Wi-Fi password. Empty is treated as an open network. |
| `stt_url` | `https://api.groq.com/openai/v1/audio/transcriptions` | OpenAI-compatible transcription endpoint. The Test button replaces the `/audio/...` tail with `/models` to check reachability. |
| `stt_model` | `whisper-large-v3-turbo` | Model name sent in the multipart `model` field. |
| `stt_key` | empty | Bearer token for the speech endpoint. |
| `stt_lang` | empty | ISO-639-1 language hint, for example `en`. Empty lets the model auto-detect, which is slower and occasionally wrong. |
| `llm_on` | off | Whether to run the transcript through a language model before saving. |
| `llm_kind` | `anthropic` | Request shape, either `anthropic` or `openai`. Anthropic sends `x-api-key` and `anthropic-version`, OpenAI sends a bearer token. |
| `llm_url` | `https://api.anthropic.com/v1/messages` | Chat endpoint. Change this to `.../v1/chat/completions` when using the `openai` kind. |
| `llm_model` | `claude-haiku-4-5` | Model name. |
| `llm_key` | empty | API key for the language model. |
| `llm_prompt` | A dictation prompt: fix punctuation and mis-hearings, drop fillers and stutters, keep only the corrected half of self-corrections ("I mean", "no wait"), never summarize or add ideas, reply with only the text. | System prompt for the cleanup pass. Edit it on the settings page to make cleanup more or less aggressive. |
| `obs_url` | `http://192.168.1.2:27123` | Scheme, host, and port of the Local REST API. A trailing slash is trimmed. An `https://` address is accepted with a self-signed certificate. |
| `obs_key` | empty | Local REST API key, sent as a bearer token. |
| `obs_mode` | `daily` | `daily` appends a line to today's daily note. `note` creates a new file. |
| `obs_folder` | `Inbox` | Vault folder for new notes. Used only in `note` mode. Empty puts notes at the vault root. |
| `obs_line` | `- **{time}** {text}` | Template for the daily-note line. `{time}` becomes `HH:MM` and `{text}` becomes the transcript with newlines flattened to spaces. Used only in `daily` mode. |
| `device_name` | `Obsidian Sticky` | The heading on the device's info screen and the hostname it gives your router, so a household with more than one can tell them apart. Trimmed, capped at 32 bytes on a character boundary, and a blank field goes back to the default. The footer keeps saying `Obsidian Sticky` and the firmware version whatever you rename it to. The hostname keeps letters and digits and turns every other run of characters into a single hyphen, so "Fridge Note" becomes `Fridge-Note`; a name with no Latin characters falls back to `Obsidian-Sticky`. Your router sees the new name at the next DHCP lease, so restart the device after renaming it. |
| `text_size` | `auto` | Note text size on the screen. `auto` picks the largest size that shows the whole note at once. The fixed sizes are named by how much they fit: `small` (about 9 lines), `medium` (about 7), `large` (about 5) and `xlarge` (about 4). |
| `text_font` | `inter` | Typeface for Latin text: `inter`, `atkinson` (Atkinson Hyperlegible), `opensans`, `literata` (serif) or `shantell` (Shantell Sans, handwriting). All five are baked to the same capital height, so switching changes the letterforms and not the size or the lines that fit. Chinese ignores this and always uses Noto Sans TC. |
| `tz` | `EST5EDT,M3.2.0,M11.1.0` | POSIX TZ string, used for the clock, the daily-note timestamp, and new-note filenames. |
| `sleep_min` | `10` | Idle minutes before deep sleep. 0 disables sleep. |
| `wifi_idle_min` | `0` | Idle minutes before the Wi-Fi radio is stopped to save power, trading a few seconds of reconnect on the next note for a longer time between charges. 0 keeps it on. The status band strikes through the aerial icon and any button restarts the radio, but the settings page is unreachable until it does. Never fires when `sleep_min` is smaller and non-zero, because deep sleep comes first. |
| `beep` | on | Buzzer cues on record start, record stop, save, and error. |

New notes are named `YYYY-MM-DD HHMM Voice note.md` and carry a small
frontmatter block with `created` and `source: reterminal-sticky`.

## Building and flashing from source

You need ESP-IDF v5.4.4. This is a Windows-first project and the toolchain is
driven from PowerShell, not from WSL: flashing over USB from WSL needs
`usbipd-win` to attach the serial device into the Linux VM, which is more
moving parts than it is worth here.

In a PowerShell window, dot-source the export script so it can set the
environment in your current shell, then build:

```powershell
. C:\Users\Madelena\esp\esp-idf\export.ps1
cd D:\GitHub\Obsidian-Sticky
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Replace `COM3` with the port Device Manager lists as "USB-Enhanced-SERIAL CH343"; that is the Sticky. No button combination is needed, the bridge resets the chip into download mode by itself.
Press `Ctrl+]` to leave the monitor.

Notes:

- Git Bash cannot be used for this. The ESP-IDF installer and `idf.py` both
  refuse to run under an MSYS environment.
- `export.ps1` must be dot-sourced, with the leading `. ` and a space.
  Running it as a plain command sets the variables in a child shell that
  exits immediately, and `idf.py` will then not be found.
- Do not edit `sdkconfig` by hand. Board facts and project choices live in
  `sdkconfig.defaults`. To pick up a change there, delete `sdkconfig` and
  build again.
- The firmware logs on UART0 through the on-board USB-serial bridge. The
  native USB-Serial-JTAG console is disabled on purpose, because those two
  pins are the microphone pins on this board.

## Chinese and other scripts

English and the rest of ASCII are drawn from bitmap fonts baked into the
firmware, in whichever of the five families `text_font` names. Everything above
U+007F ignores that setting and is drawn from a TrueType font that lives in its
own 8 MB flash partition and is rasterized as the screen is painted, so
Chinese, Japanese and Korean text needs that partition to be flashed.

The font is Noto Sans TC, which covers traditional and simplified Chinese as
well as kana and the common punctuation, and is not in this repository. Build
it and flash it in two commands:

```powershell
python tools\fetch_cjk_font.py
idf.py -p COM3 flash
```

The first command downloads the variable font from Google Fonts, pins it at
regular weight, subsets it to the ranges the screen needs, and writes
`build\font_cjk.ttf`, about 5.6 MB. The second flashes the firmware and the
font together: the build only notices the file when CMake runs, so if you
built before creating it, run `idf.py reconfigure` once. `fetch_cjk_font.py`
needs `fonttools`, which `pip install fonttools` provides.

Without the partition nothing breaks. Accented Latin letters lose their
marks, curly quotes and dashes become ASCII ones, and anything else, Chinese
included, shows as `?`. The note saved to your vault is always the full
original text either way; only the screen folds.

For Cantonese, set the `stt_lang` setting to `yue`. Groq Whisper accepts it
as a language hint and the transcript comes back in traditional Chinese
characters.

## Publishing to the Seeed Playground

The Playground firmware page has a **Submit with the form** button, which asks
for a merged image written at `0x0` plus a name, a one-line summary, a version,
a license (MIT here), a project page, and one photo of the device actually
running the firmware. The merged image to upload is
`obsidian-sticky-merged.bin` from the
[GitHub Release](https://github.com/Madelena/Obsidian-Sticky/releases) for that
version; it already contains the bootloader, the partition table, the app and
the CJK font, so nothing needs to be rebuilt for the submission.

## Troubleshooting

**The device turns itself off a second after boot.** The power latch did not
take. The reTerminal Sticky keeps its rail up only while firmware holds
`POWER_HOLD` high and pulses `POWER_LOCK`, which is the first thing
`board::init()` does in `app_main`. If you have modified the boot order, or
something before it crashed, the rail drops as soon as you release the
button. Watch the serial log while holding the side button down, which keeps
the rail alive long enough to read the panic.

**It never joins Wi-Fi.** The radio is 2.4 GHz only. A combined 2.4/5 GHz
network with band steering usually works, but a 5 GHz-only SSID never will.
Check the SSID and password by holding Down for 3 seconds to get back into
setup mode. Note that the device waits up to 20 seconds for a connection
before it gives up on a recording. On mesh networks a node sometimes accepts
the association but never hands out an address; the firmware drops such a
link after 12 seconds and reconnects, which usually lands on another node.

**Save failed, HTTP 401.** The Local REST API key is wrong. Copy it again
from the plugin's settings tab in Obsidian. Keys are stored but never shown
back to you, so a blank field on the settings page means the stored key is
being kept, not that it is missing.

**Save failed, "Periodic Notes plugin missing".** You are in daily-note mode
and the `/periodic/daily/` route returned 404. Install the
"Local REST API - Periodic Notes" companion plugin and enable it, and make
sure Obsidian's own Daily Notes or Periodic Notes plugin is enabled so there
is a daily note to append to. If you would rather not install it, switch
`obs_mode` to new-note mode on the settings page.

**Save failed with a connection error.** The computer running Obsidian is
asleep, Obsidian is closed, the Local REST API's HTTP server is switched off,
or the IP address has changed. The address in `obs_url` is a plain LAN
address, so give that machine a DHCP reservation if your router likes to move
it around.

**Nothing is heard, or the microphone is silent after a wake from sleep.**
This was a real bug and is handled. GPIO19 and GPIO20 are both the PDM
microphone pins and the ESP32-S3's native USB-Serial-JTAG pads, and that pad
connection survives deep sleep, so on the second boot the USB peripheral
still owned the pins. The firmware disables the USB-Serial-JTAG PHY pad and
resets both pins before the I2S driver claims them. If you see silence
anyway, check that nothing else re-enabled the native USB console, and that
the mic power pin is being driven.

**The screen is full of ghosting.** E-ink accumulates artifacts across
partial refreshes. The firmware forces a full refresh every 20 partial ones,
and on every screen that matters (Saved, Ready, errors, the info screen). If
it still looks poor, power off and on.

## License

This firmware is MIT licensed. See [LICENSE](LICENSE).

Vendor drivers, the bundled button component, and the fonts come from other
projects under their own terms. Every one of them is listed with its origin
and license in [THIRD_PARTY.md](THIRD_PARTY.md).
