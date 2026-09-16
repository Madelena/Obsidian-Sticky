# Third-party code and assets

| Path | Origin | License |
| --- | --- | --- |
| `components/seeed_epaper/` | SSD1677 panel driver by Seeed Studio, taken from the Sticky 2048 source in the [Playground registry](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry) (`firmwares/sticky-2048/source/components`) | MIT, per the registry entry and the upstream [Lukilyy/reterminal-sticky-2048-eink-game](https://github.com/Lukilyy/reterminal-sticky-2048-eink-game) LICENSE |
| `components/bq27220/` | BQ27220 fuel gauge driver, same source as above | MIT |
| `components/debug_logging/` | Log macros required by `seeed_epaper`, same source as above | MIT |
| `components/button/` | `espressif/button` 4.1.6 with a local ISR cache-safety patch (see `LOCAL_PATCHES.md`), same source as above | Apache-2.0 (`license.txt`) |
| `components/dns_server/` | Captive-portal DNS responder from the ESP-IDF example `examples/protocols/http_server/captive_portal` | Unlicense OR CC0-1.0 (SPDX header in the sources) |
| `components/stb_truetype/` | `stb_truetype.h` v1.26 from [github.com/nothings/stb](https://github.com/nothings/stb), fetched verbatim by `curl` and not modified | Public domain (Unlicense) or MIT, at your option, per the dual-license notice at the end of the header |
| `tools/fonts/AtkinsonHyperlegible-*.ttf` | Braille Institute of America | SIL Open Font License 1.1 (`tools/fonts/OFL.txt`) |
| `main/ui/fonts/*.h` | Bitmaps generated from the fonts above by `tools/gen_font.py` | OFL 1.1 |
| `build/font_cjk.ttf` | Noto Sans TC, subset from [google/fonts](https://github.com/google/fonts/tree/main/ofl/notosanstc) by `tools/fetch_cjk_font.py`, downloaded on demand and never committed | SIL Open Font License 1.1 (`build/NotoSansTC-OFL.txt`, fetched alongside it) |

`main/board/touch.cpp` is written from scratch, but its GT911 register
addresses and reset timings are taken from the `gt911` component in the same
MIT-licensed Sticky 2048 source as the drivers above.

Design references that were read but not copied: Seeed's official
`Sticky_dashboard_demo` (no license file) and the GPL-3.0
[Followup Sticky](https://github.com/alxv2016/folloup-sticky) firmware.
