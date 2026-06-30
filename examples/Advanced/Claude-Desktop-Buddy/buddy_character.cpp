// buddy_character.cpp — LittleFS + AnimatedGIF renderer for Buddy.
// Ported from claude-desktop-buddy/src/character.cpp.
// Draws into spr[0] (128x128 sprite, cat panel).

#include "buddy_character.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ArduinoJson.h>

// spr[0] is the cat-panel sprite, declared in Buddy.ino.
extern LGFX_Sprite spr[];

static const char* STATE_NAMES[] = {
    "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"
};
static const uint8_t N_STATES = 7;

// ---- State ------------------------------------------------------------------
static bool          _loaded    = false;
static BuddyPalette  _pal       = { 0xC2A6, 0x0000, 0xFFFF, 0x8410, 0x0000 };
static char          _basePath[48];

static const uint8_t MAX_GIFS = 32;
static char     _gifPaths[MAX_GIFS][32];
static uint8_t  _stateStart[N_STATES];
static uint8_t  _stateCount[N_STATES];
static uint8_t  _stateRot[N_STATES];
static uint8_t  _gifTotal  = 0;
static uint8_t  _curState  = 0xFF;

static AnimatedGIF _gif;
static File        _gifFile;
static int         _gifX = 0, _gifY = 0, _gifW = 0, _gifH = 0;
static bool        _gifOpen = false;
static uint32_t    _nextFrameAt     = 0;
static uint32_t    _animPauseUntil  = 0;
static uint32_t    _variantStartMs  = 0;

static const uint32_t VARIANT_DWELL_MS = 5000;
static const uint32_t ANIM_PAUSE_MS    = 800;

// Draw target (defaults to spr[0]; characterRenderTo() retargets temporarily).
static lgfx::LovyanGFX* _tgt     = nullptr;
static bool              _peek    = false;
static const int         PEEK_TOP = 128;  // full panel height on 128px screen

// ---- Helpers ----------------------------------------------------------------
static uint16_t _parseHexColor(const char* s, uint16_t fallback) {
    if (!s) return fallback;
    if (*s == '#') s++;
    uint32_t v = strtoul(s, nullptr, 16);
    return (uint16_t)(((v >> 19) & 0x1F) << 11 | ((v >> 10) & 0x3F) << 5 | ((v >> 3) & 0x1F));
}

static void _gifPlace() {
    int outW = _peek ? _gifW / 2 : _gifW;
    int outH = _peek ? _gifH / 2 : _gifH;
    _gifX = (128 - outW) / 2;
    _gifY = _peek ? (PEEK_TOP - outH) / 2 : (128 - outH) / 2;
}

// ---- AnimatedGIF file callbacks (LittleFS) ----------------------------------
static void* _gifOpenCb(const char* fname, int32_t* pSize) {
    _gifFile = LittleFS.open(fname, "r");
    if (!_gifFile) return nullptr;
    *pSize = _gifFile.size();
    return (void*)&_gifFile;
}

static void _gifCloseCb(void* handle) {
    File* f = (File*)handle;
    if (f) f->close();
}

static int32_t _gifReadCb(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = (File*)pFile->fHandle;
    int32_t n = f->read(pBuf, iLen);
    pFile->iPos = f->position();
    return n;
}

