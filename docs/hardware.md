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
| `PIN_TOUCH_SCL` | 2 | GT911. Unused by this firmware. |
| `PIN_TOUCH_SDA` | 3 | |
| `PIN_TOUCH_EN` | 42 | Driven low at boot to keep the controller powered off. |
| `PIN_TOUCH_INT` | 21 | |
| `PIN_TOUCH_RST` | 41 | Driven low at boot. |

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
