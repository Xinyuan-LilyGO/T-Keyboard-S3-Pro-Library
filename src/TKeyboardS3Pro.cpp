#include "TKeyboardS3Pro.h"
#include <algorithm>

using namespace tkbpro;

const char* TKeyboardS3ProClass::TAG = "TKeyboardS3Pro";

TKeyboardS3ProClass TKeyboardS3Pro;

// ---------------------------------------------------------------------------
// I2C transaction primitives — replicate Arduino_DriveBus's WriteC8D8 / ReadC8.

bool TKeyboardS3ProClass::writeReg(uint8_t address, uint8_t command, uint8_t data)
{
    _wire->beginTransmission(address);
    _wire->write(command);
    _wire->write(data);
    return _wire->endTransmission() == 0;
}

bool TKeyboardS3ProClass::readReg(uint8_t address, uint8_t command, uint8_t* data, size_t length)
{
    _wire->beginTransmission(address);
    _wire->write(command);
    if (_wire->endTransmission() != 0) return false;
    if (_wire->requestFrom((int)address, (int)length) != (int)length) return false;
    for (size_t i = 0; i < length; ++i) data[i] = (uint8_t)_wire->read();
    return true;
}

// ---------------------------------------------------------------------------

bool TKeyboardS3ProClass::begin(BoardConfig cfg)
{
    const board_pin_config_t& p = *_pins;

    _csSettleMs = cfg.lcdCsSettleMs;
    _singleBoardSpiFrequency = cfg.spiFrequency;
    _multiBoardSpiFrequency  = cfg.multiBoardSpiFrequency;

    // STM32 keyboard / LED / CS co-processor bus (always needed).
    _wire->begin(p.i2c_sda, p.i2c_scl, cfg.i2cFrequency);

    // Bind each host key to a ButtonSense state machine. The keys are read from
    // the STM32 over I2C, so instead of a GPIO each ButtonSense reads its bit from
    // the cached key byte (_keyState, refreshed once per update()) via a state
    // callback. Added to the manager in KEY1..KEY5 order for combo detection.
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        const uint8_t mask = keyMaskFromIndex(i);
        _keyBtns[i].begin();
        _keyBtns[i].setButtonStateFunction(
            [this, mask]() -> uint8_t { return (_keyState & mask) ? 1 : 0; });
        _keyManager.add(_keyBtns[i]);
    }

    if (cfg.enableEncoder) {
        encoder.begin(p.knob_a, p.knob_b);
    }

    if (cfg.enableDisplay) {
        {   // Shared SPI bus (all panels).
            auto c        = _bus.config();
            c.spi_host    = SPI2_HOST;     // FSPI on the ESP32-S3
            c.spi_mode    = 0;
            c.freq_write  = cfg.spiFrequency;
            c.freq_read   = cfg.spiFrequency;
            c.spi_3wire   = false;
            c.use_lock    = true;
            c.dma_channel = 0;             // disabled: keeps the shared bus simple
            c.pin_sclk    = p.lcd_sclk;
            c.pin_mosi    = p.lcd_mosi;
            c.pin_miso    = -1;
            c.pin_dc      = p.lcd_dc;
            _bus.config(c);
        }

        // Release the shared GC9107 reset line (held HIGH). Each panel is then
        // reset in software by its per-CS init sequence below.
        if (p.lcd_rst >= 0) {
            pinMode(p.lcd_rst, OUTPUT);
            digitalWrite(p.lcd_rst, HIGH);
        }

        // Shared backlight, driven manually and brought up before any panel init.
        // It is one shared PWM pin for ALL panels (active-low: duty 0 == full on).
        ledcAttachBacklight(p.lcd_bl);
        setBrightness(255);

        // Wait for the STM32 to be ready — it processes the LCD_CS register in its
        // main loop, so CS writes right after power-on are serviced late.
        delay(cfg.stm32StartupMs);

        // Discover the boards on the bus (host 0x01 first).
        _devices = scanDevices();
        if (_devices.empty()) {
            // No co-processor answered: fall back to the host so begin() still
            // initialises something, but the panels likely won't respond.
            _devices.push_back(DEFAULT_ADDRESS);
            TKBP_LOGE(TAG, "no I2C device found; defaulting to 0x%02X", DEFAULT_ADDRESS);
        }

        setSpiFrequency(displaySpiFrequencyForDeviceCount(_devices.size()));

        // Initialise every panel in one shot: select ALL panels (SCREEN_ALL) and
        // run a single init(). This is reliable here ONLY because selectCs() writes
        // the CS register twice (the STM32 latches a CS write one write late) and
        // the I2C bus runs at 400 kHz — at 1 MHz, or with a single CS write, 0x1F
        // drives only one (random) CS and most panels stay blank.
        selectCs(_devices[0], SCREEN_ALL);
        display.begin(_bus, displayCsControl, this, _devices[0], SCREEN_ALL);
        selectCs(_devices[0], SCREEN_NONE);

        // Fixed host display objects. Their panel cs_control() selects the
        // STM32-owned CS automatically when LovyanGFX starts a transaction.
        display1.begin(_bus, displayCsControl, this,
                       DEFAULT_ADDRESS, panelMask(0, DEFAULT_ADDRESS));
        display2.begin(_bus, displayCsControl, this,
                       DEFAULT_ADDRESS, panelMask(1, DEFAULT_ADDRESS));
        display3.begin(_bus, displayCsControl, this,
                       DEFAULT_ADDRESS, panelMask(2, DEFAULT_ADDRESS));
        display4.begin(_bus, displayCsControl, this,
                       DEFAULT_ADDRESS, panelMask(3, DEFAULT_ADDRESS));

        display.setTarget(DEFAULT_ADDRESS, SCREEN_NONE);
    }

    _initialized = true;
    return true;
}

