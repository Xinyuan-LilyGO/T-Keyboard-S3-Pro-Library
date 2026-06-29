/*
 * Buddy — A Claude desktop companion for the LILYGO T-Keyboard-S3-Pro.
 *
 * Connects to the Claude desktop app (or claude-desktop-buddy bridge) over
 * USB Serial (115200 baud) or BLE (Nordic UART Service).  Displays Claude
 * session state across the host's four 128×128 GC9107 panels and lets you
 * approve/deny permission requests with the physical keys.
 *
 * Panel layout (left → right):
 *   [0] Cat animation — 7 states driven by Claude activity
 *   [1] Session status — running / waiting / total sessions
 *   [2] Transcript / approval prompt
 *   [3] Token counter + key guide
 *
 * Keys:
 *   KEY1 (panel 0) — approve pending permission ("once")
 *   KEY2 (panel 1) — deny pending permission
 *   KEY3 (panel 2) — cycle display brightness
 *   KEY5           — toggle BLE advertising display
 *
 * Wire protocol: newline-delimited JSON on USB Serial (or BLE NUS).
 * See buddy_data.h for the schema.
 *
 * Dependencies (add to your library manager or library.json):
 *   - TKeyboardS3Pro (this library)
 *   - ArduinoJson  >= 7.x
 *   - ButtonSense  (bundled with TKeyboardS3Pro)
 *   - ESP32 BLE Arduino (ships with the esp32 Arduino core)
 *
 * Anti-flicker: each panel has a 128x128 LGFX_Sprite in PSRAM. All drawing
 * goes into the sprite; a single pushSprite() transfers it to the display at
 * the end of each frame — no intermediate black frame, no visible flicker.
 */

#include <TKeyboardS3Pro.h>
#include "buddy_data.h"
#include "buddy_cat.h"
#include "buddy_ble.h"

// ---------------------------------------------------------------------------
// Persona state
// ---------------------------------------------------------------------------
enum PersonaState : uint8_t {
    P_SLEEP = 0,
    P_IDLE,
    P_BUSY,
    P_ATTENTION,
    P_CELEBRATE,
    P_DIZZY,
    P_HEART
};

static PersonaState _oneShotState = P_IDLE;
static uint32_t     _oneShotEnd   = 0;

static void triggerOneShot(PersonaState s, uint32_t durationMs) {
    _oneShotState = s;
    _oneShotEnd   = millis() + durationMs;
}

static PersonaState derivePersona(const BuddyState& st) {
    if (!st.connected)          return P_SLEEP;
    if (st.promptId[0])         return P_ATTENTION;
    if (st.sessionsRunning > 0) return P_BUSY;
    if (st.sessionsWaiting > 0) return P_ATTENTION;
    if (st.sessionsTotal   > 0) return P_IDLE;
    return P_SLEEP;
}

// ---------------------------------------------------------------------------
// Sprites — one per panel, allocated in PSRAM at setup()
// ---------------------------------------------------------------------------
static LGFX_Sprite spr[4];   // spr[0]..spr[3] map to displayAt(0)..displayAt(3)

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static BuddyState  gState     = {};
static uint32_t    gTick      = 0;
static uint32_t    gLastDraw  = 0;
static uint8_t     gBrightness = 200;

static _BuddyLineBuf<512> _bleLine;

static bool     _wasCompleted = false;
static uint32_t _lastTokens   = 0;

// ---------------------------------------------------------------------------
// Drawing helpers (operate on a sprite, not a Display)
// ---------------------------------------------------------------------------

static const uint16_t COL_BG    = TFT_BLACK;
static const uint16_t COL_FG    = TFT_WHITE;
static const uint16_t COL_DIM   = 0x8410;
static const uint16_t COL_GREEN = TFT_GREEN;
static const uint16_t COL_YEL   = TFT_YELLOW;
static const uint16_t COL_RED   = TFT_RED;
static const uint16_t COL_CYAN  = TFT_CYAN;

