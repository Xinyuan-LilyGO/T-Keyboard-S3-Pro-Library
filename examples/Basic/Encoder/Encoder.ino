/*
 * Encoder — read the optional rotary knob that can replace the 5th panel.
 *
 * The knob's A/B lines are plain ESP32 GPIOs (not on the STM32), decoded by the
 * library's quadrature state machine. update() advances it; encoderPosition()
 * returns the running detent count, and encoder.delta() the change since the
 * last read. The position is also shown on the first panel.
 */
#include <TKeyboardS3Pro.h>

void showPosition(long pos)
{
    Display& s = TKeyboardS3Pro.display1;
    s.fillScreen(TFT_BLACK);
    s.setTextColor(TFT_GREEN);
    s.setTextSize(2);
    s.setCursor(8, 12);
    s.print("ENCODER");
    s.setTextColor(TFT_WHITE);
    s.setTextSize(3);
    s.setCursor(8, 60);
    s.printf("%ld", pos);
}

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();
    showPosition(0);
}

void loop(void)
{
    TKeyboardS3Pro.update();   // polls the encoder (and host keys)

    long d = TKeyboardS3Pro.encoder.delta();
    if (d != 0) {
        long pos = TKeyboardS3Pro.encoderPosition();
        Serial.printf("encoder %s -> %ld\n", d > 0 ? "CW" : "CCW", pos);
        showPosition(pos);
    }

    delay(1);
}
