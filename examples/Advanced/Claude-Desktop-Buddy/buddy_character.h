#pragma once
// buddy_character.h — LittleFS + AnimatedGIF character renderer.
// Ported from claude-desktop-buddy/src/character.h/.cpp.
// Targets spr[0] (128x128 LGFX_Sprite, the cat panel).

#include <stdint.h>
#include <LovyanGFX.hpp>

struct BuddyPalette {
    uint16_t body, bg, text, textDim, ink;
};

// Mount LittleFS and load /characters/<name>/manifest.json.
// Pass nullptr to auto-detect the first installed character.
bool characterInit(const char* name = nullptr);
bool characterLoaded();
void characterClose();
void characterInvalidate();

// Set the active animation state (0=sleep .. 6=heart).
void characterSetState(uint8_t state);

// Advance frame timing; call every loop(). Draws into spr[0].
void characterTick();

// Render a half-scale snapshot to an arbitrary LovyanGFX target
// (used for the status panel header). cx/cy = centre point.
void characterRenderTo(lgfx::LovyanGFX* tgt, int cx, int cy);

const BuddyPalette& characterPalette();