static inline int centerX(LGFX_Sprite& s, const char* str) {
    return (128 - (int)s.textWidth(str)) / 2;
}
static inline int centerXInt(LGFX_Sprite& s, int v) {
    char buf[12]; itoa(v, buf, 10); return centerX(s, buf);
}

// ---- Panel 0: Cat -----------------------------------------------------------
static void drawCat(PersonaState persona) {
    LGFX_Sprite& s = spr[0];
    s.setTextSize(1);

    switch (persona) {
        case P_SLEEP:     catSleep    (s, gTick); break;
        case P_IDLE:      catIdle     (s, gTick); break;
        case P_BUSY:      catBusy     (s, gTick); break;
        case P_ATTENTION: catAttention(s, gTick); break;
        case P_CELEBRATE: catCelebrate(s, gTick); break;
        case P_DIZZY:     catDizzy    (s, gTick); break;
        case P_HEART:     catHeart    (s, gTick); break;
    }

    uint16_t dotCol = gState.connected ? COL_GREEN : COL_DIM;
    if (gState.promptId[0]) dotCol = COL_YEL;
    s.fillCircle(6, 122, 3, dotCol);

    if (bleConnected()) {
        s.setTextColor(COL_CYAN, COL_BG);
        s.setCursor(110, 118);
        s.print("BT");
    }

    spr[0].pushSprite(&TKeyboardS3Pro.displayAt(0), 0, 0);
}

// ---- Panel 1: Session status ------------------------------------------------
static void drawStatus() {
    LGFX_Sprite& s = spr[1];
    s.fillScreen(COL_BG);
    s.setTextSize(1);

    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "CLAUDE STATUS"), 4);
    s.print("CLAUDE STATUS");

    s.setTextSize(4);
    s.setTextColor(gState.sessionsRunning > 0 ? COL_GREEN : COL_DIM, COL_BG);
    s.setCursor(centerXInt(s, gState.sessionsRunning), 24);
    s.printf("%d", gState.sessionsRunning);

    s.setTextSize(1);
    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "running"), 60);
    s.print("running");

    s.drawFastHLine(4, 72, 120, COL_DIM);

    s.setTextColor(COL_FG, COL_BG);
    char totalBuf[16]; snprintf(totalBuf, sizeof(totalBuf), "total:   %d", gState.sessionsTotal);
    char waitBuf[16];  snprintf(waitBuf,  sizeof(waitBuf),  "waiting: %d", gState.sessionsWaiting);
    s.setCursor(centerX(s, totalBuf), 80); s.print(totalBuf);
    s.setCursor(centerX(s, waitBuf),  92); s.print(waitBuf);

    s.setTextColor(COL_YEL, COL_BG);
    char truncMsg[22]; strncpy(truncMsg, gState.msg, 21); truncMsg[21] = 0;
    s.setCursor(centerX(s, truncMsg), 108);
    s.print(truncMsg);

    spr[1].pushSprite(&TKeyboardS3Pro.displayAt(1), 0, 0);
}