static int32_t _gifSeekCb(GIFFILE* pFile, int32_t iPosition) {
    File* f = (File*)pFile->fHandle;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

// ---- Draw callback: one scanline → pixel draw -------------------------------
static void _gifDrawCb(GIFDRAW* d) {
    if (!_tgt) return;
    uint16_t* pal16 = d->pPalette;
    uint8_t*  src   = d->pPixels;
    uint8_t   t     = d->ucTransparent;
    bool      hasT  = d->ucHasTransparency;
    int       srcY  = d->iY + d->y;

    auto put = [&](int x, int y, uint8_t idx) {
        _tgt->drawPixel(x, y, (hasT && idx == t) ? _pal.bg : pal16[idx]);
    };

    if (_peek) {
        if (srcY & 1) return;
        int y = _gifY + (srcY >> 1);
        if (y < 0 || y >= PEEK_TOP) return;
        int x0 = _gifX + (d->iX >> 1);
        int w  = d->iWidth >> 1;
        for (int i = 0; i < w; i++) put(x0 + i, y, src[i << 1]);
        return;
    }

    int y = _gifY + srcY;
    if (y < 0 || y >= 128) return;
    int x0 = _gifX + d->iX;
    int w  = d->iWidth;
    if (w > 256) w = 256;
    if (x0 < 0) { src -= x0; w += x0; x0 = 0; }
    if (x0 + w > 128) w = 128 - x0;
    if (w <= 0) return;
    for (int i = 0; i < w; i++) put(x0 + i, y, src[i]);
}

// ---- Public API -------------------------------------------------------------
bool characterInit(const char* name) {
    if (!LittleFS.begin(false)) {
        if (!LittleFS.open("/")) {
            Serial.println("[char] LittleFS mount failed");
            return false;
        }
    }

    static char _scanned[24];
    if (!name) {
        File d = LittleFS.open("/characters");
        if (d && d.isDirectory()) {
            File e = d.openNextFile();
            while (e) {
                if (e.isDirectory()) {
                    const char* n = strrchr(e.name(), '/');
                    strncpy(_scanned, n ? n + 1 : e.name(), sizeof(_scanned) - 1);
                    _scanned[sizeof(_scanned) - 1] = 0;
                    name = _scanned;
                    e.close();
                    break;
                }
                e.close();
                e = d.openNextFile();
            }
            d.close();
        }
        if (!name) { Serial.println("[char] no characters installed"); return false; }
    }

    snprintf(_basePath, sizeof(_basePath), "/characters/%s", name);
    char mpath[64];
    snprintf(mpath, sizeof(mpath), "%s/manifest.json", _basePath);

    File mf = LittleFS.open(mpath, "r");
    if (!mf) {
        Serial.printf("[char] manifest not found: %s\n", mpath);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, mf);
    mf.close();
    if (err) {
        Serial.printf("[char] manifest parse: %s\n", err.c_str());
        return false;
    }

    JsonObject colors = doc["colors"];
    _pal.body    = _parseHexColor(colors["body"],    _pal.body);
    _pal.bg      = _parseHexColor(colors["bg"],      _pal.bg);
    _pal.text    = _parseHexColor(colors["text"],    _pal.text);
    _pal.textDim = _parseHexColor(colors["textDim"], _pal.textDim);
    _pal.ink     = _parseHexColor(colors["ink"],     _pal.ink);

    JsonObject states = doc["states"];
    _gifTotal = 0;
    for (uint8_t i = 0; i < N_STATES; i++) {
        _stateStart[i] = _gifTotal;
        _stateCount[i] = 0;
        _stateRot[i]   = 0;
        JsonVariant v = states[STATE_NAMES[i]];
        if (v.is<JsonArray>()) {
            for (JsonVariant e : v.as<JsonArray>()) {
                if (_gifTotal >= MAX_GIFS) break;
                const char* fn = e.as<const char*>();
                if (fn) { snprintf(_gifPaths[_gifTotal], 32, "%s", fn); _gifTotal++; _stateCount[i]++; }
            }
        } else {
            const char* fn = v.as<const char*>();
            if (fn) { snprintf(_gifPaths[_gifTotal], 32, "%s", fn); _gifTotal++; _stateCount[i] = 1; }
        }
    }

    _gif.begin(LITTLE_ENDIAN_PIXELS);
    _loaded = true;
    Serial.printf("[char] loaded '%s' from %s\n", name, _basePath);
    return true;
}

bool characterLoaded() { return _loaded; }
const BuddyPalette& characterPalette() { return _pal; }

void characterClose() {
    if (_gifOpen) { _gif.close(); _gifOpen = false; }
    _loaded = false;
    _curState = 0xFF;
}

void characterInvalidate() {
    if (!_loaded) return;
    if (_gifOpen) { _gif.close(); _gifOpen = false; }
    _animPauseUntil = 0;
    uint8_t s = _curState; _curState = 0xFF;
    characterSetState(s);
}

void characterSetState(uint8_t s) {
    if (!_loaded || s >= N_STATES || s == _curState) return;
    if (_gifOpen) { _gif.close(); _gifOpen = false; }
    _animPauseUntil = 0;
    _curState = s;

    if (_stateCount[s] == 0) {
        Serial.printf("[char] no gif for state %d\n", s);
        return;
    }

    uint8_t idx = _stateStart[s] + _stateRot[s];
    char full[80];
    snprintf(full, sizeof(full), "%s/%s", _basePath, _gifPaths[idx]);

    _tgt = &spr[0];
    if (_gif.open(full, _gifOpenCb, _gifCloseCb, _gifReadCb, _gifSeekCb, _gifDrawCb)) {
        _gifOpen = true;
        _gifW = _gif.getCanvasWidth();
        _gifH = _gif.getCanvasHeight();
        _gifPlace();
        spr[0].fillSprite(_pal.bg);
        _nextFrameAt    = 0;
        _variantStartMs = millis();
        Serial.printf("[char] %s: %dx%d @ (%d,%d)\n",
                      _gifPaths[idx], _gifW, _gifH, _gifX, _gifY);
    } else {
        Serial.printf("[char] open failed: %s (err %d)\n", full, _gif.getLastError());
    }
}

void characterTick() {
    if (!_loaded || !_tgt) return;

    uint32_t now = millis();
    if (!_gifOpen) {
        if (_animPauseUntil && now >= _animPauseUntil) {
            _animPauseUntil = 0;
            uint8_t s = _curState; _curState = 0xFF;
            characterSetState(s);
        }
        return;
    }
    if (now < _nextFrameAt) return;

    _tgt = &spr[0];
    int delayMs = 0;
    if (!_gif.playFrame(false, &delayMs)) {
        if (_stateCount[_curState] == 1) {
            // Single-GIF state: loop by resetting instead of closing.
            _gif.reset();
            _nextFrameAt = now;
            return;
        }
        if (now - _variantStartMs < VARIANT_DWELL_MS) {
            _gif.reset(); _nextFrameAt = now; return;
        }
        _gif.close(); _gifOpen = false;
        _stateRot[_curState] = (_stateRot[_curState] + 1) % _stateCount[_curState];
        _animPauseUntil = now + ANIM_PAUSE_MS;
        return;
    }
    _nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
}

void characterRenderTo(lgfx::LovyanGFX* tgt, int cx, int cy) {
    if (!_gifOpen) return;
    lgfx::LovyanGFX* prevT = _tgt;
    bool prevP = _peek;
    int px = _gifX, py = _gifY;

    _tgt  = tgt;
    _peek = true;
    _gifX = cx - _gifW / 4;
    _gifY = cy - _gifH / 4;

    uint32_t now = millis();
    if (now >= _nextFrameAt) {
        int delayMs = 0;
        if (!_gif.playFrame(false, &delayMs)) { _gif.reset(); _gif.playFrame(false, &delayMs); }
        _nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
    }

    _tgt  = prevT;
    _peek = prevP;
    _gifX = px;
    _gifY = py;
}
