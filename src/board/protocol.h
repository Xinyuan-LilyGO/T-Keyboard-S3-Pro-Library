#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// T-Keyboard-S3-Pro STM32G030 co-processor I2C protocol.
//
// The ESP32-S3 talks to the on-board STM32 slave to: select which display(s)
// receive SPI data (LCD chip-select multiplexing), read the key state, and
// drive the WS2812C RGB LEDs. Each register is an 8-bit command followed by a
// single 8-bit data byte (write) or a single byte read back.
//
// Mirrors the register map from the official T-Keyboard-S3-Pro firmware.
// ---------------------------------------------------------------------------
namespace tkbpro {

// Default 7-bit I2C address of the host board's STM32. Chained boards enumerate
// on further addresses, discovered with TKeyboardS3Pro.scanDevices().
static constexpr uint8_t DEFAULT_ADDRESS = 0x01;

// ---------------------------- Register / command map -----------------------
enum Command : uint8_t {
    CMD_WR_LCD_CS               = 0x01,  // W: per-screen chip-select bitmask
    CMD_RD_KEY_TRIGGER          = 0x02,  // R: 5-bit key state (see Key)
    CMD_WR_LED_MODE             = 0x03,  // W: LedMode
    CMD_WR_LED_BRIGHTNESS       = 0x04,  // W: 0..100
    CMD_WR_LED_COLOR_HUE_H      = 0x05,  // W: hue high byte (hue 0..360)
    CMD_WR_LED_COLOR_HUE_L      = 0x06,  // W: hue low byte
    CMD_WR_LED_COLOR_SATURATION = 0x07,  // W: 0..100
    CMD_WR_LED_CONTROL_1        = 0x08,  // W: flags + LED-select high 6 bits
    CMD_WR_LED_CONTROL_2        = 0x09,  // W: LED-select low 8 bits
    CMD_RD_FIRMWARE_VERSION     = 0x10,  // R: STM32 firmware version
};

// ---------------------------- Display chip-select --------------------------
// Bitmask written to CMD_WR_LCD_CS. Bit set == that panel listens on the bus.
enum ScreenMask : uint8_t {
    SCREEN_NONE = 0B00000000,
    SCREEN_1    = 0B00010000,
    SCREEN_2    = 0B00001000,
    SCREEN_3    = 0B00000100,
    SCREEN_4    = 0B00000010,
    SCREEN_5    = 0B00000001,
    SCREEN_ALL  = 0B00011111,
};

// Map a 0-based CS index to its chip-select bit: 0->CS1(SCREEN_1) .. 4->CS5.
// This is the raw CS-bit mapping. The host's four panels physically sit on
// CS1..CS4 (0x10/0x08/0x04/0x02); CS5 (0x01) is the rotary-encoder slot. So
// panel index maps to CS directly (no offset) — confirmed against the PCB.
// Use TKeyboardS3Pro.panelMask(index, address) / .displayAt() for drawing.
static inline uint8_t screenMaskFromIndex(uint8_t index)
{
    return (index < 5) ? (uint8_t)(1u << (4 - index)) : (uint8_t)SCREEN_NONE;
}

// ---------------------------- Keys -----------------------------------------
// Bits returned by CMD_RD_KEY_TRIGGER (1 == pressed). KEY5 on the host is the
// reused BOOT-0 line, so the firmware exposes it as an internal pull-up key.
enum KeyMask : uint8_t {
    KEY_1 = 0B00010000,
    KEY_2 = 0B00001000,
    KEY_3 = 0B00000100,
    KEY_4 = 0B00000010,
    KEY_5 = 0B00000001,
};

// Map a 0-based key index (0..4) to its bit.
static inline uint8_t keyMaskFromIndex(uint8_t index)
{
    return (index < 5) ? (uint8_t)(1u << (4 - index)) : (uint8_t)0;
}

// ---------------------------- RGB LEDs -------------------------------------
static constexpr uint8_t LED_COUNT = 14;

enum LedMode : uint8_t {
    LED_MODE_NORMAL = 0B00000001,  // host firmware drives the LEDs itself
    LED_MODE_FREE   = 0B00000010,  // user-controlled (use the led* helpers)
    LED_MODE_TEST1  = 0B00000011,  // built-in self-test pattern 1
    LED_MODE_TEST2  = 0B00000100,  // built-in self-test pattern 2
};

// CMD_WR_LED_CONTROL_1 flag bits (top two bits). The low 6 bits select LED1..LED6
// (bit5==LED1 .. bit0==LED6); CMD_WR_LED_CONTROL_2 selects LED7..LED14
// (bit7==LED7 .. bit0==LED14). See ledSelect() for the bit0==LED1 user mask.
enum LedControl1 : uint8_t {
    LED_CTRL1_CLEAR = 0B10000000,  // bit7: clear the pending LED selection
    LED_CTRL1_SHOW  = 0B01000000,  // bit6: latch the selection to the strip
};

} // namespace tkbpro
