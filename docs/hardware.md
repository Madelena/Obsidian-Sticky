# reTerminal Sticky hardware notes

Board facts this firmware depends on, verified against Seeed's documentation
and the two shipping ESP32-S3 apps for the board. The authoritative copy of
every number below is `main/pin_config.h`. This file explains them; it does
not replace them.

## Pin table

### Power

| Signal | GPIO | Notes |
| --- | --- | --- |
| `PIN_POWER_BTN` | 4 | Side (AI) button, active low. Also the deep-sleep wake pin. |
| `PIN_POWER_HOLD` | 45 | Latch data. High keeps the rail up. |
| `PIN_POWER_LOCK` | 46 | Latch clock. The pulse makes the latch sample HOLD. |
| `PIN_BAT_CHG_EN` | 39 | `EN_BAT_CHGn` on the BQ25616 charger, active low. Unused. |
| `PIN_CHARGE_STATE` | 40 | Charger status input. Unused. |
| `PIN_EXTERNAL_POWER` | 9 | High while USB power is present. |

### Buttons and buzzer

| Signal | GPIO | Notes |
| --- | --- | --- |
| `PIN_BTN_UP` | 5 | Active low. |
| `PIN_BTN_DOWN` | 6 | Active low. |
| `PIN_BUZZER` | 48 | LEDC PWM. |

### Microphone

| Signal | GPIO | Notes |
| --- | --- | --- |
| `PIN_MIC_CLK` | 19 | Also the native USB D- pad. |
| `PIN_MIC_DATA` | 20 | Also the native USB D+ pad. |
| `PIN_MIC_EN` | 38 | Mic power, active high. |

### I2C1, sensor bus

| Signal | GPIO or address | Notes |
| --- | --- | --- |
| `PIN_SENSOR_SCL` | 0 | Shared bus. |
| `PIN_SENSOR_SDA` | 1 | Shared bus. |
| `BQ27220_I2C_ADDR` | 0x55 | Fuel gauge. Used. |
| `PCF8563_I2C_ADDR` | 0x51 | RTC. Present, unused. |
| `SHT40_I2C_ADDR` | 0x44 | Temperature and humidity. Present, unused. |
| `LSM6DS3_I2C_ADDR` | 0x6A | IMU. Present, unused. |

### I2C0, touch controller

| Signal | GPIO | Notes |
| --- | --- | --- |
| `PIN_TOUCH_SCL` | 2 | GT911, on I2C0. The bus is created and deleted with the power, in `main/board/touch.cpp`. |
| `PIN_TOUCH_SDA` | 3 | |
| `PIN_TOUCH_EN` | 42 | Low at boot. Raised only while a note on screen scrolls. |
| `PIN_TOUCH_INT` | 21 | Output during reset, where its level picks the I2C address, then the controller's own interrupt output. |
| `PIN_TOUCH_RST` | 41 | Low at boot. |

### E-paper panel on SPI2

| Signal | GPIO |
| --- | --- |
| `PIN_EPD_MOSI` | 14 |
| `PIN_EPD_CLK` | 13 |
| `PIN_EPD_MISO` | 12 |
| `PIN_EPD_CS` | 15 |
| `PIN_EPD_DC` | 16 |
| `PIN_EPD_RST` | 17 |
| `PIN_EPD_BUSY` | 18 |
| `PIN_EPD_EN` | 47 (panel power, active high) |

### MicroSD on SPI2

| Signal | GPIO | Notes |
| --- | --- | --- |
| `PIN_SD_CS` | 8 | Driven high at boot so the card never answers on the shared bus. |
| `PIN_SD_DETECT` | 11 | Unused. |
| `PIN_SD_EN` | 10 | Card power. |

## Power latch

The board does not stay on by itself. The side button only brings the rail up
long enough for firmware to start, and firmware then has to take over or the
board dies as soon as the button is released. The latch is a simple data and
clock pair: `POWER_HOLD` is the level to be latched and `POWER_LOCK` is the
strobe that makes the latch sample it.

