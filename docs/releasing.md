# Releasing

Producing a flashable image, cutting a release, and the two CI traps that were
hit on the way.

## The CI release flow

Pushing a tag `vX.Y.Z` runs `.github/workflows/release.yml`, which is the
normal route and does everything below by itself. It:

1. Builds in the `espressif/idf:v5.4.4` container, fetching the CJK font first
   because the top-level `CMakeLists.txt` only adds the font partition when
   `build/font_cjk.ttf` exists at configure time.
2. Runs `idf.py merge-bin` and stages `dist/` with the bootloader, partition
   table, app, font, merged image and an ESP Web Tools `manifest.json`.
3. Publishes a GitHub Release with all of those attached.
4. Deploys `web/` plus the same binaries to GitHub Pages, which is the
   installer at <https://madelena.github.io/Obsidian-Sticky/>.

Before tagging, bump `CONFIG_APP_PROJECT_VER` in `sdkconfig.defaults`, delete
the generated `sdkconfig`, and rebuild, or the old number stays baked in and
the device shows it on the info screen.

### Two traps

- **A tag pushed in the same push as the workflow file itself may not trigger a
  run.** GitHub does not always see the workflow as existing on the ref that
  carries it. The fallback is `gh workflow run Release --ref vX.Y.Z`, which
  dispatches it explicitly.
- **The `github-pages` environment rejects tag deployments by default.** The
  pages job fails on the deploy step until a custom deployment branch policy
  allowing `v*` tags is added to that environment in the repository settings.

## Producing the merged image by hand

Rarely needed, since CI attaches one to every release. After a successful
`idf.py build`, from the ESP-IDF PowerShell:

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
characters fall back to ASCII.

The merged image is about 13.7 MB, most of which is the 5.6 MB font partition
plus the padding between partition offsets.

Flash it with:

```powershell
python -m esptool --chip esp32s3 --port COM3 write_flash 0x0 build\obsidian-sticky-merged.bin
```

The CH343 bridge resets the chip into download mode by itself; if esptool
reports "No serial data received", check that you picked the CH343 port and
not another USB serial device.

## Seeed Playground submission

The Playground firmware page has a **Submit with the form** button. It takes
the merged image written at offset 0, plus a name, a one-line summary, a
version, a license (MIT here), a project page, and one photo of the device
actually running the firmware. Upload `obsidian-sticky-merged.bin` from the
GitHub Release for that version; it already contains the bootloader, the
partition table, the app and the CJK font, so nothing needs rebuilding.

For the registry route instead, follow `docs/contributing-firmware.md` in the
[registry](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry)
and build with an explicit version so the artifact matches `firmware.json`:

```powershell
idf.py -D PROJECT_VER=0.7.0 build
```
