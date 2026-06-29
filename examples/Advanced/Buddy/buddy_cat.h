#pragma once
// buddy_cat.h — ASCII cat animations for the 128x128 GC9107 panels.
// All drawing targets an LGFX_Sprite (128x128). The caller pushSprite()s
// to the physical Display after drawing — no intermediate black frame.

#include <TKeyboardS3Pro.h>

#define BC_BG      0x0000
#define BC_WHITE   0xFFFF
#define BC_DIM     0x8410
#define BC_YEL     0xFFE0
#define BC_HEART   0xF800
#define BC_CYAN    0x07FF
#define BC_GREEN   0x07E0
#define BC_ORANGE  0xFC60
#define BC_BODY    0xC2A6

// Layout constants
static const int CAT_X   = 28;   // left edge of 12-char (72px) sprite
static const int CAT_Y   = 36;   // top of 5-row body
static const int OVL_Y   = 20;   // overlay row (Zzz, !, hearts)
static const int STAT_Y  = 112;

// Clear only the cat + overlay area, preserving the status indicators drawn
// by the caller (bottom 8px handled separately).
static void _catClear(LGFX_Sprite& s) {
    s.fillRect(0, OVL_Y - 2, 128, 100, BC_BG);
}

static void _catLine(LGFX_Sprite& s, int x, int y, const char* txt, uint16_t col) {
    s.setTextColor(col, BC_BG);
    s.setCursor(x, y);
    s.print(txt);
}

static void _catSprite(LGFX_Sprite& s, const char* const* P, uint16_t col,
                       int yOff = 0, int xOff = 0) {
    for (int r = 0; r < 5; r++)
        _catLine(s, CAT_X + xOff, CAT_Y + yOff + r * 8, P[r], col);
}

// ============================================================== SLEEP =======
static void catSleep(LGFX_Sprite& s, uint32_t t) {
    static const char* const LOAF[5]    = { "            ", "            ", "   .-..-.   ", "  ( -.- )   ", "  `------`~ " };
    static const char* const BREATHE[5] = { "            ", "            ", "   .-..-.   ", "  ( -.- )_  ", " `~------'~ " };
    static const char* const PURR[5]    = { "            ", "            ", "   .-..-.   ", "  ( u.u )   ", " `~------'~ " };

    static const uint8_t SEQ[] = { 0,1,0,1,0,1, 2,2,0,1, 0,1,0,1 };
    const char* const* P[3] = { LOAF, BREATHE, PURR };
    _catClear(s);
    _catSprite(s, P[SEQ[(t / 6) % sizeof(SEQ)]], BC_BODY);

    int p1 = t % 10, p2 = (t + 4) % 10;
    s.setTextColor(BC_DIM, BC_BG);
    s.setCursor(CAT_X + 50 + p1, OVL_Y + 8 - p1); s.print("z");
    s.setTextColor(BC_WHITE, BC_BG);
    s.setCursor(CAT_X + 56 + p2, OVL_Y + 4 - p2 / 2); s.print("Z");
}

// ============================================================== IDLE ========
static void catIdle(LGFX_Sprite& s, uint32_t t) {
    static const char* const REST[5]   = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const BLINK[5]  = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const LOOK_L[5] = { "            ", "   /\\_/\\    ", "  (o    o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const LOOK_R[5] = { "            ", "   /\\_/\\    ", "  ( o    o) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const TAIL_L[5] = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")~  " };
    static const char* const GROOM[5]  = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  P   )  ", "  (\")_(\")   " };

    static const uint8_t SEQ[] = { 0,0,0,1,0,2,0,3, 4,4,0, 5,5,5,0, 0,0,1,0 };
    const char* const* P[6] = { REST, BLINK, LOOK_L, LOOK_R, TAIL_L, GROOM };
    _catClear(s);
    _catSprite(s, P[SEQ[(t / 5) % sizeof(SEQ)]], BC_BODY);
}

