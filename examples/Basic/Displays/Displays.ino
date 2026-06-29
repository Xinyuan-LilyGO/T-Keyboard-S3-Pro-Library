/*
 * Display — exercises the host's four GC9107 128x128 panels via LovyanGFX.
 *
 * On the T-Keyboard-S3-Pro every panel shares one SPI bus and the chip-select
 * lines are multiplexed by the on-board STM32 over I2C. Each Display object
 * overrides LovyanGFX cs_control(), so drawing on display1..display4 selects
 * the right STM32 CS automatically.
 *
 * The host has FOUR panels (HOST_SCREEN_COUNT) — its 5th slot is the rotary
 * encoder. An expansion module has five; see the MultiBoard example for drawing
 * across several chained boards.
 */
#include <TKeyboardS3Pro.h>

static constexpr uint8_t N = TKeyboardS3ProClass::HOST_SCREEN_COUNT;   // 4 on the host

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();
    TKeyboardS3Pro.setBrightness(255);

    // Clear every host panel (drawn one at a time — the reliable way).
    TKeyboardS3Pro.fillAllScreens(TFT_BLACK);

    // Label each panel with its index.
    for (uint8_t i = 0; i < N; i++) {
        Display& s = TKeyboardS3Pro.displayAt(i);
        s.fillScreen(TFT_NAVY);
        s.setTextColor(TFT_WHITE);
        s.setTextSize(3);
        s.setCursor(48, 50);
        s.printf("%d", i + 1);
    }
}

void loop(void)
{
    // Draw a random filled rectangle on a random panel each pass.
    uint8_t i  = rand() % N;
    Display& s = TKeyboardS3Pro.displayAt(i);

    int x = rand() % s.width();
    int y = rand() % s.height();
    int r = (s.width() >> 4) + 2;
    s.fillRect(x - r, y - r, r * 2, r * 2, rand());

    delay(2);
}
