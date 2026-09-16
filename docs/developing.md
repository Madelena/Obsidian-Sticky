# Developing

The working environment and the traps in it. Every entry here cost real time
at least once.

## Toolchain

ESP-IDF v5.4.4, installed at `C:\Users\Madelena\esp\esp-idf`.

```powershell
. C:\Users\Madelena\esp\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

- **PowerShell only.** ESP-IDF refuses MSYS environments, so neither the
  toolchain installer nor `idf.py` runs under Git Bash or WSL-hosted MSYS.
  Git Bash is still fine for `grep`, `git` and file edits.
- **Dot-source `export.ps1`**, with the leading `. ` and a space. Running it as
  a plain command sets the variables in a child shell that exits immediately,
  and `idf.py` is then not on the path.
- **Sourcing it takes about ten seconds.** This matters below.

## Serial

- **The device is the CH343 port**, listed by Device Manager as
  "USB-Enhanced-SERIAL CH343". A second, different USB serial device on the
  same machine is not the Sticky. Check which port is which before flashing;
  writing firmware to the wrong board is the expensive version of this
  mistake.
- **Opening the port resets the device.** The bridge asserts DTR and RTS on
  open, which pulls the chip through a reset. Every capture therefore starts
  with a boot log, and anything you wanted to observe has to happen after the
  port is already open.
- **A capture launched in the background is not listening for the first ten
  seconds**, because it is still sourcing `export.ps1`. Wait for confirmation
  that the port is open before triggering whatever you are trying to capture.
  This wasted a full test cycle: the button was pressed, the note was saved,
  and the log file was empty.
- **A capture holding the port makes flashing fail.** `idf.py flash` reports a
  ninja subcommand error rather than anything about the port. Stop the capture
  first.
- 115200 baud, on UART0 through the on-board bridge. The native
  USB-Serial-JTAG console is off on purpose: those two pads are the microphone
  pins. See `docs/hardware.md`.

## Build

- **Two `idf.py build` runs in the same build directory at once will clash.**
  Serialize them. There is one `build/`, and ninja does not arbitrate between
  two owners.
- **`idf.py merge-bin -o NAME` writes relative to `build/`.** esptool is run
  with `build/` as its working directory, so passing `build/NAME` fails with a
  `FileNotFoundError` for `build/build/NAME`.
- **`sdkconfig` is generated; never edit it.** Change `sdkconfig.defaults`,
  delete `sdkconfig`, then rebuild. Editing the generated file, or changing
  the defaults without deleting it, silently keeps the old value. The version
  string is the usual victim, and the device shows it on the info screen.

## Tools

| Script | Does |
| --- | --- |
| `tools/gen_font.py` | Bakes a TrueType face at one pixel size into a 1-bit C header in `main/ui/fonts/`. Never hand-edit the output. |
| `tools/fetch_cjk_font.py` | Downloads Noto Sans TC, pins it at regular weight, subsets it to the ranges the screen needs, and writes `build/font_cjk.ttf`. Needs `fonttools`. |
| `tools/stt_test.py` | Sends a WAV to the transcription endpoint with byte-for-byte the multipart body `post_wav()` builds. |
| `tools/llm_test.py` | Sends a transcript through the cleanup model with the request bodies `llm_client.cpp` builds, for both the anthropic and openai kinds. |
| `tools/obsidian_test.py` | Writes a note through the Local REST API with the routes, headers and body `obsidian_client.cpp` sends. |

The three `*_test.py` scripts reproduce the firmware's three HTTP requests from
a desktop, so a failing note can be split into "the board is wrong" and "the
request is wrong" without reflashing. Their defaults match
`main/app/settings.cpp` and they take overrides from the environment.

## Putting text on the screen without speaking

The device serves `POST /api/show` with a plain UTF-8 body, on the LAN IP shown
on the info screen. It paints the body as the note and records nothing.

```
curl -X POST http://<device-ip>/api/show --data-binary "Hello 你好"
```

Invaluable for checking fonts, wrapping, line counts and paging: a layout
question that would otherwise need a recording per attempt becomes one command
per attempt.
