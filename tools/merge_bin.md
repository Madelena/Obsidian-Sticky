# Producing a single flashable image

The Playground firmware page and ESP Web Tools both accept one merged image
written at offset 0x0. You rarely need to do this by hand:
`.github/workflows/release.yml` runs the same command on every `v*` tag and
attaches the merged image, the individual binaries and the ESP Web Tools
`manifest.json` to the GitHub Release, and publishes them to the Pages
installer.

To do it locally, after a successful `idf.py build`, run from the ESP-IDF
PowerShell (dot-source `export.ps1` first):

```powershell
idf.py merge-bin -o obsidian-sticky-merged.bin
```

The output path is relative to `build\`, not to the project root, because
`idf.py` runs esptool with `build\` as its working directory. Passing
`build\obsidian-sticky-merged.bin` fails with a `FileNotFoundError` for
`build\build\...`.

That writes bootloader (0x0), partition table (0x8000), the app (0x10000),
and, when `build/font_cjk.ttf` exists, the CJK font partition (0x810000) into
one file using the flash mode and size from `sdkconfig`. Run
`python tools/fetch_cjk_font.py` before building if you want the font in the
merged image; without it the image is the same as before and non-Latin
characters fall back to ASCII. Flash it with:

```powershell
python -m esptool --chip esp32s3 --port COM3 write_flash 0x0 build\obsidian-sticky-merged.bin
```

The CH343 bridge resets the chip into download mode by itself; if esptool
reports "No serial data received", check that you picked the CH343 port and
not another USB serial device.

For a Playground submission, follow `docs/contributing-firmware.md` in the
[registry](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry)
and build with an explicit version so the artifact matches `firmware.json`:

```powershell
idf.py -D PROJECT_VER=0.3.0 build
```
