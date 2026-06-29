# LILYGO T-Keyboard-S3-Pro Library

Arduino driver library for the **LILYGO T-Keyboard-S3-Pro** (ESP32-S3). A single
unified class (`TKeyboardS3ProClass`) reached through a global singleton
(`TKeyboardS3Pro`) — the same shape as the original
[T-Keyboard-S3 library](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3),
adapted to the Pro's hardware.

## Hardware

| Block            | Detail |
| ---------------- | ------ |
| Main MCU         | ESP32-S3 (16 MB Flash, 8 MB OPI PSRAM) |
| Co-processor     | STM32G030F6 — reached over I2C, multiplexes the panel CS lines, reads the keys and drives the LEDs |
| Displays         | GC9107 128×128 IPS (N085-1212TBWIG06-C08) on a shared SPI bus — **4** on the host, **5** on an expansion module |
| Keys             | **5** hot-swappable keys (read over I2C) |
| RGB              | **14** × WS2812C (driven by the STM32 over I2C) |
| Rotary encoder   | host only — occupies the host's CS5 slot (read on ESP32 GPIO) |
| Chaining         | up to **6** boards magnetically chained on one I2C bus, each with its own address |

> **Panels per board:** the host (I2C address `0x01`) has **4 panels**, on
> **CS1..CS4** — its **CS5** slot holds the rotary encoder, not a panel. Each
> expansion module has **5 panels** (CS1..CS5). Host panels are exposed as
> `display1`..`display4`; `displayAt(index, address)` /
> `panelMask(index, address)` handle indexed access; `screenCount(address)`
> returns the panel count for a board.

The decisive difference from the original T-Keyboard-S3: the panels' chip-select
lines, the keys and the LEDs are **not** wired to the ESP32 — they hang off the
STM32 co-processor and are accessed over I2C. Each `Display` object is a full
LovyanGFX device whose `cs_control()` selects the right STM32-owned CS line
automatically while drawing.

### Pins (ESP32-S3)

- **STM32 I2C bus:** SDA=42, SCL=2 (secondary header: SDA=6, SCL=7)
- **Display (shared SPI):** MOSI=40, SCLK=41, DC=39, RST=38, BL=1
- **Rotary encoder:** A=4, B=5

## Install

Copy this folder into your Arduino `libraries/` directory, or add it as a
PlatformIO `lib_deps` entry. Requires:

- esp32 Arduino core 3.x (ESP32-S3)
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [ButtonSense](https://github.com/lbuque/ButtonSense)

## Usage

```cpp
#include <TKeyboardS3Pro.h>

void setup() {
    Serial.begin(115200);
    TKeyboardS3Pro.begin();

    TKeyboardS3Pro.display1.fillScreen(TFT_RED);      // host panel 1 only
    TKeyboardS3Pro.display2.drawString("hi", 0, 0);   // host panel 2 only

    TKeyboardS3Pro.fillAllScreens(TFT_BLACK);         // all panels, one at a time
}

void loop() {
    TKeyboardS3Pro.update();                          // poll keys + encoder
    if (TKeyboardS3Pro.key(0).wasPressed()) {         // KEY1 on the host
        Serial.println("KEY1");
    }
}
```

### Displays

Each panel shares the bus; the STM32 decides which one listens. `address`
defaults to the host (`0x01`).

- `display1`..`display4` — the host panels as independent LovyanGFX devices.
  Draw on them directly; their overridden `cs_control()` selects/releases the
  STM32-owned CS automatically.
- `displayAt(index, address)` — access **one** physical panel (0-based) anywhere
  in the cluster. Host panels return `display1`..`display4`; expansion panels use
  a retargeted dynamic display object.
- `screen(index, address)` — backward-compatible alias for `displayAt()`.
- `screenCount(address)` — panels on that board (4 for the host, 5 for a module).
- `fillAllScreens(color)` — fill every panel on every board (drawn one at a time).
- `selectScreen(mask, address)` — low-level raw CS write for one board. Selecting
  several panels at once (`SCREEN_ALL`) is **unreliable** on this hardware — use
  `displayAt()` / one panel at a time for drawing.
- `display` — dynamic/compatibility LovyanGFX device used by `selectScreen()`.
- `setBrightness(0..255)` — shared backlight.

```cpp
TKeyboardS3Pro.displayAt(0).fillScreen(TFT_RED);          // host panel 0 (CS1)
TKeyboardS3Pro.displayAt(4, 0x02).fillScreen(TFT_BLUE);   // panel 5 of module 0x02
```

> **Why one panel at a time?** The panels' chip-select is multiplexed by the STM32
> over I2C. Two quirks shaped this driver: (1) the STM32 latches a CS-register
> write only when the next write arrives, so selections are flushed with a double
> write; (2) the STM32G030 mishandles LCD-CS writes at 1 MHz I2C, so the bus stays
> at a reliable 400 kHz. For drawing, use one Display object at a time. The
> shared SPI bus runs at 20 MHz when only one I2C board is detected, and falls
> back to 4.5 MHz when multiple chained boards are detected.

### Keys

- `update()` — call from `loop()`; refreshes the host key state and the encoder.
- `key(i)` — ButtonSense object for host keys, `i` = 0..4 (KEY1..KEY5): use
  `key(i).isPressed()`, `key(i).wasPressed()`, `key(i).wasReleased()`, click /
  double-click / hold helpers, callbacks and thresholds.
- `keyManager()` — combo detection across the five host keys.
- `keys(address)` — live raw 5-bit read of any board (for chained boards).

### RGB LEDs

- `setLeds(hue, saturation, brightness, ledMask = all, address)` — one-call HSV paint (hue 0..360, sat 0..100, brightness 0..100; `ledMask` bit0=LED1 … bit13=LED14).
- Low-level: `ledSetMode`, `ledSetColor`, `ledSetBrightness`, `ledSelect`, `ledClear`, `ledShow`.

> When several boards are chained, keep the brightness low (~10): 6 × 14 LEDs at
> full brightness exceeds what the USB rail can comfortably supply.

### Rotary encoder

- `encoder.delta()` — count change since the last read.
- `encoderPosition()` — running detent count.

### Chained boards

Each board answers on its own I2C address; the host is `0x01`. Draw to any panel
with `displayAt(index, address)` (see Displays above).

- `devices()` / `deviceCount()` — boards known since `begin()` / `refreshDevices()`.
- `refreshDevices()` — re-scan the bus, update the SPI speed for the detected
  board count, and initialise the panels of any board hot-plugged after
  `begin()`. Returns the new board count.
- `scanDevices()` — live scan, returns the 7-bit addresses present.
- `isDevicePresent(address)`, `firmwareVersion(address)`.

## Examples

- `Basic/Displays`     — label and draw across the host's four panels
- `Basic/Keyboard`    — read the keys, mirror them on the panels and Serial
- `Basic/Encoder`     — read the rotary knob
- `Basic/RGB`         — sweep the 14 WS2812C LEDs
- `Advanced/MultiBoard` — drive the panels across several chained boards

## License

MIT — see [LICENSE](LICENSE).
