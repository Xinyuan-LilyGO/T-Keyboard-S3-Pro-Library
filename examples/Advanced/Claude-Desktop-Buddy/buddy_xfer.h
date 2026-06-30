#pragma once
// buddy_xfer.h — BLE/Serial GIF character pack transfer protocol.
// Ported from claude-desktop-buddy/src/xfer.h.
// Handles char_begin / file / chunk / file_end / char_end JSON commands.
// Include from exactly one translation unit (Buddy.ino).

#include <Arduino.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <ArduinoJson.h>
#include "buddy_ble.h"
#include "buddy_stats.h"
#include "buddy_character.h"

// bleClearBonds is not in buddy_ble.h (NimBLE core 3.x manages bonds
// automatically); stub it out so the "unpair" command is a no-op.
static inline void bleClearBonds() { BLEDevice::deinit(true); }

static File     _xFile;
static uint32_t _xExpected = 0, _xWritten = 0;
static char     _xCharName[24] = "";
static bool     _xActive  = false;
static uint32_t _xTotal   = 0, _xTotalWritten = 0;

// Send ack to both Serial and BLE.
static void _xAck(const char* what, bool ok, uint32_t n = 0) {
    char b[80];
    int len = snprintf(b, sizeof(b),
        "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}\n",
        what, ok ? "true" : "false", (unsigned long)n);
    Serial.write(b, len);
    bleWrite((const uint8_t*)b, len);
}

static uint32_t _xWipeDir(const char* dir) {
    File d = LittleFS.open(dir);
    if (!d || !d.isDirectory()) { LittleFS.mkdir(dir); return 0; }
    uint32_t freed = 0;
    File f = d.openNextFile();
    while (f) {
        freed += f.size();
        char p[80]; snprintf(p, sizeof(p), "%s/%s", dir, f.name());
        f.close();
        LittleFS.remove(p);
        f = d.openNextFile();
    }
    d.close();
    return freed;
}

static uint32_t _xWipeAllChars() {
    File root = LittleFS.open("/characters");
    if (!root || !root.isDirectory()) { LittleFS.mkdir("/characters"); return 0; }
    uint32_t freed = 0;
    File sub = root.openNextFile();
    while (sub) {
        if (sub.isDirectory()) {
            char p[64]; snprintf(p, sizeof(p), "/characters/%s", sub.name());
            sub.close();
            freed += _xWipeDir(p);
            LittleFS.rmdir(p);
        } else {
            sub.close();
        }
        sub = root.openNextFile();
    }
    root.close();
    return freed;
}

// Declare the globals from Buddy.ino that xferCommand needs to toggle.
extern bool gGifMode;
extern bool gGifAvailable;

// Returns true when the JSON doc was a transfer/control command.
// Caller skips state-update parsing when this returns true.
inline bool xferCommand(JsonDocument& doc) {
    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    if (strcmp(cmd, "name") == 0) {
        const char* n = doc["name"];
        if (n) petNameSet(n);
        _xAck("name", n != nullptr);
        return true;
    }

    if (strcmp(cmd, "owner") == 0) {
        const char* n = doc["name"];
        if (n) ownerSet(n);
        _xAck("owner", n != nullptr);
        return true;
    }

    if (strcmp(cmd, "species") == 0) {
        uint8_t idx = doc["idx"] | 0xFF;
        speciesIdxSave(idx);
        gGifMode = !(gGifAvailable && idx == 0xFF);
        _xAck("species", true);
        return true;
    }

    if (strcmp(cmd, "status") == 0) {
        char b[320];
        int len = snprintf(b, sizeof(b),
            "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
            "\"name\":\"%s\",\"owner\":\"%s\","
            "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
            "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u}"
            "}}\n",
            petName(), ownerName(),
            millis() / 1000, ESP.getFreeHeap(),
            (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()),
            (unsigned long)LittleFS.totalBytes(),
            stats().approvals, stats().denials, statsMedianVelocity(),
            (unsigned long)stats().napSeconds, stats().level
        );
        Serial.write(b, len);
        bleWrite((const uint8_t*)b, len);
        return true;
    }

    if (strcmp(cmd, "unpair") == 0) {
        bleClearBonds();
        _xAck("unpair", true);
        return true;
    }

    if (strcmp(cmd, "char_begin") == 0) {
        const char* name = doc["name"] | "pet";
        _xTotal = doc["total"] | 0;

        // Fit check
        uint32_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
        uint32_t reclaimable = 0;
        {
            File r = LittleFS.open("/characters");
            if (r && r.isDirectory()) {
                File s = r.openNextFile();
                while (s) {
                    if (s.isDirectory()) {
                        File f = s.openNextFile();
                        while (f) { reclaimable += f.size(); f.close(); f = s.openNextFile(); }
                    }
                    s.close(); s = r.openNextFile();
                }
                r.close();
            }
        }
        uint32_t available = freeBytes + reclaimable;
        if (_xTotal > 0 && _xTotal + 4096 > available) {
            char b[96];
            int len = snprintf(b, sizeof(b),
                "{\"ack\":\"char_begin\",\"ok\":false,\"n\":%lu,"
                "\"error\":\"need %luK have %luK\"}\n",
                (unsigned long)available,
                (unsigned long)(_xTotal / 1024),
                (unsigned long)(available / 1024));
            Serial.write(b, len);
            bleWrite((const uint8_t*)b, len);
            return true;
        }

        strncpy(_xCharName, name, sizeof(_xCharName) - 1);
        _xCharName[sizeof(_xCharName) - 1] = 0;
        characterClose();
        _xWipeAllChars();
        char dir[48]; snprintf(dir, sizeof(dir), "/characters/%s", _xCharName);
        LittleFS.mkdir(dir);
        _xTotalWritten = 0;
        _xActive = true;
        _xAck("char_begin", true);
        return true;
    }

    // Commands below require an active transfer.
    if (!_xActive) return (strcmp(cmd, "permission") != 0);

    if (strcmp(cmd, "file") == 0) {
        const char* path = doc["path"];
        _xExpected = doc["size"] | 0;
        _xWritten  = 0;
        if (!path) { _xAck("file", false); return true; }
        char full[80]; snprintf(full, sizeof(full), "/characters/%s/%s", _xCharName, path);
        _xFile = LittleFS.open(full, "w");
        _xAck("file", (bool)_xFile);
        return true;
    }

    if (strcmp(cmd, "chunk") == 0) {
        const char* b64 = doc["d"];
        if (!b64 || !_xFile) { _xAck("chunk", false); return true; }
        uint8_t buf[300];
        size_t outLen = 0;
        int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                       (const uint8_t*)b64, strlen(b64));
        if (rc != 0) { _xAck("chunk", false); return true; }
        _xFile.write(buf, outLen);
        _xWritten      += outLen;
        _xTotalWritten += outLen;
        _xAck("chunk", true, _xWritten);
        return true;
    }

    if (strcmp(cmd, "file_end") == 0) {
        bool ok = _xFile && (_xWritten == _xExpected || _xExpected == 0);
        if (_xFile) _xFile.close();
        _xAck("file_end", ok, _xWritten);
        return true;
    }

    if (strcmp(cmd, "char_end") == 0) {
        _xActive = false;
        bool ok = characterInit(_xCharName);
        if (ok) {
            gGifMode      = true;
            gGifAvailable = true;
            speciesIdxSave(0xFF);
        }
        _xAck("char_end", ok);
        return true;
    }

    return false;
}

inline bool     xferActive()   { return _xActive; }
inline uint32_t xferProgress() { return _xTotalWritten; }
inline uint32_t xferTotal()    { return _xTotal; }
