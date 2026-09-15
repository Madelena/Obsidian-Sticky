# Third-party code and assets

| Path | Origin | License |
| --- | --- | --- |
| `components/seeed_epaper/` | SSD1677 panel driver by Seeed Studio, taken from the Sticky 2048 source in the [Playground registry](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry) (`firmwares/sticky-2048/source/components`) | MIT, per the registry entry and the upstream [Lukilyy/reterminal-sticky-2048-eink-game](https://github.com/Lukilyy/reterminal-sticky-2048-eink-game) LICENSE |
| `components/bq27220/` | BQ27220 fuel gauge driver, same source as above | MIT |
| `components/debug_logging/` | Log macros required by `seeed_epaper`, same source as above | MIT |
| `components/button/` | `espressif/button` 4.1.6 with a local ISR cache-safety patch (see `LOCAL_PATCHES.md`), same source as above | Apache-2.0 (`license.txt`) |
| `components/dns_server/` | Captive-portal DNS responder from the ESP-IDF example `examples/protocols/http_server/captive_portal` | Unlicense OR CC0-1.0 (SPDX header in the sources) |
| `tools/fonts/AtkinsonHyperlegible-*.ttf` | Braille Institute of America | SIL Open Font License 1.1 (`tools/fonts/OFL.txt`) |
| `main/ui/fonts/*.h` | Bitmaps generated from the fonts above by `tools/gen_font.py` | OFL 1.1 |

Design references that were read but not copied: Seeed's official
`Sticky_dashboard_demo` (no license file) and the GPL-3.0
[Followup Sticky](https://github.com/alxv2016/folloup-sticky) firmware.
