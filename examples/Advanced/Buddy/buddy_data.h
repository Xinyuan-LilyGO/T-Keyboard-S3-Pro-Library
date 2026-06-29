#pragma once
// buddy_data.h — JSON state parser for T-Keyboard-S3-Pro Buddy.
// Reads newline-delimited JSON from USB Serial (same wire protocol as
// claude-desktop-buddy). No BLE in this file; see buddy_ble.h.
//
// Wire protocol (desktop → device):
//   {"total":N,"running":N,"waiting":N,"completed":bool,"tokens_today":N,
//    "msg":"...","entries":["line",...],
//    "prompt":{"id":"...","tool":"...","hint":"..."}}
//
// Commands (device → desktop):
//   {"cmd":"permission","id":"...","decision":"once"}
//   {"cmd":"permission","id":"...","decision":"deny"}

#include <Arduino.h>
#include <ArduinoJson.h>

struct BuddyState {
    uint8_t  sessionsTotal;
    uint8_t  sessionsRunning;
    uint8_t  sessionsWaiting;
    bool     recentlyCompleted;
    uint32_t tokensToday;
    char     msg[32];
    bool     connected;

    // Transcript lines (word-wrapped by desktop, up to 6 fit on 128px)
    char     lines[6][48];
    uint8_t  nLines;
    uint16_t lineGen;   // bumps when lines change → UI resets scroll

    // Pending permission prompt
    char     promptId[40];
    char     promptTool[20];
    char     promptHint[44];
};

// ---------------------------------------------------------------------------
static uint32_t _buddyLastLiveMs = 0;

inline bool buddyDataConnected() {
    return _buddyLastLiveMs != 0 && (millis() - _buddyLastLiveMs) <= 30000;
}

static void _buddyApplyJson(const char* line, BuddyState* out) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;

    // Time sync — desktop sends {"time":[epoch_sec, tz_offset_sec]}
    JsonArray t = doc["time"];
    if (!t.isNull() && t.size() == 2) {
        _buddyLastLiveMs = millis();
        return; // no RTC on T-Keyboard-S3-Pro, just mark alive
    }

    out->sessionsTotal     = doc["total"]     | out->sessionsTotal;
    out->sessionsRunning   = doc["running"]   | out->sessionsRunning;
    out->sessionsWaiting   = doc["waiting"]   | out->sessionsWaiting;
    out->recentlyCompleted = doc["completed"] | false;
    out->tokensToday       = doc["tokens_today"] | out->tokensToday;

    const char* m = doc["msg"];
    if (m) {
        strncpy(out->msg, m, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = 0;
    }

    JsonArray la = doc["entries"];
    if (!la.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : la) {
            if (n >= 6) break;
            const char* s = v.as<const char*>();
            strncpy(out->lines[n], s ? s : "", 47);
            out->lines[n][47] = 0;
            n++;
        }
        if (n != out->nLines) out->lineGen++;
        out->nLines = n;
    }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
        const char* pid = pr["id"];
        const char* pt  = pr["tool"];
        const char* ph  = pr["hint"];
        strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId)   - 1); out->promptId[sizeof(out->promptId)-1]     = 0;
        strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool) - 1); out->promptTool[sizeof(out->promptTool)-1] = 0;
        strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint) - 1); out->promptHint[sizeof(out->promptHint)-1] = 0;
    } else {
        out->promptId[0] = 0;
        out->promptTool[0] = 0;
        out->promptHint[0] = 0;
    }

    _buddyLastLiveMs = millis();
}

// Line-buffered USB serial reader — call every loop().
template<size_t N>
struct _BuddyLineBuf {
    char     buf[N];
    uint16_t len = 0;
    void feed(Stream& s, BuddyState* out) {
        while (s.available()) {
            char c = s.read();
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    buf[len] = 0;
                    if (buf[0] == '{') _buddyApplyJson(buf, out);
                    len = 0;
                }
            } else if (len < N - 1) {
                buf[len++] = c;
            }
        }
    }
};

static _BuddyLineBuf<512> _buddyUsbLine;

inline void buddyDataPoll(BuddyState* out) {
    _buddyUsbLine.feed(Serial, out);
    out->connected = buddyDataConnected();
    if (!out->connected) {
        out->sessionsTotal = out->sessionsRunning = out->sessionsWaiting = 0;
        out->recentlyCompleted = false;
        strncpy(out->msg, "No Claude connected", sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = 0;
    }
}

// Send a permission decision back to the desktop.
inline void buddySendPermission(const char* promptId, bool approve) {
    Serial.printf("{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
                  promptId, approve ? "once" : "deny");
}
