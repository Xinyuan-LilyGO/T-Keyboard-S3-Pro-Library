/*
 * BleKeyboard — T-Keyboard-S3-Pro as a Bluetooth HID keyboard.
 *
 * Each of the five physical keys sends a configurable HID keycode over BLE.
 * The rotary encoder sends Volume Up / Volume Down media keys.
 * The four host panels show the BLE connection state and the last key event
 * on each panel.  KEY5 (no panel) is displayed on Serial only.
 *
 * Default key mapping (edit KEY_MAP[] below):
 *   KEY1 → 'A'
 *   KEY2 → 'B'
 *   KEY3 → 'C'
 *   KEY4 → 'D'
 *   KEY5 → Space
 *   ENC↑ → Volume Up
 *   ENC↓ → Volume Down
 *
 * Hold KEY1 + KEY2 simultaneously (combo) to toggle BLE advertising on/off.
 *
 * Dependencies:
 *   - TKeyboardS3Pro (this library)
 *   - ESP32 BLE Arduino (ships with the esp32 Arduino core >= 3.x)
 *     Uses the BleKeyboard wrapper from ESP32 BLE Keyboard library; install
 *     via Arduino Library Manager: "ESP32 BLE Keyboard" by T-vK.
 *
 * Board: LILYGO T-Keyboard-S3-Pro  (esp32 core >= 3.x, IDF5)
 */

#include <TKeyboardS3Pro.h>
#include <BleKeyboard.h>         // https://github.com/T-vK/ESP32-BLE-Keyboard

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr uint8_t KEYS    = TKeyboardS3ProClass::KEY_COUNT;          // 5
static constexpr uint8_t SCREENS = TKeyboardS3ProClass::HOST_SCREEN_COUNT;  // 4

// HID key sent on click for each of the five physical keys.
static const uint8_t KEY_MAP[KEYS] = {
    'a',        // KEY1
    'b',        // KEY2
    'c',        // KEY3
    'd',        // KEY4
    ' ',        // KEY5 (no panel on host — reported on Serial only)
};

static const char* KEY_LABEL[KEYS] = { "A", "B", "C", "D", "SPC" };

// ---------------------------------------------------------------------------
// BLE keyboard instance
// ---------------------------------------------------------------------------
BleKeyboard bleKb("TKB-S3-Pro", "LILYGO", 100);

// ---------------------------------------------------------------------------
// Panel drawing
// ---------------------------------------------------------------------------

// Colours
static constexpr uint16_t C_BG      = TFT_BLACK;
static constexpr uint16_t C_TITLE   = 0x4208;   // dark grey
static constexpr uint16_t C_KEY     = TFT_CYAN;
static constexpr uint16_t C_PRESS   = TFT_GREEN;
static constexpr uint16_t C_CONN    = TFT_GREEN;
static constexpr uint16_t C_DISCONN = TFT_RED;
static constexpr uint16_t C_WHITE   = TFT_WHITE;
static constexpr uint16_t C_YELLOW  = TFT_YELLOW;

static bool     _lastConnected = false;
static uint32_t _pressCounts[KEYS]   = {};
static char     _lastEvents[KEYS][12] = {};

static void drawPanel(uint8_t i)
{
    if (i >= SCREENS) return;

    bool   down = TKeyboardS3Pro.key(i).isPressed();
    bool   conn = bleKb.isConnected();
    Display& d  = TKeyboardS3Pro.displayAt(i);

    d.fillScreen(C_BG);
    d.setTextSize(1);

    // -- Top bar: BLE status
    d.setTextColor(conn ? C_CONN : C_DISCONN, C_BG);
    const char* status = conn ? "BLE OK" : "BLE...";
    d.setCursor((128 - d.textWidth(status)) / 2, 4);
    d.print(status);

    d.drawFastHLine(0, 14, 128, C_TITLE);

    // -- Key label (large)
    d.setTextSize(4);
    d.setTextColor(down ? C_PRESS : C_KEY, C_BG);
    int lw = d.textWidth(KEY_LABEL[i]);
    d.setCursor((128 - lw) / 2, 28);
    d.print(KEY_LABEL[i]);

    // -- Press count
    d.setTextSize(1);
    d.setTextColor(C_WHITE, C_BG);
    char buf[20];
    snprintf(buf, sizeof(buf), "x %lu", (unsigned long)_pressCounts[i]);
    d.setCursor((128 - d.textWidth(buf)) / 2, 78);
    d.print(buf);

    // -- Last event
    d.setTextColor(C_YELLOW, C_BG);
    d.setCursor((128 - d.textWidth(_lastEvents[i])) / 2, 96);
    d.print(_lastEvents[i]);

    // -- Down indicator
    d.setTextColor(down ? C_PRESS : C_TITLE, C_BG);
    const char* dnStr = down ? "[ DOWN ]" : "[  up  ]";
    d.setCursor((128 - d.textWidth(dnStr)) / 2, 112);
    d.print(dnStr);
}

