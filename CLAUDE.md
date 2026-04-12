# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Upload

This is an Arduino IDE project. There is no CLI build system — compilation and upload are done through the Arduino IDE (or `arduino-cli` if installed).

**Required libraries (install via Arduino Library Manager):**
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `FluxGarage RoboEyes`
- `BluetoothSerial` (built-in with the ESP32 Arduino core)
- `Makuna/DFMiniMp3` (`DFMiniMp3.h`) — for DFPlayer Mini control

**Board:** ESP32 (must have Classic Bluetooth enabled — the sketch has a compile-time `#error` guard that enforces this).

**Serial monitor baud rate:** 115200 (for debug output).

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (Classic Bluetooth SPP) |
| Motor driver | L298N dual H-bridge |
| Motors | 2× N20 DC gear motors |
| Display | SSD1306 OLED 128×64 (I2C, address 0x3C) |
| Touch sensor | TTP223 capacitive (digital input) |
| Audio module | DFPlayer Mini (MP3-TF-16P) via UART2 at 9600 baud |
| BT device name | `ESP32-MakersLab-Car` |

**Pin mapping:**

| Signal | GPIO |
|--------|------|
| Motor A IN1 | 32 |
| Motor A IN2 | 33 |
| Motor A ENA (PWM ch 0) | 14 |
| Motor B IN3 | 27 |
| Motor B IN4 | 26 |
| Motor B ENB (PWM ch 1) | 25 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| Touch sensor | 19 |
| DFPlayer RX (from DFPlayer TX) | 16 |
| DFPlayer TX (to DFPlayer RX, 1KΩ resistor) | 17 |

PWM: 1 kHz, 8-bit resolution (0–255).

## Software Architecture

Everything lives in the single `.ino` file (~600 lines). The `loop()` function acts as a cooperative scheduler — it is non-blocking and must never use `delay()`.

**Major subsystems and their interaction:**

1. **Motor control** — `setMotor()` takes a direction + speed, clamps to `[minSpeed, maxSpeed]`, and drives the L298N via `ledcWrite`. Passive braking (both direction pins LOW) on stop.

2. **Bluetooth command processor** — Characters accumulate into a buffer; a newline triggers `processCommand()`. All movement commands reset `lastMoveTime` for the safety watchdog.

3. **Safety watchdog** — If no movement command arrives within `cmdTimeoutMs` (3 s), motors stop automatically. This guards against BT disconnection leaving the robot moving.

4. **OLED / RoboEyes animation** — `roboEyes.update()` is called every loop iteration. Framerate is 100 fps during normal operation and 15 fps during the sleep cycle. Custom "zzz" text overlay is drawn directly on the `display` object and rendered via `display.display()` alongside the RoboEyes frame.

5. **Sleep state machine** — A 4-state cyclic FSM (SLEEP_A → STIR_A → SLEEP_B → STIR_B → repeat) activates 10 s after Bluetooth disconnect. Each state has a fixed duration and its own eye/animation behaviour.

6. **Touch/pet handler** — Debounced edge detection on GPIO 19. Five taps within a 4-second window triggers a 2-second laugh animation. Single-tap hold squints the eyes (height 18 px); release restores them (36 px) after a 3-second cooldown.

7. **DFPlayer Mini audio** — Sound effect player via UART2. Plays tracks from `/mp3/` folder on SD card, triggered by robot behaviour events (pet → track 3, laugh → track 8, sleep → track 9, boot → track 1). Uses Makuna/DFMiniMp3 library with `Mp3ChipIncongruousNoAck` variant (critical: the default `Mp3ChipOriginal` retries commands 3× when ACKs are missing, causing triple playback). `mp3.loop()` is called every loop iteration. All effects use `mp3PlayTrackNow_Interrupt()` to play immediately in sync with eye animations.

**Key state flags:**
- `btConnected` — BT client currently connected
- `eyesSleeping` — sleep FSM is active
- `tapCount` — touch interaction state
- `currentSpeed` — global speed applied to all directional commands until changed

## Bluetooth Protocol

Commands are newline-terminated ASCII strings sent over Classic Bluetooth SPP.

| Command | Action |
|---------|--------|
| `F01` | Forward at `currentSpeed` |
| `B01` | Backward at `currentSpeed` |
| `L01` | Turn left (Motor A back, Motor B forward) |
| `R01` | Turn right (Motor A forward, Motor B back) |
| `S00` | Stop, reset watchdog |
| `SL1:XX` | Set speed (0–255 slider → mapped to 0 or 60–255 PWM) |
| `A00` / `B00` / `X00` / `Y00` | Face buttons (available for future use) |
| `P` | Heartbeat ping → device replies `K`, resets watchdog |

Speed mapping: slider value 0 → full stop; 1–255 → clamped to `[minSpeed=60, maxSpeed=255]`.

## Key Constants (top of .ino)

| Constant | Value | Purpose |
|----------|-------|---------|
| `minSpeed` | 60 | Minimum PWM to prevent motor stall |
| `maxSpeed` | 255 | Maximum PWM |
| `cmdTimeoutMs` | 3000 ms | Safety watchdog |
| `eyeSleepTimeout` | 15000 ms | Idle time before sleep FSM activates |
| `petCooldown` | 3000 ms | Eye-happy duration after touch release |
| `tapWindow` | 4000 ms | Window for counting rapid taps |
| `TAPS_FOR_JOY` | 5 | Taps required to trigger laugh |
| `laughDuration` | 2000 ms | Duration of laugh animation |
| `MP3_VOLUME` | 25 | DFPlayer volume (0–30) |
| `SPURIOUS_GUARD_MS` | 500 ms | Ignore DFPlayer finish callbacks this soon after play |
| `DUPLICATE_GUARD_MS` | 800 ms | Ignore duplicate DFPlayer finish callbacks |

## Code Conventions

- Comments are in **Spanish**; code identifiers are in English.
- All timing uses `millis()` — no `delay()` calls anywhere.
- Motor direction changes go through `setMotor()` exclusively; never write to motor pins directly.
- Eye mood changes (`roboEyes.setMood()`, `roboEyes.setPosition()`) are paired with movement commands to give visual feedback of robot state.