To hold power on, which `board::init()` does as the very first thing in
`app_main`:

1. Configure `POWER_HOLD` (GPIO45) as an output and drive it high.
2. Configure `POWER_LOCK` (GPIO46) as an output and drive it low.
3. Pulse `POWER_LOCK` low, high, low, with a 10 microsecond delay between
   each step.
4. Wait about 100 ms before doing anything else.

To turn power off, in `board::power_off()`, the sequence is the same pulse
with `POWER_HOLD` driven low instead of high. Two things have to happen
first:

- Wait for the side button to be released. It is on the same node as the
  latch, and pulsing while it is still held re-arms the latch, so the board
  refuses to turn off.
- Release any `gpio_hold_en` on `POWER_HOLD` before changing its level, or
  the write is ignored.

On a wake from deep sleep the latch pins are still held at their pre-sleep
levels. `board::init()` therefore preloads GPIO45 and GPIO46 to the same
levels before calling `gpio_deep_sleep_hold_dis()` and releasing the
individual holds, so the rail never blinks in the gap.

## Shared buses

Each bus is created exactly once, by one owner, and everything else takes a
handle from it.

- **SPI2** is shared by the e-paper panel and the microSD slot.
  `display::init()` owns `spi_bus_initialize(SPI2_HOST, ...)`. The SD card is
  not used, so `board::init()` drives `PIN_SD_CS` high and parks the card: a
  floating chip select corrupts panel transfers. Any future SD support must
  add a device to the existing bus, never initialize SPI2 again.
- **I2C1** is shared by the BQ27220 fuel gauge, the PCF8563 RTC, the SHT40,
  and the LSM6DS3TR-C IMU. `board::init()` owns
  `i2c_new_master_bus(I2C_NUM_1, ...)` and hands the handle out through
  `board::sensor_i2c_bus()`. `battery::init()` takes it as an argument. Any
  new sensor driver must do the same.
- When configuring the SPI bus, every unused data pin must be set to -1. A
  zero would claim GPIO0, which is the sensor bus SCL.

## Display

- The SSD1677 panel is natively 800x480 landscape, and the firmware draws a
  logical canvas in that same 800x480 with the origin at the top left.
- The panel is mounted upside down relative to that canvas, so
  `display::push()` rotates the whole packed framebuffer 180 degrees before
  sending it: reverse the byte order, then reverse the bits within each byte.
  The panel config also sets `mirror_x = true`. Both are needed. Nothing
  outside `main/ui/display.cpp` knows about physical orientation.
- A partial refresh on this controller still submits a whole comparison
  frame, not a dirty rectangle. The driver diffs the new frame against the
  previous one to decide which pixels move. That means a caller cannot leave
  stale content in the buffer and expect it to disappear: whatever region is
  being replaced has to be white-filled first. `screen::show()` sidesteps this
  by clearing the canvas and redrawing everything on every call.
- E-ink accumulates ghosting across partial updates, so `refresh_partial()`
  forces a full refresh every 20th call. Screens that the user reads at rest
  (Saved, Ready, errors, the info screen) request a full refresh directly.
- The canvas and the rotation scratch buffer are both 48000 bytes and both
  live in PSRAM.

## Microphone

- PDM, mono, 16 kHz, 16-bit, read through the I2S PDM RX driver. `clip` caps
  a recording at 90 seconds, which is 2.88 MB of PCM in PSRAM.
- Mic power is `PIN_MIC_EN` (GPIO38), active high, with roughly 20 ms to
  settle after switching it on.
- The first 100 ms of samples after `pdm_mic::start()` are read and thrown
  away. The mic emits a DC settle ramp on power-up that would otherwise show
  up as a thump at the head of every clip and confuse the level meter.
