// =============================================================================
// PIN CONFIG
// =============================================================================
// Every GPIO and I2C address on the reTerminal Sticky, verified against Seeed's
// hardware overview and the two shipping ESP-IDF apps. Include this instead of
// repeating numbers anywhere else.
#pragma once

// Power latch: firmware must hold the rail or the board turns off when the
// AI button is released. See board.cpp for the sequence.
#define PIN_POWER_BTN       4    // AI button, active low, also the deep-sleep wake pin
#define PIN_POWER_HOLD      45
#define PIN_POWER_LOCK      46
#define PIN_BAT_CHG_EN      39   // EN_BAT_CHGn on the BQ25616 charger, active low
#define PIN_CHARGE_STATE    40   // Charger status input
#define PIN_EXTERNAL_POWER  9    // High while USB power is present

// Buttons (all active low)
#define PIN_BTN_UP          5
#define PIN_BTN_DOWN        6

// Buzzer (LEDC PWM)
#define PIN_BUZZER          48

// PDM microphone. GPIO19/20 default to the USB-Serial-JTAG peripheral and are
// released in pdm_mic.cpp before the I2S driver claims them.
#define PIN_MIC_CLK         19
#define PIN_MIC_DATA        20
#define PIN_MIC_EN          38   // Mic power, active high

// I2C1 sensor bus: BQ27220 fuel gauge, PCF8563 RTC, SHT40, LSM6DS3TR-C
#define PIN_SENSOR_SCL      0
#define PIN_SENSOR_SDA      1
#define BQ27220_I2C_ADDR    0x55
#define PCF8563_I2C_ADDR    0x51
#define SHT40_I2C_ADDR      0x44
#define LSM6DS3_I2C_ADDR    0x6A

// I2C0 touch controller (GT911), powered up by board/touch.cpp only while a
// note is long enough to scroll
#define PIN_TOUCH_SCL       2
#define PIN_TOUCH_SDA       3
#define PIN_TOUCH_EN        42
#define PIN_TOUCH_INT       21
#define PIN_TOUCH_RST       41

// E-paper SSD1677 on SPI2
#define PIN_EPD_MOSI        14
#define PIN_EPD_CLK         13
#define PIN_EPD_MISO        12
#define PIN_EPD_CS          15
#define PIN_EPD_DC          16
#define PIN_EPD_RST         17
#define PIN_EPD_BUSY        18
#define PIN_EPD_EN          47   // Panel power, active high

// MicroSD shares SPI2 with the panel; unused, so it is held deselected and off
#define PIN_SD_CS           8
#define PIN_SD_DETECT       11
#define PIN_SD_EN           10
