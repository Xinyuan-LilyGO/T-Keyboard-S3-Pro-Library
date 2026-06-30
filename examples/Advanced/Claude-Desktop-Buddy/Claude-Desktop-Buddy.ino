/*
 * Buddy — A Claude desktop companion for the LILYGO T-Keyboard-S3-Pro.
 *
 * Connects to the Claude desktop app (or claude-desktop-buddy bridge) over
 * USB Serial (115200 baud) or BLE (Nordic UART Service).
 *
 * Panel layout (left → right):
 *   [0] Character — GIF animation (if installed) or ASCII cat fallback
 *   [1] Session status — running / waiting / total sessions
 *   [2] Transcript / approval prompt
 *   [3] Token counter + stats + key guide
 *
 * Keys:
 *   KEY1 — approve pending permission ("once")
 *   KEY2 — deny pending permission
 *   KEY3 — cycle display brightness
 *   KEY5 — show BLE/connection status on panel 3
 *
 * Filesystem: LittleFS at /characters/<name>/ (manifest.json + GIF files).
 * Install a character pack with the claude-desktop-buddy desktop tool over BLE.
 *
 * NVS (Preferences): approvals, denials, tokens, level, pet name, owner.
 *
 * Anti-flicker: 128×128 LGFX_Sprite per panel in PSRAM; pushSprite() at
 * frame end — no intermediate black frame.
 *
 * Include order matters:
 *   stats → ble → character_h → xfer (needs stats+ble+character) →
 *   data (needs xfer+stats) → cat (fallback ASCII) → Buddy.ino
 */

#include <TKeyboardS3Pro.h>
#include <LittleFS.h>
#include "buddy_stats.h"      // NVS persistence (include first)
#include "buddy_ble.h"        // BLE NUS bridge
#include "buddy_character.h"  // LittleFS + AnimatedGIF renderer
#include "buddy_xfer.h"       // BLE file-transfer protocol (needs stats+ble+character)
#include "buddy_data.h"       // JSON state parser (needs xfer+stats)
#include "buddy_cat.h"        // ASCII cat fallback

// ---------------------------------------------------------------------------
// Render mode
// ---------------------------------------------------------------------------
bool gGifMode      = false;   // true → draw GIF; false → draw ASCII cat
bool gGifAvailable = false;   // true once a character pack is installed

