# Session: Eye Wake Restore Fix
**Feature:** `eye-wake-restore`
**Branch:** `feat/eye-wake-restore`
**Base / Target:** `main`
**Date:** 2026-04-07

---

## Problem Statement

When the robot boots, eyes start wide open (height 36, autoblinker on) and do an initial blink sequence — this is the desired "awake" look.

When the robot falls asleep (10 s after BT disconnect) and is then woken by:
- **BT connect** → `eyesWake()` is called, eyes are set to height 36 + HAPPY mood, BUT no blink is triggered, and the very next `stopMotors()` call resets mood to DEFAULT silently.
- **Touch while sleeping** → `eyesWake()` is called, then `onPetStart()` immediately overrides the state with `setHeight(18, 18)` (squinted pleasure eyes). Eyes never show the wide-open state.

**Expected behaviour:** After any wake event, eyes should show the same wide-open + blink animation as at boot, then settle into normal idle/autoblink mode.

---

## Root Cause Analysis

### Boot path (correct)
```
setup():
  roboEyes.begin(128, 64, 100)   ← full engine init, eyes fully open
  setAutoblinker(true)
  setIdleMode(true, 2, 6)
  → eyes wide open, autoblinker starts, blink on first cycle
```

### BT-connect wake path (broken)
```
loop() detects nowConnected && !btConnected:
  eyesWake()                     ← height 36, mood HAPPY, autoblinker on
  → no blink() call → eyes just "appear" open without animation
  → next stopMotors() resets mood to DEFAULT with no blink
```

### Touch-while-sleeping wake path (broken)
```
onPetStart() detects eyesSleeping:
  eyesWake()                     ← height 36, mood HAPPY
  → falls through to pet handler:
    setMood(HAPPY)
    setPosition(DEFAULT)
    setHeight(18, 18)            ← SQUINTS the just-opened eyes immediately
    blink()
  → eyes never fully open after wake
```

---

## Fix Plan

### Change 1 — Add `roboEyes.blink()` at end of `eyesWake()`

`eyesWake()` (line ~305) needs one extra call at the end:

```cpp
void eyesWake() {
  eyesSleeping = false;
  display.fillRect(107, 0, 21, 64, SSD1306_BLACK);
  display.fillRect(0, 0, 25, 64, SSD1306_BLACK);
  display.display();
  roboEyes.setFramerate(100);
  roboEyes.setHeight(36, 36);
  roboEyes.setMood(HAPPY);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setAutoblinker(true);
  roboEyes.setIdleMode(true, 2, 6);
  roboEyes.blink();              // ← ADD: simulate boot blink
  Serial.println("[OLED] Despertando");
}
```

This alone fixes the BT-connect path.

### Change 2 — Guard the touch path: skip pet squint when just woken

In `onPetStart()`, when `eyesSleeping` is true and we call `eyesWake()`, we should `return` immediately after waking so the pet-squint code doesn't override the wide-open state on the very same touch event.

```cpp
void onPetStart() {
  if (eyesSleeping) {
    eyesWake();
    if (!btConnected) lastConnectedTime = millis();
    Serial.println("[TOUCH] Despertado por caricia");
    return;   // ← ADD: don't squint on the same touch that woke the robot
  }
  // ... rest of pet handler unchanged
}
```

This is safe because:
- The touch is still debounce-edge-detected (only fires once on press).
- On the *next* touch the pet handler runs normally.
- `lastConnectedTime` is refreshed so the robot won't immediately re-sleep.

### Change 3 (optional quality) — Ensure `stopMotors()` doesn't silently override the wake mood

Currently `stopMotors()` always calls `roboEyes.setMood(DEFAULT)`. After a wake event the mood is HAPPY and the blink hasn't completed yet. A subsequent S00 or watchdog stop will reset mood to DEFAULT immediately, removing the HAPPY feel.

Option A (minimal): no change — DEFAULT is acceptable, this only matters for the first second after wake.
Option B (preferred): make `stopMotors()` respect `eyesSleeping` state — already false by the time it's called, so no change needed. DEFAULT mood at stop is correct once the robot is awake and not moving.

**Decision: Option A, no change to `stopMotors()`** — the blink from `eyesWake()` will have been queued and the autoblinker takes over. DEFAULT mood at stop is the correct resting state.

---

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `ESP32_L298_motor_N20_MakersLab.ino` | ~305–318 (`eyesWake`) | Add `roboEyes.blink()` before closing brace |
| `ESP32_L298_motor_N20_MakersLab.ino` | ~225–232 (`onPetStart` sleep guard) | Add `return` after `eyesWake()` call |

Total diff: **+2 lines**.

---

## Timing / Safety Notes

- `roboEyes.blink()` is non-blocking — it queues a blink animation processed in `roboEyes.update()`.
- No `delay()` added anywhere.
- No new state variables needed.
- No motor pins touched.
- No BT logic changed.

---

## Failsafe Matrix (unchanged)

| Failure mode | Detection | Response |
|---|---|---|
| BT disconnect during movement | `cmdTimeoutMs` watchdog | `stopMotors()` |
| Sleep FSM stuck | `eyesSleeping` flag | `eyesWake()` on any wake event resets flag |
| Touch during post-laugh | `postLaugh` flag guards `petTimer` | unchanged |

---

## Acceptance Criteria

- [ ] After BT connects to sleeping robot → eyes open wide and blink once, then idle normally
- [ ] After touching sleeping robot → eyes open wide and blink once; next touch triggers normal pet squint
- [ ] After 5 rapid taps on a sleeping robot → wake, then second set of 5 taps triggers laugh
- [ ] Robot still falls asleep after 10 s of no BT + no touch
- [ ] No `delay()` introduced
- [ ] Serial output shows `[OLED] Despertando` on both wake paths

---

## Branch Strategy

- **Branch:** `feat/eye-wake-restore`
- **Base:** `main`
- **Target:** `main`
- **Review:** 1 reviewer required

---

## Status

- [x] Root cause identified
- [x] Plan written
- [x] Implementation
- [ ] Testing on hardware