void TKeyboardS3ProClass::end()
{
    setBrightness(0);
    _initialized = false;
}

void TKeyboardS3ProClass::update()
{
    // Refresh the cached host key byte first — the ButtonSense state callbacks
    // read from it — then advance every key's state machine (and combo detection)
    // in one pass via the manager.
    _keyState = keys(DEFAULT_ADDRESS);
    _keyManager.update();
    encoder.update();
}

// ----------------------------------------------------------------- Display

void TKeyboardS3ProClass::selectCs(uint8_t address, uint8_t mask)
{
    // The STM32 latches a CS-register write only when the NEXT write arrives, so a
    // single write would take effect one selection late. Write twice: the second
    // flushes the first through, so the CS is actually on `mask` on return.
    writeReg(address, CMD_WR_LCD_CS, mask);
    if (_csSettleMs) ::delay(_csSettleMs);
    writeReg(address, CMD_WR_LCD_CS, mask);
    if (_csSettleMs) ::delay(_csSettleMs);
}

void TKeyboardS3ProClass::setSpiFrequency(uint32_t hz)
{
    if (_currentSpiFrequency == hz) return;

    auto c       = _bus.config();
    c.freq_write = hz;
    c.freq_read  = hz;
    _bus.config(c);
    _currentSpiFrequency = hz;
}

uint32_t TKeyboardS3ProClass::displaySpiFrequencyForDeviceCount(size_t count) const
{
    return (count > 1) ? _multiBoardSpiFrequency : _singleBoardSpiFrequency;
}

void TKeyboardS3ProClass::displayCsControl(void* context,
                                           uint8_t address,
                                           uint8_t mask,
                                           bool level)
{
    static_cast<TKeyboardS3ProClass*>(context)->handleDisplayCs(address, mask, level);
}

void TKeyboardS3ProClass::handleDisplayCs(uint8_t address, uint8_t mask, bool level)
{
    // LovyanGFX calls cs_control(false) when a panel transaction starts and
    // cs_control(true) when it ends. The STM32 selection can stay latched after a
    // transaction; changing panels on the next LOW assert is enough, and avoids a
    // very slow I2C deselect after every small SPI transfer.
    if (level) return;

    if (_activeAddress == address && _activeMask == mask) return;

    for (uint8_t a : _devices) {
        if (a != address) selectCs(a, SCREEN_NONE);
    }
    _activeAddress = address;
    _activeMask    = mask;
    selectCs(address, mask);
}

void TKeyboardS3ProClass::selectScreen(uint8_t mask, uint8_t address)
{
    // Select the raw CS mask over I2C (outside any drawing). The selection stays
    // asserted until the next select; the caller draws on it directly.
    _activeAddress = address;
    _activeMask    = mask;
    display.setTarget(address, mask);
    selectCs(address, mask);
}

void TKeyboardS3ProClass::deselectAllScreens()
{
    for (uint8_t addr : _devices) {
        selectCs(addr, SCREEN_NONE);
    }
    _activeMask = SCREEN_NONE;
}

Display& TKeyboardS3ProClass::displayAt(uint8_t index, uint8_t address)
{
    if (address == DEFAULT_ADDRESS) {
        switch (index) {
            case 0:  return display1;
            case 1:  return display2;
            case 2:  return display3;
            case 3:  return display4;
            default: break;
        }
    }

    display.setTarget(address, panelMask(index, address));
    return display;
}

void TKeyboardS3ProClass::initScreens(const std::vector<uint8_t>& addresses)
{
    // Bring up hot-plugged boards one CS at a time (full panel init per CS).
    for (uint8_t addr : addresses) {
        uint8_t n = screenCount(addr);
        for (uint8_t i = 0; i < n; ++i) {
            display.setTarget(addr, panelMask(i, addr));
            display.init();
            display.fillScreen(TFT_BLACK);
        }
    }
    for (uint8_t addr : addresses) {
        selectCs(addr, SCREEN_NONE);
    }
}

void TKeyboardS3ProClass::fillAllScreens(uint16_t color)
{
    for (uint8_t a : _devices) {
        uint8_t n = screenCount(a);
        for (uint8_t i = 0; i < n; i++) displayAt(i, a).fillScreen(color);
    }
}

