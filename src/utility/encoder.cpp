#include "encoder.h"

void RotaryEncoder::begin(int pinA, int pinB)
{
    _pinA = pinA;
    _pinB = pinB;
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);

    // Seed the detent state from the current line levels.
    _prevDetent   = (uint8_t)((digitalRead(_pinA) ? 0B10 : 0) | (digitalRead(_pinB) ? 0B01 : 0));
    _pendingDir   = 0;
    _pendingValid = false;
}

int RotaryEncoder::update()
{
    if (_pinA < 0) return 0;

    uint8_t scan = (uint8_t)((digitalRead(_pinA) ? 0B10 : 0) | (digitalRead(_pinB) ? 0B01 : 0));
    int delta = 0;

    if (scan != _prevDetent) {
        if (scan == 0B00 || scan == 0B11) {
            // Reached a detent: commit the direction sensed at the half-step.
            if (_pendingValid) {
                delta = _pendingDir;
                _position += delta;
                _accum    += delta;
                _pendingValid = false;
            }
            _prevDetent = scan;
        } else {
            // Half-step (0b01 / 0b10): resolve direction from the prior detent.
            if (scan == 0B10) {
                _pendingDir = (_prevDetent == 0B00) ? +1 : (_prevDetent == 0B11 ? -1 : 0);
            } else { // 0B01
                _pendingDir = (_prevDetent == 0B00) ? -1 : (_prevDetent == 0B11 ? +1 : 0);
            }
            _pendingValid = (_pendingDir != 0);
        }
    }

    return delta;
}

long RotaryEncoder::delta()
{
    long d = _accum;
    _accum = 0;
    return d;
}
