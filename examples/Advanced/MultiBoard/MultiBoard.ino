/*
 * MultiBoard — drive the panels across several magnetically-chained boards.
 *
 * Every board (host + expansion modules) shares ONE SPI bus; each board's STM32
 * controls its own panels' chip-select over I2C, on its own I2C address. To draw
 * to a specific physical panel you therefore address it as (screenIndex, board
 * address): TKeyboardS3Pro.displayAt(index, address). Its LovyanGFX cs_control()
 * selects the right STM32 CS automatically when drawing starts.
 *
 * The host (address 0x01) has 4 panels (5th slot = rotary encoder); an expansion
 * module has 5. screenCount(address) returns the right number for each board.
 *
 * Hot-plug: if you connect a module after power-up, call refreshDevices() to
 * scan the bus and initialise the new board's panels.
 */
#include <TKeyboardS3Pro.h>

void labelBoard(uint8_t address, uint16_t color)
{
    uint8_t n = TKeyboardS3Pro.screenCount(address);
    for (uint8_t i = 0; i < n; i++) {
        Display& s = TKeyboardS3Pro.displayAt(i, address);   // panel i on this board
        s.fillScreen(color);
        s.setTextColor(TFT_WHITE);
        s.setTextSize(2);
        s.setCursor(6, 10);
        s.printf("0x%02X", address);   // which board
        s.setTextSize(3);
        s.setCursor(48, 60);
        s.printf("%d", i + 1);         // which panel on it
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(500);
    TKeyboardS3Pro.begin();
    TKeyboardS3Pro.setBrightness(255);

    Serial.printf("boards detected: %u\n", TKeyboardS3Pro.deviceCount());
    for (uint8_t a : TKeyboardS3Pro.devices())
        Serial.printf("   0x%02X  (%u panels)\n", a, TKeyboardS3Pro.screenCount(a));
}

void loop(void)
{
    // Pick up any board plugged in since the last pass.
    TKeyboardS3Pro.refreshDevices();

    // Give each board a distinct colour and label every one of its panels.
    static const uint16_t palette[6] = {
        TFT_NAVY, TFT_MAROON, TFT_DARKGREEN, TFT_PURPLE, TFT_OLIVE, TFT_DARKCYAN
    };
    uint8_t idx = 0;
    for (uint8_t a : TKeyboardS3Pro.devices()) {
        labelBoard(a, palette[idx % 6]);
        idx++;
    }

    delay(1000);
}
