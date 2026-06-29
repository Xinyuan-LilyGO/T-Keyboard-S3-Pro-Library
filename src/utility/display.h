#pragma once
#include <stdint.h>
#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// Display — one physical GC9107 panel as its own LovyanGFX device.
//
// On the T-Keyboard-S3-Pro the panel chip-select lines are not ESP32 GPIOs; the
// on-board STM32 switches them over I2C. Each Display therefore owns a GC9107
// panel object whose cs_control() is overridden: when LovyanGFX asserts/releases
// CS, the manager writes the STM32 LCD_CS register instead of toggling a GPIO.
//
// Display inherits lgfx::LGFX_Device, so the full LovyanGFX drawing API is
// available directly: TKeyboardS3Pro.display1.fillScreen(TFT_RED);
// ---------------------------------------------------------------------------
class Display : public lgfx::LGFX_Device {
public:
    using CsControlCallback = void (*)(void* context,
                                       uint8_t address,
                                       uint8_t mask,
                                       bool level);

    Display();

    // The SPI bus is owned by TKeyboardS3ProClass and shared by every Display.
    bool begin(lgfx::Bus_SPI& bus,
               CsControlCallback callback,
               void* context,
               uint8_t address,
               uint8_t mask,
               bool runInit = true);

    // Retarget this Display. Used by the compatibility/dynamic display object
    // for expansion-board panels and raw CS masks.
    void setTarget(uint8_t address, uint8_t mask);

    bool isInitialized() const { return _initialized; }

private:
    class Stm32CsPanel : public lgfx::Panel_GC9107 {
    public:
        void setCsControl(CsControlCallback callback, void* context);
        void setTarget(uint8_t address, uint8_t mask);

    protected:
        void init_cs(void) override;
        void cs_control(bool level) override;

    private:
        CsControlCallback _callback = nullptr;
        void*             _context  = nullptr;
        uint8_t           _address  = 0;
        uint8_t           _mask     = 0;
    };

    Stm32CsPanel _panel;
    bool         _initialized = false;
};