- **GPIO19 and GPIO20 are the native USB-Serial-JTAG pads.** That is a
  dedicated pad connection, not a GPIO-matrix routing, and it survives deep
  sleep. `usb_serial_jtag_ll_phy_enable_pad(false)` followed by
  `gpio_reset_pin` on both pins must run before `i2s_channel_init_pdm_rx_mode`
  or the I2S driver gets a dead pin pair, which reads as a silent microphone.
  `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` in `sdkconfig.defaults` keeps the
  console off those pads for the same reason. Flashing and logs go through
  the external UART bridge on GPIO43/44, so nothing is lost.

## Deep sleep

- Wake source is EXT1 on `PIN_POWER_BTN` (GPIO4), `ESP_EXT1_WAKEUP_ANY_LOW`,
  with the pin set to input, pull-up enabled, pull-down disabled, and then
  held.
- The button must already be released before entering sleep, or the device
  wakes again immediately on the level that is still asserted.
- `power::deep_sleep()` holds this set of pins through sleep, listed here
  because `board.cpp` has to release exactly the same set on the next boot:
  `POWER_HOLD` high, `POWER_LOCK` high, `EPD_EN` low, `TOUCH_EN` low,
  `TOUCH_RST` low, `MIC_EN` low, `SD_EN` low, `BUZZER` low. Then
  `gpio_deep_sleep_hold_en()`.
- `board::init()` calls `gpio_deep_sleep_hold_dis()` and `gpio_hold_dis()` on
  each of those pins near the top of boot. If a pin is added to the hold list
  in `power.cpp`, it must also be added to `kPossiblyHeldPins` in `board.cpp`,
  or it stays stuck at its sleep level forever.
- `board::woke_from_button()` reports whether the wake cause was EXT1, which
  is what lets `pipeline` start recording immediately when the user wakes the
  device by holding the side button down.

## Learned by experiment

Facts that are not in anyone's documentation and were found by building
against the board.

### The side button cannot carry a long press

Holding the AI button is how recording works, so the button is never up long
enough for a long press to mean anything else. Worse, the drain loop at the
end of `record_and_process()` in `main/app/pipeline.cpp` discards the button
release, and it swallows any long press queued behind it. A power-off bound to
the side button was therefore dead code, present and unreachable, until this
was found. Power off lives on a long press of Up instead, matching setup mode
on a long press of Down.

### The BQ27220 discharge bit is the wrong way to detect charging

A full pack sitting on a cable is not discharging either, so the discharge bit
reads as charging forever once the battery tops up. Use the gauge's signed
current instead, positive into the pack, with a threshold of about 10 mA to
ignore the noise around zero. `main/board/battery.cpp` also requires the
external-power pin to be high, and reports "not charging" on a failed read
rather than guessing.

### Only the font file can be memory mapped, not the partition

Mapping the whole 8 MB `font` partition fails with
`Address 0x00810000 is out of range for 24bit flash mapping`: the flash cache
addresses 16 MB and the partition's tail lies beyond it. `cjk_font::init()`
therefore reads the TrueType table directory at the start of the partition,
computes the real file length from it, and maps only that.

### Lines per screen charges the last line only its glyph box

`fitting_lines()` in `main/ui/screen.cpp`:

```
fits = (bottom - top - face.height) / pitch + 1
```

Pitch is the distance to the *next* line, so only the lines before the last
one need it. That is what fits a fifth 52 px line into the same space. `top`
is per face rather than fixed, for the reason in the next section, and
`bottom` is y 412, which is 12 px above the status bar.

| Face | Nominal | Glyph box | Cap gap | Draw top | Pitch | Lines |
| --- | --- | --- | --- | --- | --- | --- |
| `body` (small) | 30 px | 38 px | 9 px | y 27 | 42 px | 9 |
| `large` (medium) | 40 px | 50 px | 11 px | y 25 | 55 px | 7 |
| `xlarge` (large) | 52 px | 66 px | 16 px | y 20 | 73 px | 5 |
| `xxlarge` (xlarge) | 64 px | 80 px | 19 px | y 17 | 88 px | 4 |