// ============================================================== BUSY ========
static void catBusy(LGFX_Sprite& s, uint32_t t) {
    static const char* const STARE[5]  = { "            ", "   /\\_/\\    ", "  ( O   O ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const PAW_UP[5] = { "      .     ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )/ ", "  (\")_(\")   " };
    static const char* const PAW_TP[5] = { "    .       ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )_ ", "  (\")_(\")   " };
    static const char* const NUDGE[5]  = { "    o       ", "   /\\_/\\    ", "  ( o   o ) ", "  ( -w   )  ", "  (\")_(\")   " };
    static const char* const SMUG[5]   = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };

    static const uint8_t SEQ[] = { 0,0, 1,2,1,2, 3,3,3, 4,4, 0,0, 1,2,1,2 };
    const char* const* P[5] = { STARE, PAW_UP, PAW_TP, NUDGE, SMUG };
    _catClear(s);
    _catSprite(s, P[SEQ[(t / 5) % sizeof(SEQ)]], BC_BODY);

    static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  ." };
    s.setTextColor(BC_WHITE, BC_BG);
    s.setCursor(CAT_X + 62, OVL_Y + 10);
    s.print(DOTS[t % 5]);
}

// ============================================================== ATTENTION ===
static void catAttention(LGFX_Sprite& s, uint32_t t) {
    static const char* const ALERT[5]  = { "            ", "   /^_^\\    ", "  ( O   O ) ", "  (  v   )  ", "  (\")_(\")   " };
    static const char* const SCAN_L[5] = { "            ", "   /^_^\\    ", "  (O    O ) ", "  (  v   )  ", "  (\")_(\")   " };
    static const char* const SCAN_R[5] = { "            ", "   /^_^\\    ", "  ( O    O) ", "  (  v   )  ", "  (\")_(\")   " };
    static const char* const HISS[5]   = { "            ", "   /^_^\\    ", "  ( O   O ) ", "  (  >   )  ", "  (\")_(\")   " };

    static const uint8_t SEQ[] = { 0,1,0,2,0,1,0,2, 3,0, 0,0,1,2 };
    const char* const* P[4] = { ALERT, SCAN_L, SCAN_R, HISS };
    uint8_t pose = SEQ[(t / 5) % sizeof(SEQ)];
    int xOff = (pose == 3) ? ((t & 1) ? 1 : -1) : 0;
    _catClear(s);
    _catSprite(s, P[pose], BC_BODY, 0, xOff);

    if ((t / 2) & 1) { s.setTextColor(BC_YEL, BC_BG); s.setCursor(CAT_X + 28, OVL_Y);     s.print("!"); }
    if ((t / 3) & 1) { s.setTextColor(BC_YEL, BC_BG); s.setCursor(CAT_X + 40, OVL_Y + 6); s.print("!"); }
}

// ============================================================== CELEBRATE ===
static void catCelebrate(LGFX_Sprite& s, uint32_t t) {
    static const char* const CROUCH[5] = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  W   )  ", " /(\")_(\")\\  " };
    static const char* const JUMP[5]   = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( ^   ^ ) ", "  (  W   )  ", "  (\")_(\")   " };
    static const char* const PEAK[5]   = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( * * * ) ", "  (  W   )  ", "  (\")_(\")~  " };
    static const char* const POSE[5]   = { "    \\o/     ", "   /\\_/\\    ", "  ( ^   ^ ) ", " /(  W   )\\ ", "  (\")_(\")   " };

    static const uint8_t SEQ[]  = { 0,1,2,1,0, 0,1,2,1,0, 3,3 };
    static const int8_t  Y_SH[] = { 0,-3,-6,-3,0, 0,-3,-6,-3,0, 0,0 };
    uint8_t beat = (t / 3) % sizeof(SEQ);
    const char* const* P[4] = { CROUCH, JUMP, PEAK, POSE };
    _catClear(s);
    _catSprite(s, P[SEQ[beat]], BC_BODY, Y_SH[beat]);

    static const uint16_t cols[] = { BC_YEL, BC_HEART, BC_CYAN, BC_WHITE, BC_GREEN };
    for (int i = 0; i < 5; i++) {
        int phase = (t * 2 + i * 9) % 20;
        int x = 10 + i * 22, y = 14 + phase;
        if (y > 60) continue;
        s.setTextColor(cols[i % 5], BC_BG);
        s.setCursor(x, y);
        s.print((i + (int)(t / 2)) & 1 ? "*" : ".");
    }
}

