#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <vector>
#include <ButtonSense.h>
#include <ButtonManager.h>
#include "board/board_config.h"
#include "board/protocol.h"
#include "utility/display.h"
#include "utility/encoder.h"

// Logging macro (Arduino only — this library targets the Arduino environment).
#define TKBP_LOGI(tag, fmt, ...) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define TKBP_LOGE(tag, fmt, ...) Serial.printf("[%s] ERROR: " fmt "\n", tag, ##__VA_ARGS__)

// Selectively enable/disable subsystems passed to TKeyboardS3Pro.begin().
struct BoardConfig {
    bool     enableDisplay = true;
    bool     enableEncoder = true;
    uint32_t i2cFrequency  = 400000;      // STM32 I2C bus speed. 400 kHz: the
                                          // STM32G030 mishandles CMD_WR_LCD_CS at
                                          // 1 MHz (drops/garbles the CS byte ->
                                          // random/blank panels); 400 kHz is reliable.
    uint32_t spiFrequency  = 20000000;   // single-board display SPI speed.
    uint32_t multiBoardSpiFrequency = 4500000;
                                         // chained-board display SPI speed: the
                                         // panels hang off STM32-multiplexed CS
                                         // over FPC/magnetic links and drop
                                         // pixels much above this.
    uint16_t lcdCsSettleMs = 10;         // wait after writing LCD_CS over I2C so
                                         // the STM32 has switched the CS line
                                         // before SPI data is sent. Required:
                                         // with 0 only one panel ever displays.
    uint16_t stm32StartupMs = 1000;       // wait at begin() for the STM32 main loop
                                         // to be ready before sending CS/init — it
                                         // processes LCD_CS in its main loop, so a
                                         // too-early init races and leaves the
                                         // first panel(s) blank.
};

// ---------------------------------------------------------------------------
// TKeyboardS3ProClass — unified driver for the LILYGO T-Keyboard-S3-Pro.
//
// The board carries up to five GC9107 128x128 panels on one shared SPI bus, an
// STM32G030 co-processor (reached over I2C) that multiplexes the panels' chip-
// select lines, reads the five keys and drives the 14 WS2812C RGB LEDs, and an
// optional rotary encoder on two ESP32 GPIOs. Boards can be magnetically chained
// (up to six), each with its own I2C address; the host defaults to 0x01.
//
//     TKeyboardS3Pro.begin();
//     TKeyboardS3Pro.display1.fillScreen(TFT_RED);    // draw on host panel 1
//     TKeyboardS3Pro.display2.drawString("hi", 0, 0);
//
//     void loop() {
//         TKeyboardS3Pro.update();                       // poll keys + encoder
//         if (TKeyboardS3Pro.key(0).wasPressed()) { ... } // KEY1 on the host
//     }
//
// Everything is reached through the global singleton `TKeyboardS3Pro`.
// ---------------------------------------------------------------------------
class TKeyboardS3ProClass {
public:
    static constexpr uint8_t SCREEN_COUNT      = 5;  // max panels per board (expansion module)
    static constexpr uint8_t HOST_SCREEN_COUNT = 4;  // host: the 5th slot is the rotary encoder
    static constexpr uint8_t KEY_COUNT         = 5;
    static constexpr uint8_t LED_COUNT         = tkbpro::LED_COUNT;   // 14

    // Public host panels. Each is a full LovyanGFX device; its cs_control()
    // selects/releases the STM32-owned CS automatically during drawing.
    Display        display1;
    Display        display2;
    Display        display3;
    Display        display4;
    Display        display;    // dynamic/compatibility display for selectScreen()

    // Rotary encoder (optional 5th-screen replacement).
    RotaryEncoder  encoder;

    bool begin(void) { BoardConfig cfg; return begin(cfg); }
    bool begin(BoardConfig cfg);
    void end();

    // Poll loop — call from loop(). Reads the host key state (for the wasPressed /
    // isPressed edge helpers) and advances the encoder.
    void update();

