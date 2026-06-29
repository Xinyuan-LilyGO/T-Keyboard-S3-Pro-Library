/*
 * Keyboard — read the five keys and mirror them on the host's four panels.
 *
 * The keys live on the STM32 co-processor and are read over I2C, and are driven
 * by the ButtonSense library: update() refreshes the host key byte each pass and
 * feeds it into a per-key ButtonSense state machine, reached with key(i) for
 * KEY1..KEY5 (indices 0..4). key(i).wasPressed()/wasReleased()/isPressed() give
 * debounced edge- and level-detection; registering a callback with
 * key(i).wasClicked()/wasDoubleClicked()/wasHold() report the richer click /
 * double-click / hold events. Events are printed to the Serial Monitor at
 * 115200 baud when USB CDC is connected.
 *
 * The host has 5 keys but only 4 panels (its 5th slot is the rotary encoder),
 * so KEY1..KEY4 are shown on the panels and KEY5 is reported on Serial only.
 * For chained boards, read their state directly with keys(address).
 */
#include <TKeyboardS3Pro.h>

static constexpr uint8_t KEYS    = TKeyboardS3ProClass::KEY_COUNT;          // 5
static constexpr uint8_t SCREENS = TKeyboardS3ProClass::HOST_SCREEN_COUNT;  // 4

static uint32_t pressCount[KEYS] = {0};
static const char* lastEvent[KEYS] = {"ready", "ready", "ready", "ready", "ready"};


void drawKey(uint8_t i)
{
    if (i >= SCREENS) return;   // KEY5 has no panel on the host

    bool down = TKeyboardS3Pro.key(i).isPressed();
    Display& s = TKeyboardS3Pro.displayAt(i);
    s.fillScreen(down ? TFT_DARKGREEN : TFT_BLACK);

    s.setTextColor(TFT_WHITE);
    s.setTextSize(2);
    s.setCursor(8, 12);
    s.printf("KEY%d", i + 1);

    s.setTextColor(TFT_CYAN);
    s.setCursor(8, 52);
    s.printf("x %lu", (unsigned long)pressCount[i]);

    s.setTextColor(TFT_YELLOW);
    s.setTextSize(1);
    s.setCursor(8, 92);
    s.print(lastEvent[i]);

    s.setCursor(8, 112);
    s.print(down ? "DOWN" : "up");
}

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();

    for (uint8_t i = 0; i < SCREENS; i++) drawKey(i);
}

void loop(void)
{
    TKeyboardS3Pro.update();   // refresh host key state + ButtonSense events + encoder

    for (uint8_t i = 0; i < KEYS; i++) {
        button::ButtonSense& b = TKeyboardS3Pro.key(i);

        // update() has already read the STM32 and advanced ButtonSense; this loop
        // only consumes the resulting debounced events.
        const char* ev = nullptr;
        if (b.wasPressed()) {
            ev = "press";
            pressCount[i]++;
        }
        if (b.wasReleased())      ev = "release";
        if (b.wasClicked())       ev = "click";
        if (b.wasDoubleClicked()) ev = "double";
        if (b.wasHold())          ev = "hold";

        if (!ev) continue;

        lastEvent[i] = ev;
        Serial.printf("KEY%d: %s (total %lu)\n",
                      i + 1, ev, (unsigned long)pressCount[i]);
        drawKey(i);
    }

    delay(1);
}
