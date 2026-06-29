/*
 * RGB — drive the 14 on-board WS2812C LEDs through the STM32 co-processor.
 *
 * The LEDs are not wired to the ESP32 directly; they are controlled with I2C
 * register writes. setLeds() bundles the usual sequence (free mode -> colour ->
 * brightness -> select -> show). Hue is 0..360, saturation 0..100, brightness
 * 0..100. With several boards chained, keep the brightness low (~10).
 */
#include <TKeyboardS3Pro.h>

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();

    uint8_t fw = TKeyboardS3Pro.firmwareVersion();
    Serial.printf("STM32 firmware version: 0x%02X\n", fw);
}

void loop(void)
{
    // Sweep the hue across all 14 LEDs (saturation 100 = fully saturated).
    for (uint16_t hue = 0; hue < 360; hue += 5) {
        TKeyboardS3Pro.setLeds(hue, 100 /* saturation */, 60 /* brightness */);
        delay(20);
    }
}