static void refreshAllPanels()
{
    for (uint8_t i = 0; i < SCREENS; i++) drawPanel(i);
}

// ---------------------------------------------------------------------------
// LED accent: green pulse when connected, slow red when not
// ---------------------------------------------------------------------------
static void updateLeds()
{
    static uint32_t _lastMs = 0;
    if (millis() - _lastMs < 500) return;
    _lastMs = millis();

    if (bleKb.isConnected()) {
        // gentle green breathe
        uint8_t brt = 10 + (uint8_t)(10.0f * sinf((float)millis() / 800.0f));
        TKeyboardS3Pro.setLeds(120, 70, brt);
    } else {
        // slow red fade — advertising
        uint8_t brt = 5 + (uint8_t)(5.0f * sinf((float)millis() / 1200.0f));
        TKeyboardS3Pro.setLeds(0, 80, brt);
    }
}

// ---------------------------------------------------------------------------
// Encoder → media keys
// ---------------------------------------------------------------------------
static long _lastEncoderPos = 0;

static void handleEncoder()
{
    long pos = TKeyboardS3Pro.encoderPosition();
    if (pos == _lastEncoderPos) return;

    if (!bleKb.isConnected()) { _lastEncoderPos = pos; return; }

    if (pos > _lastEncoderPos) {
        bleKb.write(KEY_MEDIA_VOLUME_UP);
        Serial.println("ENC: Volume Up");
    } else {
        bleKb.write(KEY_MEDIA_VOLUME_DOWN);
        Serial.println("ENC: Volume Down");
    }
    _lastEncoderPos = pos;
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();

    // Init panel labels
    for (uint8_t i = 0; i < KEYS; i++) strlcpy(_lastEvents[i], "ready", sizeof(_lastEvents[i]));

    refreshAllPanels();

    // Boot LED sweep
    for (int h = 0; h < 360; h += 30) {
        TKeyboardS3Pro.setLeds(h, 80, 25);
        delay(30);
    }
    TKeyboardS3Pro.setLeds(0, 0, 0);

    // Combo KEY1+KEY2 (bit0|bit1 == 0x03) → toggle advertising (not supported
    // by the library directly, but we can restart it).
    TKeyboardS3Pro.keyManager().onCombo(0x03, [](uint16_t /*mask*/, bool pressed) {
        if (!pressed) return;
        Serial.println("Combo KEY1+KEY2: restarting BLE advertising");
        // BleKeyboard does not expose startAdvertising(); resetting is the
        // simplest approach available with this library.
        bleKb.end();
        delay(200);
        bleKb.begin();
    });

    bleKb.begin();
    Serial.println("BLE Keyboard advertising as \"TKB-S3-Pro\"");
}

void loop()
{
    TKeyboardS3Pro.update();
    handleEncoder();
    updateLeds();

    // Redraw panels when connection state changes
    bool conn = bleKb.isConnected();
    if (conn != _lastConnected) {
        _lastConnected = conn;
        Serial.printf("BLE: %s\n", conn ? "connected" : "disconnected");
        refreshAllPanels();
    }

    // Key events
    for (uint8_t i = 0; i < KEYS; i++) {
        button::ButtonSense& b = TKeyboardS3Pro.key(i);
        bool dirty = false;

        if (b.wasPressed()) {
            _pressCounts[i]++;
            strlcpy(_lastEvents[i], "press", sizeof(_lastEvents[i]));
            if (bleKb.isConnected()) bleKb.press(KEY_MAP[i]);
            Serial.printf("KEY%d: press -> '%c' (total %lu)\n",
                          i + 1, KEY_MAP[i], (unsigned long)_pressCounts[i]);
            dirty = true;
        }

        if (b.wasReleased()) {
            strlcpy(_lastEvents[i], "release", sizeof(_lastEvents[i]));
            if (bleKb.isConnected()) bleKb.release(KEY_MAP[i]);
            Serial.printf("KEY%d: release\n", i + 1);
            dirty = true;
        }

        if (dirty && i < SCREENS) drawPanel(i);
    }

    delay(1);
}
