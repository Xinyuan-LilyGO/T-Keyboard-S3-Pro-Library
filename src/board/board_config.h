#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// LILYGO T-Keyboard-S3-Pro pin map (ESP32-S3 main MCU).
//
// Unlike the original T-Keyboard-S3 (where the four displays and four keys hang
// directly off the ESP32-S3), the Pro routes its keys, RGB LEDs and per-screen
// chip-select lines through an on-board STM32G030 co-processor reached over I2C.
// The ESP32-S3 therefore only owns:
//
//   * one shared SPI bus driving the (up to five) GC9107 panels — the panels'
//     CS lines are switched by the STM32 over I2C, NOT by the ESP32;
//   * the shared display DC / RST / backlight lines;
//   * the primary I2C bus to the STM32 ("keyboard bus");
//   * the rotary-encoder A/B GPIOs (the 5th screen position can host a knob).
//
// Multiple boards can be magnetically chained on the same I2C bus (up to six),
// each answering on its own 7-bit address (the host defaults to 0x01).
//
// A single hardware revision is supported, so there is one pin table.
// ---------------------------------------------------------------------------
typedef struct {
    // ---------- Primary I2C bus (STM32 keyboard / LED / CS co-processor) ----------
    int i2c_sda;    // 42
    int i2c_scl;    // 2

    // ---------- Secondary I2C bus (spare expansion header) ----------
    int i2c_sda2;   // 6
    int i2c_scl2;   // 7

    // ---------- Display GC9107 (N085-1212TBWIG06-C08, 128x128 IPS) ----------
    // Shared SPI bus for all panels; CS is multiplexed by the STM32 over I2C.
    int lcd_mosi;   // 40
    int lcd_sclk;   // 41
    int lcd_dc;     // 39
    int lcd_rst;    // 38  shared reset (all panels reset together)
    int lcd_bl;     // 1   shared backlight (PWM)

    // ---------- Rotary encoder (optional, replaces the 5th screen) ----------
    int knob_a;     // 4
    int knob_b;     // 5
} board_pin_config_t;

static const board_pin_config_t BOARD_PIN_CONFIG = {
    .i2c_sda  = 42,
    .i2c_scl  = 2,

    .i2c_sda2 = 6,
    .i2c_scl2 = 7,

    .lcd_mosi = 40,
    .lcd_sclk = 41,
    .lcd_dc   = 39,
    .lcd_rst  = 38,
    .lcd_bl   = 1,

    .knob_a   = 4,
    .knob_b   = 5,
};

#ifdef __cplusplus
}
#endif