// ---- Panel 2: Transcript or approval prompt ---------------------------------
static void drawTranscript() {
    LGFX_Sprite& s = spr[2];
    s.fillScreen(COL_BG);
    s.setTextSize(1);

    if (gState.promptId[0]) {
        s.setTextColor(COL_YEL, COL_BG);
        s.setCursor(centerX(s, "ALLOW TOOL?"), 4);
        s.print("ALLOW TOOL?");

        s.setTextColor(COL_FG, COL_BG);
        char tool[18]; strncpy(tool, gState.promptTool, 17); tool[17] = 0;
        s.setCursor(centerX(s, tool), 18);
        s.print(tool);

        s.setTextColor(COL_DIM, COL_BG);
        const char* h = gState.promptHint;
        int hlen = strlen(h);
        for (int line = 0; line < 4 && line * 21 < hlen; line++) {
            char row[22]; strncpy(row, h + line * 21, 21); row[21] = 0;
            s.setCursor(centerX(s, row), 32 + line * 10);
            s.print(row);
        }

        s.drawFastHLine(4, 86, 120, COL_DIM);
        s.setTextColor(COL_GREEN, COL_BG);
        s.setCursor(centerX(s, "KEY1=approve"), 94);
        s.print("KEY1=approve");
        s.setTextColor(COL_RED, COL_BG);
        s.setCursor(centerX(s, "KEY2=deny"), 106);
        s.print("KEY2=deny");
    } else {
        s.setTextColor(COL_DIM, COL_BG);
        s.setCursor(centerX(s, "TRANSCRIPT"), 4);
        s.print("TRANSCRIPT");

        if (!gState.connected) {
            s.setTextColor(COL_DIM, COL_BG);
            s.setCursor(centerX(s, "waiting for"), 50);
            s.print("waiting for");
            s.setCursor(centerX(s, "desktop..."), 62);
            s.print("desktop...");
        } else if (gState.nLines == 0) {
            s.setTextColor(COL_DIM, COL_BG);
            s.setCursor(centerX(s, "(no output)"), 58);
            s.print("(no output)");
        } else {
            s.setTextColor(COL_FG, COL_BG);
            for (int i = 0; i < gState.nLines && i < 6; i++) {
                char row[22]; strncpy(row, gState.lines[i], 21); row[21] = 0;
                s.setCursor(4, 16 + i * 18);
                s.print(row);
            }
        }
    }

    spr[2].pushSprite(&TKeyboardS3Pro.displayAt(2), 0, 0);
}

