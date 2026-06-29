#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// RotaryEncoder — quadrature decoder for the optional knob that can replace the
// 5th screen. The A/B lines are plain ESP32 GPIOs (KNOB_DATA_A / KNOB_DATA_B),
// read directly here rather than through the STM32. The detent state machine is
// ported from the official T-Keyboard-S3-Pro Keyboard example: one count is
// produced per full detent-to-detent transition, with the direction resolved at
// the intermediate (half-step) state.
// ---------------------------------------------------------------------------
class RotaryEncoder {
public:
    void begin(int pinA, int pinB);

    // Poll the lines. Returns the count change this call (-1, 0 or +1) and folds
    // it into the running position(). Call frequently (e.g. every few ms).
    int update();

    long position() const { return _position; }
    void setPosition(long p) { _position = p; }

    // Consume the accumulated movement since the last call to delta().
    long delta();

private:
    int     _pinA = -1;
    int     _pinB = -1;
    uint8_t _prevDetent  = 0;   // last settled detent code (0b00 or 0b11)
    int8_t  _pendingDir  = 0;   // direction sensed at the half-step
    bool    _pendingValid = false;
    long    _position    = 0;
    long    _accum       = 0;   // movement not yet consumed by delta()
};