// ============================================================== DIZZY =======
static void catDizzy(LGFX_Sprite& s, uint32_t t) {
    static const char* const TILT_L[5] = { "            ", "  /\\_/\\     ", " ( @   @ )  ", " (   ~~  )  ", " (\")_(\")    " };
    static const char* const TILT_R[5] = { "            ", "    /\\_/\\   ", "  ( @   @ ) ", "  (  ~~  )  ", "    (\")_(\") " };
    static const char* const WOOZY[5]  = { "            ", "   /\\_/\\    ", "  ( x   @ ) ", "  (  v   )  ", "  (\")_(\")~  " };
    static const char* const SPLAT[5]  = { "            ", "   /\\_/\\    ", "  ( @   @ ) ", "  (  -   )  ", " /(\")_(\")\\~ " };

    static const uint8_t SEQ[]  = { 0,1,0,1, 2,2, 0,1,0,1, 3,3 };
    static const int8_t  X_SH[] = { -2,2,-2,2, 0,0, -2,2,-2,2, 0,0 };
    uint8_t beat = (t / 4) % sizeof(SEQ);
    const char* const* P[4] = { TILT_L, TILT_R, WOOZY, SPLAT };
    _catClear(s);
    _catSprite(s, P[SEQ[beat]], BC_BODY, 0, X_SH[beat]);

    static const int8_t OX[] = { 0,4,6,4,0,-4,-6,-4 };
    static const int8_t OY[] = { -4,-2,0,2,4,2,0,-2 };
    uint8_t p1 = t % 8, p2 = (t + 4) % 8;
    s.setTextColor(BC_CYAN, BC_BG); s.setCursor(CAT_X + 36 + OX[p1], OVL_Y + 10 + OY[p1]); s.print("*");
    s.setTextColor(BC_YEL,  BC_BG); s.setCursor(CAT_X + 36 + OX[p2], OVL_Y + 10 + OY[p2]); s.print("*");
}

// ============================================================== HEART =======
static void catHeart(LGFX_Sprite& s, uint32_t t) {
    static const char* const DREAMY[5] = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  u   )  ", "  (\")_(\")~  " };
    static const char* const BLUSH[5]  = { "            ", "   /\\_/\\    ", "  (#^   ^#) ", "  (  u   )  ", "  (\")_(\")   " };
    static const char* const HRT_E[5]  = { "            ", "   /\\_/\\    ", "  ( <3 <3 ) ", "  (  u   )  ", "  (\")_(\")~  " };
    static const char* const PURR[5]   = { "            ", "   /\\-/\\    ", "  ( ~   ~ ) ", "  (  u   )  ", " ~(\")_(\")~  " };

    static const uint8_t SEQ[]  = { 0,0,1,0, 2,2,0, 1,0, 0,3,3, 0,1,0,2 };
    static const int8_t  Y_BOB[]= { 0,-1,0,-1, 0,-1,0, -1,0, 0,0,0, -1,0,-1,0 };
    uint8_t beat = (t / 5) % sizeof(SEQ);
    const char* const* P[4] = { DREAMY, BLUSH, HRT_E, PURR };
    _catClear(s);
    _catSprite(s, P[SEQ[beat]], BC_BODY, Y_BOB[beat]);

    s.setTextColor(BC_HEART, BC_BG);
    for (int i = 0; i < 4; i++) {
        int phase = (t + i * 5) % 14;
        int y = OVL_Y + 14 - phase;
        if (y < 0 || y > CAT_Y) continue;
        int x = 14 + i * 24 + ((phase / 3) & 1) * 2;
        s.setCursor(x, y); s.print("v");
    }
}