// ---------------------------------------------------------------------------
// Persona state
// ---------------------------------------------------------------------------
enum PersonaState : uint8_t {
    P_SLEEP = 0, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART
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
// Sprites — one per panel in PSRAM
// ---------------------------------------------------------------------------
LGFX_Sprite spr[4];   // declared extern in buddy_character.cpp

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static BuddyState gState      = {};
static uint32_t   gTick       = 0;
static uint32_t   gLastDraw   = 0;
static uint8_t    gBrightness = 178;  // default 70% (255*0.7)

static _BuddyLineBuf<512> _bleLine;

static bool     _wasCompleted  = false;
static uint32_t _lastTokens    = 0;
static uint32_t _approveStartMs = 0;   // timestamp when prompt appeared

// ---- approval prompt start time (for velocity tracking) ----
static uint32_t _promptArrivedMs = 0;

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static const uint16_t COL_BG    = TFT_BLACK;
static const uint16_t COL_FG    = TFT_WHITE;
static const uint16_t COL_DIM   = 0x8410;
static const uint16_t COL_GREEN = TFT_GREEN;
static const uint16_t COL_YEL   = TFT_YELLOW;
static const uint16_t COL_RED   = TFT_RED;
static const uint16_t COL_CYAN  = TFT_CYAN;
static const uint16_t COL_ORANGE= 0xFC60;

static inline int centerX(LGFX_Sprite& s, const char* str) {
    return (128 - (int)s.textWidth(str)) / 2;
}
static inline int centerXInt(LGFX_Sprite& s, int v) {
    char buf[12]; itoa(v, buf, 10); return centerX(s, buf);
}

// ---- Panel 0: character (GIF or ASCII cat) ----------------------------------
static void drawCharacter(PersonaState persona) {
    LGFX_Sprite& s = spr[0];
    s.setTextSize(1);

    if (gGifMode && gGifAvailable) {
        // GIF mode: characterTick() already drew into spr[0].
        // We only need to overlay the status dot and BLE badge.
    } else {
        // ASCII cat fallback
        switch (persona) {
            case P_SLEEP:     catSleep    (s, gTick); break;
            case P_IDLE:      catIdle     (s, gTick); break;
            case P_BUSY:      catBusy     (s, gTick); break;
            case P_ATTENTION: catAttention(s, gTick); break;
            case P_CELEBRATE: catCelebrate(s, gTick); break;
            case P_DIZZY:     catDizzy    (s, gTick); break;
            case P_HEART:     catHeart    (s, gTick); break;
        }
    }

    // Status dot bottom-left
    uint16_t dotCol = gState.connected ? COL_GREEN : COL_DIM;
    if (gState.promptId[0]) dotCol = COL_YEL;
    s.fillCircle(6, 122, 3, dotCol);

    // BLE badge bottom-right
    if (bleConnected()) {
        s.setTextColor(COL_CYAN, COL_BG);
        s.setCursor(110, 118);
        s.print("BT");
    }

    // Transfer progress bar (bottom, replaces BLE badge during xfer)
    if (xferActive() && xferTotal() > 0) {
        uint8_t pct = (uint8_t)(xferProgress() * 100 / xferTotal());
        s.fillRect(0, 124, 128, 4, COL_DIM);
        s.fillRect(0, 124, pct * 128 / 100, 4, COL_CYAN);
    }

    spr[0].pushSprite(&TKeyboardS3Pro.displayAt(0), 0, 0);
}

// ---- Panel 1: Session status ------------------------------------------------
static void drawStatus() {
    LGFX_Sprite& s = spr[1];
    s.fillScreen(COL_BG);
    s.setTextSize(1);

    // Pet name header
    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, petName()), 2);
    s.print(petName());

    // Big running count
    s.setTextSize(4);
    s.setTextColor(gState.sessionsRunning > 0 ? COL_GREEN : COL_DIM, COL_BG);
    s.setCursor(centerXInt(s, gState.sessionsRunning), 20);
    s.printf("%d", gState.sessionsRunning);

    s.setTextSize(1);
    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "running"), 58);
    s.print("running");

    s.drawFastHLine(4, 70, 120, COL_DIM);

    s.setTextColor(COL_FG, COL_BG);
    char totalBuf[16]; snprintf(totalBuf, sizeof(totalBuf), "total:   %d", gState.sessionsTotal);
    char waitBuf[16];  snprintf(waitBuf,  sizeof(waitBuf),  "waiting: %d", gState.sessionsWaiting);
    s.setCursor(centerX(s, totalBuf), 78); s.print(totalBuf);
    s.setCursor(centerX(s, waitBuf),  90); s.print(waitBuf);

    // Level + fed bar
    char lvlBuf[16]; snprintf(lvlBuf, sizeof(lvlBuf), "Lv%d", stats().level);
    s.setTextColor(COL_ORANGE, COL_BG);
    s.setCursor(4, 102);
    s.print(lvlBuf);

    // 10-pip fed bar
    uint8_t pips = statsFedProgress();
    for (int i = 0; i < 10; i++) {
        uint16_t col = (i < pips) ? COL_ORANGE : COL_DIM;
        s.fillRect(40 + i * 8, 102, 6, 6, col);
    }

    // Msg
    s.setTextColor(COL_YEL, COL_BG);
    char truncMsg[22]; strncpy(truncMsg, gState.msg, 21); truncMsg[21] = 0;
    s.setCursor(centerX(s, truncMsg), 114);
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
    } else if (xferActive()) {
        // Transfer progress screen
        s.setTextColor(COL_CYAN, COL_BG);
        s.setCursor(centerX(s, "INSTALLING"), 4);
        s.print("INSTALLING");

        s.setTextColor(COL_FG, COL_BG);
        s.setCursor(centerX(s, _xCharName), 18);
        s.print(_xCharName);

        uint32_t tot = xferTotal();
        uint32_t prog = xferProgress();
        uint8_t pct = tot > 0 ? (uint8_t)(prog * 100 / tot) : 0;
        char pctBuf[8]; snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
        s.setTextSize(3);
        s.setTextColor(COL_CYAN, COL_BG);
        s.setCursor(centerX(s, pctBuf), 46);
        s.print(pctBuf);

        s.setTextSize(1);
        s.fillRect(4, 84, 120, 8, COL_DIM);
        s.fillRect(4, 84, (int)(pct * 120 / 100), 8, COL_CYAN);
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

// ---- Panel 3: Tokens + stats + key guide ------------------------------------
static void drawTokens() {
    LGFX_Sprite& s = spr[3];
    s.fillScreen(COL_BG);
    s.setTextSize(1);

    s.setTextColor(COL_DIM, COL_BG);
    s.setCursor(centerX(s, "TOKENS TODAY"), 2);
    s.print("TOKENS TODAY");

    char tokBuf[12];
    uint32_t tok = gState.tokensToday;
    if      (tok >= 1000000) snprintf(tokBuf, sizeof(tokBuf), "%lu.%luM", tok/1000000, (tok%1000000)/100000);
    else if (tok >= 1000)    snprintf(tokBuf, sizeof(tokBuf), "%lu.%luK", tok/1000,    (tok%1000)/100);
    else                     snprintf(tokBuf, sizeof(tokBuf), "%lu",      (unsigned long)tok);

    s.setTextSize(2);
    s.setTextColor(COL_CYAN, COL_BG);
    s.setCursor(centerX(s, tokBuf), 14);
    s.print(tokBuf);

    s.setTextSize(1);
    // Mood / approvals / denials
    s.setTextColor(COL_DIM, COL_BG);
    char statBuf[24];
    snprintf(statBuf, sizeof(statBuf), "ok:%u no:%u mood:%u",
             stats().approvals, stats().denials, statsMoodTier());
    s.setCursor(centerX(s, statBuf), 36);
    s.print(statBuf);

    s.drawFastHLine(4, 46, 120, COL_DIM);

    static const char* const GUIDE[] = { "KEY1 approve", "KEY2 deny", "KEY3 status", "KEY5 dim rst" };
    s.setTextColor(COL_DIM, COL_BG);
    for (int i = 0; i < 4; i++) {
        s.setCursor(centerX(s, GUIDE[i]), 50 + i * 12);
        s.print(GUIDE[i]);
    }

    s.drawFastHLine(4, 98, 120, COL_DIM);

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
    s.setCursor(centerX(s, petName()), 116);
    s.print(petName());

    spr[3].pushSprite(&TKeyboardS3Pro.displayAt(3), 0, 0);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // NVS
    statsLoad();
    petNameLoad();

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS format failed");
    } else {
        Serial.printf("[fs] mounted, %lu/%lu bytes used\n",
                      (unsigned long)LittleFS.usedBytes(),
                      (unsigned long)LittleFS.totalBytes());
        // Try to load the last installed character.
        if (characterInit(nullptr)) {
            gGifAvailable = true;
            gGifMode = true;  // character pack found → always use GIF
        }
    }

    TKeyboardS3Pro.begin();
    TKeyboardS3Pro.setBrightness(gBrightness);
    TKeyboardS3Pro.fillAllScreens(TFT_BLACK);

    // Sprites in PSRAM
    for (int i = 0; i < 4; i++) {
        spr[i].setPsram(true);
        spr[i].setColorDepth(16);
        bool ok = spr[i].createSprite(128, 128);
        Serial.printf("[spr] %d: %s (heap free: %u)\n", i, ok ? "OK" : "FAIL", ESP.getFreeHeap());
        if (!ok) {
            // PSRAM unavailable — fall back to internal RAM
            spr[i].setPsram(false);
            spr[i].createSprite(128, 128);
        }
        spr[i].fillScreen(TFT_BLACK);
    }

    // Rainbow boot sweep
    for (int h = 0; h < 360; h += 30) {
        TKeyboardS3Pro.setLeds(h, 80, 30);
        delay(40);
    }
    TKeyboardS3Pro.setLeds(0, 0, 0);

    bleInit("TKB-S3-Buddy");

    // Initial draw
    gState.connected = false;
    strncpy(gState.msg, "No Claude connected", sizeof(gState.msg) - 1);
    drawCharacter(P_SLEEP);
    drawStatus();
    drawTranscript();
    drawTokens();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
    TKeyboardS3Pro.update();

    // ---- Encoder → brightness ----
    {
        static long _lastEncPos = 0;
        long pos = TKeyboardS3Pro.encoderPosition();
        long delta = pos - _lastEncPos;
        if (delta != 0) {
            _lastEncPos = pos;
            int v = (int)gBrightness + (int)(delta * 26);  // ~10% per detent
            if (v < 5)   v = 5;
            if (v > 255) v = 255;
            gBrightness = (uint8_t)v;
            TKeyboardS3Pro.setBrightness(gBrightness);
        }
    }

    // ---- Key handling ----
    if (TKeyboardS3Pro.key(0).wasClicked()) {   // KEY1 — approve
        if (gState.promptId[0]) {
            uint32_t elapsed = _promptArrivedMs ? (millis() - _promptArrivedMs) / 1000 : 0;
            statsOnApproval(elapsed);
            buddySendPermission(gState.promptId, true);
            gState.promptId[0] = 0;
            triggerOneShot(P_HEART, 3000);
        }
    }
    if (TKeyboardS3Pro.key(1).wasClicked()) {   // KEY2 — deny
        if (gState.promptId[0]) {
            statsOnDenial();
            buddySendPermission(gState.promptId, false);
            gState.promptId[0] = 0;
            triggerOneShot(P_DIZZY, 2000);
        }
    }
    if (TKeyboardS3Pro.key(2).wasClicked()) {   // KEY3 — refresh stats panel
        drawTokens();
    }
    if (TKeyboardS3Pro.key(4).wasClicked()) {   // KEY5 — reset brightness to 70%
        gBrightness = 178;
        TKeyboardS3Pro.setBrightness(gBrightness);
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

    // ---- Track prompt arrival for velocity ----
    static bool _hadPrompt = false;
    if (gState.promptId[0] && !_hadPrompt) _promptArrivedMs = millis();
    _hadPrompt = gState.promptId[0] != 0;

    // ---- One-shot triggers ----
    if (gState.recentlyCompleted && !_wasCompleted) triggerOneShot(P_CELEBRATE, 4000);
    _wasCompleted = gState.recentlyCompleted;

    if (statsPollLevelUp()) triggerOneShot(P_CELEBRATE, 5000);

    if (gState.tokensToday / 50000 > _lastTokens / 50000 && gState.tokensToday > 0)
        triggerOneShot(P_CELEBRATE, 3000);
    _lastTokens = gState.tokensToday;

    // ---- Persona resolution ----
    PersonaState persona = (millis() < _oneShotEnd) ? _oneShotState : derivePersona(gState);

    // Sync GIF state to persona
    if (gGifMode && gGifAvailable) {
        characterSetState((uint8_t)persona);
        characterTick();  // draws into spr[0]
    }

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

        drawCharacter(persona);

        static uint8_t _drawDiv = 0;
        if (++_drawDiv >= 4) {  // text panels at ~5 fps
            _drawDiv = 0;
            drawStatus();
            drawTranscript();
            drawTokens();
        }
    }

    delay(1);
}