The pitch and the glyph box are fixed numbers per face in the `FACES` table of
`tools/gen_font.py`, the same for every family, which is why the figures below
need no per-family column.

Spare pixels under a full page, worst case across the five families:

| Face | Box | Pitch | Lines | Drawn from | Spare |
| --- | --- | --- | --- | --- | --- |
| 30 px | 38 | 42 | 9 | y 28 | 10 |
| 40 px | 50 | 54 | 7 | y 26 | 12 |
| 52 px | 66 | 70 | 5 | y 23 | 43 |
| 64 px | 82 | 86 | 4 | y 20 | 52 |

The 30 px face is the binding one at 10 px, so anything that lowers `bottom`
by more than that costs it a line and `auto` falls through a note sooner. The
spare varies by a pixel between families, because a family whose capitals land
a pixel short of the target gets a pixel more head margin; the column is the
worst of the five.

### The head margin is set per face, because a glyph box is not its ink

`canvas::draw_text()` places a line by the top of its glyph box, but the eye
measures the margin to the ink. A capital sits 8, 10, 13 or 16 px below the top
of the box depending on the face, so a fixed head margin would make a short
note in the 64 px face look tighter at the top than a long one in the 30 px
face. Ascenders reach 1 to 3 px above the capital, which is close enough that
`cap_gap()` measures `H` and ignores them. Under `auto` the face moves with the
note, so the top margin would visibly breathe as notes come and go.

`margin_top()` in `main/ui/screen.cpp` therefore draws at `36 - cap_gap(face)`,
which puts the ink of the first line 36 px down, the same as the 36 px it
clears at the sides. The side figure needs no such correction: the left side
bearing of every capital and digit in these faces is 0.

`cap_gap()` scans the baked `H` for its first inked row rather than carrying a
table, so regenerating a face at a different size cannot leave the layout
quietly wrong. It costs one pass over about 40 bytes, up to four times per
layout under `auto`.

### The GT911 reports no configuration, so it never reports a touch

The one unit tested answers I2C perfectly and never produces a coordinate,
under this firmware and under Seeed's. Its owner reports that touch worked on
this device previously. The state, read back over thousands of polls with zero
read failures:

| Register | Reads | Should be |
| --- | --- | --- |
| 0x8140 product ID | `911` | `911` |
| 0x8144 firmware | 0x1060 | a version |
| 0x8047 config, 24 bytes | all zero | a config table |
| 0x804C touch points | 0 | 1 to 5 |
| 0x80FF checksum, 0x8100 fresh | 0x00, 0x00 | non-zero |
| 0x8146 resolution | 0 by 0 | 800 by 480 |
| 0x814A vendor | 0xFF | a vendor |
| 0x814E status | 0x00, always | bit 7 on touch |

A GT911 with no configuration does not know its sensor geometry and does not
scan, which is why an interrupt counter on `PIN_TOUCH_INT` records zero edges
while the glass is being swiped. This is not a driver bug. What was ruled out,
each on hardware:

- **Not the bus.** Thousands of reads, zero failures, and the product ID and
  firmware version come back correct from the same burst read whose later
  bytes are zero.
- **Not the address.** Both 0x5D and 0x14 answer, with identical contents.
- **Not the power rail.** With `PIN_TOUCH_EN` low the chip does not answer at
  all (`ESP_ERR_NOT_FOUND`), so the pin really does power it and is active
  high, and it is not running on parasitic current through the pull-ups.
- **Not power-up timing.** 250 ms after the rail rises, matching Seeed's own
  driver, changes nothing, and neither does releasing `PIN_TOUCH_RST` across
  the whole power-on rather than letting board.cpp hold it low.
- **Not the command register.** 0x8040 reads 0x00, which is coordinate mode,
  and writing 0x00 to it changes nothing.
- **Not a late load.** Re-reading the config every two seconds for minutes
  shows it blank throughout.

