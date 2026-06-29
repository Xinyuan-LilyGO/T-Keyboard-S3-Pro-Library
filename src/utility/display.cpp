#include "display.h"

void Display::Stm32CsPanel::setCsControl(CsControlCallback callback, void* context)
{
    _callback = callback;
    _context  = context;
}

void Display::Stm32CsPanel::setTarget(uint8_t address, uint8_t mask)
{
    _address = address;
    _mask    = mask;
}

void Display::Stm32CsPanel::init_cs(void)
{
    // CS is owned by the STM32, not an ESP32 GPIO.
    cs_control(true);
}

void Display::Stm32CsPanel::cs_control(bool level)
{
    if (_callback) _callback(_context, _address, _mask, level);
}

Display::Display()
{
    {   // GC9107 panel (CS multiplexed externally over I2C).
        auto c               = _panel.config();
        c.pin_cs             = -1;     // cs_control() is overridden above
        c.pin_rst            = -1;     // shared RST handled by the manager
        c.pin_busy           = -1;
        c.memory_width       = 128;
        c.memory_height      = 128;
        c.panel_width        = 128;
        c.panel_height       = 128;
        c.offset_x           = 0;
        c.offset_y           = 32;
        c.offset_rotation    = 0;
        c.dummy_read_pixel   = 8;
        c.dummy_read_bits    = 1;
        c.readable           = false;
        c.invert             = false;  // IPS panel
        c.rgb_order          = false;
        c.dlen_16bit         = false;
        c.bus_shared         = true;   // the bus is shared with the other panels
        _panel.config(c);
    }
    setPanel(&_panel);
}

bool Display::begin(lgfx::Bus_SPI& bus,
                    CsControlCallback callback,
                    void* context,
                    uint8_t address,
                    uint8_t mask,
                    bool runInit)
{
    _panel.setBus(&bus);
    _panel.setCsControl(callback, context);
    _panel.setTarget(address, mask);

    if (runInit) {
        if (!init()) return false;
        setRotation(0);
    }
    _initialized = true;
    return true;
}

void Display::setTarget(uint8_t address, uint8_t mask)
{
    _panel.setTarget(address, mask);
}
