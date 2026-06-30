#pragma once
// buddy_stats.h — NVS-backed persistent stats for T-Keyboard-S3-Pro Buddy.
// Direct port of claude-desktop-buddy/src/stats.h, stripped of IMU/RTC deps.
// Include from exactly one translation unit (Buddy.ino).

#include <Arduino.h>
#include <Preferences.h>

static const uint32_t TOKENS_PER_LEVEL = 50000;

struct BuddyStats {
    uint32_t napSeconds;
    uint16_t approvals;
    uint16_t denials;
    uint16_t velocity[8];   // ring buffer: seconds-to-respond per approval
    uint8_t  velIdx;
    uint8_t  velCount;
    uint8_t  level;
    uint32_t tokens;
};

static BuddyStats  _bStats   = {};
static Preferences _bPrefs;
static bool        _bDirty   = false;

inline void statsLoad() {
    _bPrefs.begin("buddy", true);
    _bStats.napSeconds = _bPrefs.getUInt("nap", 0);
    _bStats.approvals  = _bPrefs.getUShort("appr", 0);
    _bStats.denials    = _bPrefs.getUShort("deny", 0);
    _bStats.velIdx     = _bPrefs.getUChar("vidx", 0);
    _bStats.velCount   = _bPrefs.getUChar("vcnt", 0);
    _bStats.level      = _bPrefs.getUChar("lvl", 0);
    _bStats.tokens     = _bPrefs.getUInt("tok", 0);
    size_t got = _bPrefs.getBytes("vel", _bStats.velocity, sizeof(_bStats.velocity));
    if (got != sizeof(_bStats.velocity)) memset(_bStats.velocity, 0, sizeof(_bStats.velocity));
    _bPrefs.end();
    if (_bStats.tokens == 0 && _bStats.level > 0)
        _bStats.tokens = (uint32_t)_bStats.level * TOKENS_PER_LEVEL;
}

inline void statsSave() {
    if (!_bDirty) return;
    _bPrefs.begin("buddy", false);
    _bPrefs.putUInt("nap",    _bStats.napSeconds);
    _bPrefs.putUShort("appr", _bStats.approvals);
    _bPrefs.putUShort("deny", _bStats.denials);
    _bPrefs.putUChar("vidx",  _bStats.velIdx);
    _bPrefs.putUChar("vcnt",  _bStats.velCount);
    _bPrefs.putUChar("lvl",   _bStats.level);
    _bPrefs.putUInt("tok",    _bStats.tokens);
    _bPrefs.putBytes("vel",   _bStats.velocity, sizeof(_bStats.velocity));
    _bPrefs.end();
    _bDirty = false;
}

inline void statsOnApproval(uint32_t secondsToRespond) {
    _bStats.approvals++;
    _bStats.velocity[_bStats.velIdx] = (uint16_t)min(secondsToRespond, (uint32_t)65535u);
    _bStats.velIdx  = (_bStats.velIdx + 1) % 8;
    if (_bStats.velCount < 8) _bStats.velCount++;
    _bDirty = true; statsSave();
}

inline void statsOnDenial() { _bStats.denials++; _bDirty = true; statsSave(); }

static uint32_t _bLastBridgeTokens = 0;
static bool     _bTokensSynced     = false;
static bool     _bLevelUpPending   = false;

inline void statsOnBridgeTokens(uint32_t bridgeTotal) {
    if (!_bTokensSynced) { _bLastBridgeTokens = bridgeTotal; _bTokensSynced = true; return; }
    if (bridgeTotal < _bLastBridgeTokens) { _bLastBridgeTokens = bridgeTotal; return; }
    uint32_t delta = bridgeTotal - _bLastBridgeTokens;
    _bLastBridgeTokens = bridgeTotal;
    if (delta == 0) return;
    uint8_t lvlBefore = (uint8_t)(_bStats.tokens / TOKENS_PER_LEVEL);
    _bStats.tokens += delta;
    uint8_t lvlAfter  = (uint8_t)(_bStats.tokens / TOKENS_PER_LEVEL);
    if (lvlAfter > lvlBefore) {
        _bStats.level = lvlAfter;
        _bLevelUpPending = true;
        _bDirty = true; statsSave();
    }
}

inline bool statsPollLevelUp() { bool r = _bLevelUpPending; _bLevelUpPending = false; return r; }

inline uint16_t statsMedianVelocity() {
    if (_bStats.velCount == 0) return 0;
    uint16_t tmp[8]; memcpy(tmp, _bStats.velocity, sizeof(tmp));
    uint8_t n = _bStats.velCount;
    for (uint8_t i = 1; i < n; i++) {
        uint16_t k = tmp[i]; int8_t j = i - 1;
        while (j >= 0 && tmp[j] > k) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = k;
    }
    return tmp[n / 2];
}

inline uint8_t statsMoodTier() {
    uint16_t vel = statsMedianVelocity();
    int8_t tier;
    if      (vel == 0)   tier = 2;
    else if (vel < 15)   tier = 4;
    else if (vel < 30)   tier = 3;
    else if (vel < 60)   tier = 2;
    else if (vel < 120)  tier = 1;
    else                 tier = 0;
    uint16_t a = _bStats.approvals, d = _bStats.denials;
    if (a + d >= 3) {
        if (d > a)        tier -= 2;
        else if (d*2 > a) tier -= 1;
    }
    if (tier < 0) tier = 0;
    return (uint8_t)tier;
}

inline uint8_t statsFedProgress() {
    return (uint8_t)((_bStats.tokens % TOKENS_PER_LEVEL) / (TOKENS_PER_LEVEL / 10));
}

inline const BuddyStats& stats() { return _bStats; }

// ---- Pet / owner names -------------------------------------------------------
static char _bPetName[24]   = "Buddy";
static char _bOwnerName[32] = "";

static void _bSafeCopy(char* dst, size_t len, const char* src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < len - 1; i++) {
        char c = src[i];
        if (c != '"' && c != '\\' && c >= 0x20) dst[j++] = c;
    }
    dst[j] = 0;
}

inline void petNameLoad() {
    _bPrefs.begin("buddy", true);
    _bPrefs.getString("petname", _bPetName,   sizeof(_bPetName));
    _bPrefs.getString("owner",   _bOwnerName, sizeof(_bOwnerName));
    _bPrefs.end();
}

inline void petNameSet(const char* name) {
    _bSafeCopy(_bPetName, sizeof(_bPetName), name);
    _bPrefs.begin("buddy", false);
    _bPrefs.putString("petname", _bPetName);
    _bPrefs.end();
}

inline const char* petName()  { return _bPetName;   }
inline const char* ownerName(){ return _bOwnerName; }

inline void ownerSet(const char* name) {
    _bSafeCopy(_bOwnerName, sizeof(_bOwnerName), name);
    _bPrefs.begin("buddy", false);
    _bPrefs.putString("owner", _bOwnerName);
    _bPrefs.end();
}

// ---- Species preference ------------------------------------------------------
inline uint8_t speciesIdxLoad() {
    _bPrefs.begin("buddy", true);
    uint8_t v = _bPrefs.getUChar("species", 0xFF);
    _bPrefs.end();
    return v;
}

inline void speciesIdxSave(uint8_t idx) {
    _bPrefs.begin("buddy", false);
    _bPrefs.putUChar("species", idx);
    _bPrefs.end();
}