**Seeed's own firmware gets nothing either.** Their 2048 game, built from
source at v1.0.1 and flashed to the same unit, drives its whole UI from
swipes and logs every touch at INFO. Across a 57-second session of
deliberate swiping it logged not one `touch down`, only the AI-button presses.
Two independent drivers, the same silence, so this is the device and not this
firmware.

Their driver also prints `sensor=2048x2048`, which is the hardcoded fallback
in their `GT911` class, used when the resolution read returns zero. That is
not evidence that a blank config is normal: it only shows their driver read
the same zeros on this unit. A healthy GT911 holds a config version at 0x8047
and its output resolution at 0x8048, and Seeed's own code comments expect
real values there. A controller that has lost its configuration is still the
best explanation.

Neither firmware writes to the configuration region. This one only ever writes
0x8040 (command) and 0x814E (status buffer), never 0x8047 to 0x8100, so
nothing here can have erased it.

Writing a configuration from the host would mean supplying the panel's sensor
parameters, which are not published, and committing them to the controller's
flash. That is not worth attempting blind on a device that is more likely to
want a warranty claim.

### The GT911 answers at 0x14, and INT picks that during reset

The address is selected by the level on `PIN_TOUCH_INT` while `PIN_TOUCH_RST`
rises: low selects 0x5D and high selects 0x14. Measured on this board, it
comes up at **0x14** with the ID register reading `911`. `touch.cpp` still
tries both, because the level is only sampled across that one edge and a
missed edge would otherwise be a dead panel rather than a retry.

The sequence, with Seeed's timings, is RST low and INT at the chosen level for
20 ms, RST high for 20 ms, then INT back to an input and 80 ms to settle.
That is about 120 ms per address tried, which is why the last address that
answered is tried first on the next power-up.

Its resolution register (0x8146) should report the sensor's own frame, and
Seeed's driver notes that a working controller maps its native 480 by 800
sensor into an 800 by 480 range. On this unit it reads zero, per the section
above, so neither the axis mapping nor `kInvertY` in `main/board/touch.cpp`
has ever been checked against a real coordinate. Both are guesses until one
arrives.

### The panel blocks the calling task for the whole waveform

About 1.9 s for a full refresh and 0.9 s for a partial, both measured. That
put the panel at 46 percent of the original end to end budget for a note, and
is why drawing moved onto its own task. Numbers and method in
`docs/latency.md`.

## Known ambiguity

Seeed's documentation lists **GPIO7 as both the IMU interrupt and the fuel
gauge interrupt**, in different places. Only one of those can be right. This
firmware uses neither: the fuel gauge is polled over I2C and the IMU is not
used at all, so the conflict has no effect here. Anyone adding motion wake or
gauge interrupts needs to resolve it against the schematic first, not against
the docs.

## Sources

- Seeed Studio, reTerminal Sticky hardware overview,
  <https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/>.
  Primary source for the pin table.
- Seeed Projects, reTerminal Sticky Playground registry,
  <https://github.com/Seeed-Projects/reterminal-sticky-playground-registry>,
  specifically `firmwares/sticky-2048/source`. Source of the vendored
  `seeed_epaper`, `bq27220`, `debug_logging`, and `button` components, and
  the reference for the SSD1677 init sequence.
- Seeed Studio dashboard demo,
  <https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_dashboard_demo.zip>.
  Source of the power latch and deep-sleep pin-hold sequences. No license
  file, so it was read for behavior and not copied.
- Followup Sticky, <https://github.com/alxv2016/folloup-sticky>. GPL-3.0, so
  it was read only, never copied. Useful as a second opinion on the latch
  timing and the panel refresh policy.
- reTerminal Sticky Voice Companion,
  <https://github.com/sira-fiinikkusu/reterminal-sticky-voice-companion>.
  Where the USB-Serial-JTAG pad conflict on GPIO19/20 is documented, which is
  the single hardest thing about this board to work out from scratch.