    // ----------------------------------------------------------------- Display
    // Low-level: write the raw chip-select mask of ONE board (does NOT touch the
    // other chained boards). Pass a tkbpro::ScreenMask or an OR of CS bits. NOTE:
    // selecting several panels at once (e.g. SCREEN_ALL) is unreliable on the
    // STM32 firmware — prefer displayAt() / one CS bit at a time for drawing.
    void selectScreen(uint8_t mask, uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // Deselect every panel on every detected board (clean slate across the bus).
    void deselectAllScreens();

    // Access a physical panel by index/address. Host panels return display1..4;
    // expansion panels use an internal dynamic Display retargeted to that panel.
    // index is 0-based (0..3 on the host, 0..4 on an expansion module).
    Display& displayAt(uint8_t index, uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // Backward-compatible alias for older sketches.
    Display& screen(uint8_t index, uint8_t address = tkbpro::DEFAULT_ADDRESS) {
        return displayAt(index, address);
    }

    // Number of panels on `address`: HOST_SCREEN_COUNT (4) for the host, else
    // SCREEN_COUNT (5) for an expansion module.
    uint8_t screenCount(uint8_t address) const {
        return (address == tkbpro::DEFAULT_ADDRESS) ? HOST_SCREEN_COUNT : SCREEN_COUNT;
    }

    // Map a 0-based panel index to its LCD-CS bit. The host's four panels sit on
    // CS1..CS4 (0x10/0x08/0x04/0x02); CS5 (0x01) is the rotary-encoder slot. An
    // expansion module's five panels use CS1..CS5 directly. So index maps to CS
    // directly (no offset) on every board.
    uint8_t panelMask(uint8_t index, uint8_t address) const {
        (void)address;
        return tkbpro::screenMaskFromIndex(index);
    }

    // Fill every panel on every detected board with one colour, one panel at a
    // time.
    void fillAllScreens(uint16_t color);

    // Shared backlight for all panels; 0..255.
    void setBrightness(uint8_t brightness);

    // ----------------------------------------------------------------- Keys
    // The five host keys are driven by the ButtonSense library: update() refreshes
    // the host key byte over I2C and feeds it into a per-key ButtonSense state
    // machine, giving debounce plus click / double-click / multi-click / hold /
    // hold-repeat events on top of the raw press/release. The keys live on the
    // STM32 (read over I2C), so each ButtonSense reads its bit from the cached key
    // byte via a state callback rather than from a GPIO.

    // Live read of the 5-bit key state of `address` (bit set == pressed). This is
    // the raw, un-debounced byte straight from the STM32 (any chained board).
    uint8_t keys(uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // Last host key byte captured by update() (bit set == pressed). Use this when
    // a sketch wants a live visual mirror without doing a second I2C read.
    uint8_t keyState() const { return _keyState; }

    // The ButtonSense object for host key 0..4 (KEY1..KEY5). Driven by update().
    // Poll it — key(i).wasPressed() / wasReleased() / isPressed() for the basic
    // edges, or key(i).wasClicked() / wasDoubleClicked() / wasHold() /
    // getClickCount() for the rich events — register a callback
    // (key(0).onEvent(cb)), or tune the thresholds (key(i).setHoldThresh(ms)).
    button::ButtonSense& key(uint8_t keyIndex) { return _keyBtns[keyIndex]; }

    // The ButtonManager aggregating the five host keys (added in KEY1..KEY5 order,
    // so combo bit0==KEY1 .. bit4==KEY5). Register combos with
    // keyManager().onCombo(mask, cb) or poll wasComboPressed(mask). Driven by
    // update().
    button::ButtonManager& keyManager() { return _keyManager; }

    // ----------------------------------------------------------------- Encoder
    long encoderPosition() const { return encoder.position(); }

    // ----------------------------------------------------------------- RGB LEDs
    // Low-level register writes (mode must be LED_MODE_FREE for manual control).
    void ledSetMode(uint8_t mode, uint8_t address = tkbpro::DEFAULT_ADDRESS);
    void ledSetBrightness(uint8_t brightness, uint8_t address = tkbpro::DEFAULT_ADDRESS); // 0..100
    void ledSetColor(uint16_t hue, uint8_t saturation, uint8_t address = tkbpro::DEFAULT_ADDRESS); // hue 0..360, sat 0..100
    void ledSelect(uint16_t ledMask, uint8_t address = tkbpro::DEFAULT_ADDRESS); // bit0==LED1 .. bit13==LED14
    void ledClear(uint8_t address = tkbpro::DEFAULT_ADDRESS);
    void ledShow(uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // High-level: paint `ledMask` LEDs one HSV colour and latch it. Switches the
    // strip into free mode first. hue 0..360, saturation 0..100, brightness
    // 0..100; ledMask bit0==LED1 .. bit13==LED14, defaulting to all 14 LEDs.
    void setLeds(uint16_t hue, uint8_t saturation, uint8_t brightness,
                 uint16_t ledMask = 0x3FFF, uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // ----------------------------------------------------------------- Devices
    // Live scan of the keyboard I2C bus for present boards (7-bit addresses).
    std::vector<uint8_t> scanDevices();
    bool    isDevicePresent(uint8_t address);
    uint8_t firmwareVersion(uint8_t address = tkbpro::DEFAULT_ADDRESS);

    // Boards known since begin()/refreshDevices(). The host (0x01) is first.
    const std::vector<uint8_t>& devices() const { return _devices; }
    uint8_t deviceCount() const { return (uint8_t)_devices.size(); }

    // Re-scan the bus and initialise the panels of any newly attached board, so
    // hot-plugged expansion modules become controllable. Returns the new count.
    uint8_t refreshDevices();

    static inline void delay(uint32_t msec) { ::delay(msec); }

    bool isInitialized() const { return _initialized; }
    const board_pin_config_t& pins() const { return *_pins; }
    TwoWire& bus() { return *_wire; }

private:
    // STM32 I2C transaction primitives (match the official Arduino_DriveBus ops).
    bool writeReg(uint8_t address, uint8_t command, uint8_t data);
    bool readReg(uint8_t address, uint8_t command, uint8_t* data, size_t length);

    // Select a CS mask so it is ACTUALLY active before the next SPI op. The STM32
    // latches a CS-register write only when the following write arrives, so a
    // single write takes effect one selection late; this writes twice (with settle
    // delays) so the second flushes the first through.
    void selectCs(uint8_t address, uint8_t mask);
    static void displayCsControl(void* context,
                                 uint8_t address,
                                 uint8_t mask,
                                 bool level);
    void handleDisplayCs(uint8_t address, uint8_t mask, bool level);
    void setSpiFrequency(uint32_t hz);
    uint32_t displaySpiFrequencyForDeviceCount(size_t count) const;

    // Initialise the panels of the given boards, one CS at a time (hot-plug).
    void initScreens(const std::vector<uint8_t>& addresses);

    // Attach the shared backlight pin to a PWM channel (manual backlight control).
    void ledcAttachBacklight(int pin);

    // Shared SPI bus used by every Display.
    lgfx::Bus_SPI _bus;
    uint32_t _singleBoardSpiFrequency = 20000000;
    uint32_t _multiBoardSpiFrequency  = 4500000;
    uint32_t _currentSpiFrequency     = 0;

    TwoWire*                  _wire        = &Wire;
    const board_pin_config_t* _pins        = &BOARD_PIN_CONFIG;
    bool                      _initialized = false;

    // Cached host key byte, refreshed once per update(); the per-key ButtonSense
    // state callbacks read their bit from it (the keys are on the STM32 over I2C,
    // not GPIO). The ButtonSense state machines do the edge/debounce tracking.
    uint8_t _keyState = 0;
    button::ButtonSense   _keyBtns[KEY_COUNT];
    button::ButtonManager _keyManager;

    // Settle time after an LCD_CS write, before SPI data is sent (from cfg).
    uint16_t _csSettleMs = 100;

    // Boards present on the bus (host first), cached at begin()/refreshDevices().
    std::vector<uint8_t> _devices;

    // Currently selected panel/mask.
    uint8_t _activeAddress = tkbpro::DEFAULT_ADDRESS;
    uint8_t _activeMask    = tkbpro::SCREEN_NONE;

    static const char* TAG;
};

// Global singleton instance.
extern TKeyboardS3ProClass TKeyboardS3Pro;