// Backlight PWM: shared pin, active-low (duty 0 == full on). Driven directly via
// the Arduino ledc API (not LovyanGFX Light_PWM), brought up before panel init.
static constexpr uint32_t BL_PWM_FREQ = 20000;
static constexpr uint8_t  BL_PWM_RES  = 8;     // 0..255
static constexpr uint8_t  BL_PWM_CH   = 7;     // (core 2.x channel)

void TKeyboardS3ProClass::ledcAttachBacklight(int pin)
{
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(pin, BL_PWM_FREQ, BL_PWM_RES);
#else
    ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
    ledcAttachPin(pin, BL_PWM_CH);
#endif
}

void TKeyboardS3ProClass::setBrightness(uint8_t brightness)
{
    // Active-low backlight: invert the duty so 255 == full on, 0 == off.
    uint32_t duty = 255u - brightness;
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(_pins->lcd_bl, duty);
#else
    (void)duty;
    ledcWrite(BL_PWM_CH, 255u - brightness);
#endif
}

// ----------------------------------------------------------------- Keys

uint8_t TKeyboardS3ProClass::keys(uint8_t address)
{
    uint8_t v = 0;
    readReg(address, CMD_RD_KEY_TRIGGER, &v, 1);
    return v;
}

// ----------------------------------------------------------------- RGB LEDs

void TKeyboardS3ProClass::ledSetMode(uint8_t mode, uint8_t address)
{
    writeReg(address, CMD_WR_LED_MODE, mode);
}

void TKeyboardS3ProClass::ledSetBrightness(uint8_t brightness, uint8_t address)
{
    writeReg(address, CMD_WR_LED_BRIGHTNESS, brightness);
}

void TKeyboardS3ProClass::ledSetColor(uint16_t hue, uint8_t saturation, uint8_t address)
{
    writeReg(address, CMD_WR_LED_COLOR_HUE_H, (uint8_t)(hue >> 8));
    writeReg(address, CMD_WR_LED_COLOR_HUE_L, (uint8_t)(hue & 0xFF));
    writeReg(address, CMD_WR_LED_COLOR_SATURATION, saturation);
}

void TKeyboardS3ProClass::ledSelect(uint16_t ledMask, uint8_t address)
{
    // ledMask: bit n selects LED(n+1), i.e. bit0==LED1 .. bit13==LED14.
    // Register packing (per the STM32 IIC register map):
    //   CONTROL_1 bit5..bit0 == LED1..LED6
    //   CONTROL_2 bit7..bit0 == LED7..LED14
    uint8_t c1 = 0, c2 = 0;
    for (uint8_t i = 0; i < 6; ++i)
        if (ledMask & (1u << i)) c1 |= (uint8_t)(1u << (5 - i));
    for (uint8_t i = 6; i < tkbpro::LED_COUNT; ++i)
        if (ledMask & (1u << i)) c2 |= (uint8_t)(1u << (7 - (i - 6)));
    writeReg(address, CMD_WR_LED_CONTROL_1, c1);
    writeReg(address, CMD_WR_LED_CONTROL_2, c2);
}

void TKeyboardS3ProClass::ledClear(uint8_t address)
{
    writeReg(address, CMD_WR_LED_CONTROL_1, LED_CTRL1_CLEAR);
}

void TKeyboardS3ProClass::ledShow(uint8_t address)
{
    writeReg(address, CMD_WR_LED_CONTROL_1, LED_CTRL1_SHOW);
}

void TKeyboardS3ProClass::setLeds(uint16_t hue, uint8_t saturation, uint8_t brightness,
                                  uint16_t ledMask, uint8_t address)
{
    ledSetMode(LED_MODE_FREE, address);
    ledSetColor(hue, saturation, address);
    ledSetBrightness(brightness, address);
    ledClear(address);
    ledSelect(ledMask, address);
    ledShow(address);
}

// ----------------------------------------------------------------- Devices

std::vector<uint8_t> TKeyboardS3ProClass::scanDevices()
{
    std::vector<uint8_t> found;
    for (uint8_t addr = 1; addr < 128; ++addr) {
        _wire->beginTransmission(addr);
        if (_wire->endTransmission() == 0) found.push_back(addr);
    }
    return found;
}

bool TKeyboardS3ProClass::isDevicePresent(uint8_t address)
{
    _wire->beginTransmission(address);
    return _wire->endTransmission() == 0;
}

uint8_t TKeyboardS3ProClass::firmwareVersion(uint8_t address)
{
    uint8_t v = 0;
    readReg(address, CMD_RD_FIRMWARE_VERSION, &v, 1);
    return v;
}

uint8_t TKeyboardS3ProClass::refreshDevices()
{
    std::vector<uint8_t> now = scanDevices();

    // Initialise the panels of any board that wasn't there before (hot-plug).
    std::vector<uint8_t> added;
    for (uint8_t a : now) {
        if (std::find(_devices.begin(), _devices.end(), a) == _devices.end())
            added.push_back(a);
    }

    _devices = now;
    setSpiFrequency(displaySpiFrequencyForDeviceCount(_devices.size()));

    if (!added.empty()) initScreens(added);

    return (uint8_t)_devices.size();
}