// ---- Panel 3: Tokens + key guide --------------------------------------------
static void drawTokens() {
    LGFX_Sprite& s = spr[3];
    s.fillScreen(COL_BG);
    s.setTextSize(1);

    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "TOKENS TODAY"), 4);
    s.print("TOKENS TODAY");

    char tokBuf[12];
    uint32_t tok = gState.tokensToday;
    if      (tok >= 1000000) snprintf(tokBuf, sizeof(tokBuf), "%lu.%luM", tok/1000000, (tok%1000000)/100000);
    else if (tok >= 1000)    snprintf(tokBuf, sizeof(tokBuf), "%lu.%luK", tok/1000,    (tok%1000)/100);
    else                     snprintf(tokBuf, sizeof(tokBuf), "%lu",      (unsigned long)tok);

    s.setTextSize(2);
    s.setTextColor(COL_CYAN, COL_BG);
    s.setCursor(centerX(s, tokBuf), 18);
    s.print(tokBuf);

    s.setTextSize(1);
    s.drawFastHLine(4, 42, 120, COL_DIM);

    static const char* const GUIDE[] = { "KEY1 approve", "KEY2 deny", "KEY3 bright", "KEY5 BLE" };
    s.setTextColor(COL_DIM, COL_BG);
    for (int i = 0; i < 4; i++) {
        s.setCursor(centerX(s, GUIDE[i]), 48 + i * 12);
        s.print(GUIDE[i]);
    }

    s.drawFastHLine(4, 96, 120, COL_DIM);

    if (gState.connected) {
        const char* badge = bleConnected() ? "BLE connected" : "USB connected";
        s.setTextColor(COL_GREEN, COL_BG);
        s.setCursor(centerX(s, badge), 104);
        s.print(badge);
    } else {
        s.setTextColor(COL_DIM, COL_BG);
        s.setCursor(centerX(s, "disconnected"), 104);
        s.print("disconnected");
    }

    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "TKB-S3-Buddy"), 118);
    s.print("TKB-S3-Buddy");

    spr[3].pushSprite(&TKeyboardS3Pro.displayAt(3), 0, 0);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    TKeyboardS3Pro.begin();
    TKeyboardS3Pro.setBrightness(gBrightness);
    TKeyboardS3Pro.fillAllScreens(TFT_BLACK);

    // Allocate sprites in PSRAM (128x128x2 = 32 KB each, 128 KB total)
    for (int i = 0; i < 4; i++) {
        spr[i].setPsram(true);
        spr[i].setColorDepth(16);
        spr[i].createSprite(128, 128);
        spr[i].fillScreen(TFT_BLACK);
    }

    // Rainbow LED sweep to signal boot
    for (int h = 0; h < 360; h += 30) {
        TKeyboardS3Pro.setLeds(h, 80, 30);
        delay(40);
    }
    TKeyboardS3Pro.setLeds(0, 0, 0);

    bleInit("TKB-S3-Buddy");

    gState.connected = false;
    strncpy(gState.msg, "No Claude connected", sizeof(gState.msg) - 1);
    drawCat(P_SLEEP);
    drawStatus();
    drawTranscript();
    drawTokens();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
    TKeyboardS3Pro.update();

    // ---- Key handling ----
    if (TKeyboardS3Pro.key(0).wasClicked()) {
        if (gState.promptId[0]) {
            buddySendPermission(gState.promptId, true);
            gState.promptId[0] = 0;
            triggerOneShot(P_HEART, 3000);
        }
    }
    if (TKeyboardS3Pro.key(1).wasClicked()) {
        if (gState.promptId[0]) {
            buddySendPermission(gState.promptId, false);
            gState.promptId[0] = 0;
            triggerOneShot(P_DIZZY, 2000);
        }
    }
    if (TKeyboardS3Pro.key(2).wasClicked()) {
        static const uint8_t BRT[] = { 60, 120, 200, 255 };
        static uint8_t bIdx = 2;
        bIdx = (bIdx + 1) % 4;
        gBrightness = BRT[bIdx];
        TKeyboardS3Pro.setBrightness(gBrightness);
    }
    if (TKeyboardS3Pro.key(4).wasClicked()) {
        drawTokens();
    }

    // ---- BLE → data parser ----
    while (bleAvailable()) {
        int c = bleRead();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (_bleLine.len > 0) {
                _bleLine.buf[_bleLine.len] = 0;
                if (_bleLine.buf[0] == '{') _buddyApplyJson(_bleLine.buf, &gState);
                _bleLine.len = 0;
            }
        } else if (_bleLine.len < sizeof(_bleLine.buf) - 1) {
            _bleLine.buf[_bleLine.len++] = (char)c;
        }
    }

    // ---- USB Serial → data parser ----
    buddyDataPoll(&gState);

    // ---- One-shot triggers ----
    if (gState.recentlyCompleted && !_wasCompleted) triggerOneShot(P_CELEBRATE, 4000);
    _wasCompleted = gState.recentlyCompleted;

    if (gState.tokensToday / 50000 > _lastTokens / 50000 && gState.tokensToday > 0)
        triggerOneShot(P_CELEBRATE, 3000);
    _lastTokens = gState.tokensToday;

    // ---- Persona resolution ----
    PersonaState persona = (millis() < _oneShotEnd) ? _oneShotState : derivePersona(gState);

    // ---- LED accent ----
    static uint32_t _lastLedMs = 0;
    if (millis() - _lastLedMs > 500) {
        _lastLedMs = millis();
        uint16_t hue = 0; uint8_t sat = 80; uint8_t brt = 15;
        switch (persona) {
            case P_SLEEP:     hue = 240; sat = 30; brt = 5;  break;
            case P_IDLE:      hue = 200; sat = 50; brt = 10; break;
            case P_BUSY:      hue = 120; sat = 80; brt = 20; break;
            case P_ATTENTION: hue = 45;  sat = 90; brt = 30; break;
            case P_CELEBRATE: hue = (millis() / 100) % 360; brt = 40; break;
            case P_DIZZY:     hue = 300; sat = 80; brt = 20; break;
            case P_HEART:     hue = 0;   sat = 90; brt = 25; break;
        }
        TKeyboardS3Pro.setLeds(hue, sat, brt);
    }

    // ---- Redraw at ~20 fps ----
    uint32_t now = millis();
    if (now - gLastDraw >= 50) {
        gLastDraw = now;
        gTick++;

        drawCat(persona);

        static uint8_t _drawDiv = 0;
        if (++_drawDiv >= 4) {    // text panels at ~5 fps
            _drawDiv = 0;
            drawStatus();
            drawTranscript();
            drawTokens();
        }
    }

    delay(1);
}
