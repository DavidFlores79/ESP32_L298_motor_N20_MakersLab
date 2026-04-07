# HC-SR04 Ultrasonic Sensor Integration — Expert Planning Analysis

**Project:** ESP32_L298_motor_N20_MakersLab  
**Task:** Integrate 3x HC-SR04 ultrasonic distance sensors (Front, Left, Right)  
**Document Type:** Architecture Planning & Decision Analysis  
**Date:** 2026-04-07

---

## Executive Summary

This document provides expert architectural guidance for integrating 3x HC-SR04 ultrasonic sensors into an existing ESP32 robot firmware with cooperative `loop()` scheduling, Bluetooth Classic SPP control, and OLED eye animation. The analysis covers non-blocking polling strategies, crosstalk prevention, pin validation, behavior mode design, timing impact, failsafe design, and memory footprint.

**Key Recommendation:** Use **Option C (Loop-based state machine)** with 60ms round-robin cycle and implement **Mode 3 (Hybrid)** for maximum user value with acceptable complexity.

---

## 1. Non-Blocking Sensor Polling Architecture

### HC-SR04 Timing Requirements Recap

Each HC-SR04 measurement cycle:
- **TRIG pulse:** 10µs HIGH → triggers ultrasonic burst
- **ECHO response:** Pulse width proportional to distance (150µs–25ms for 2.5cm–4.3m range)
- **Timeout:** If no echo detected, ECHO stays LOW (max 38ms recommended timeout)
- **Recovery time:** 60ms minimum between consecutive measurements on the same sensor

### Option A: ISR-Based (CHANGE Interrupt on ECHO Pins)

**Implementation:**
```
Three ECHO pins → three `attachInterrupt(..., CHANGE)` handlers
ISR captures micros() timestamp on RISING and FALLING edges
Volatile uint32_t arrays store start/end times per sensor
loop() reads timestamps, calculates pulse width, clears flags
```

**Evaluation:**

| Criterion | Assessment |
|-----------|------------|
| **CPU Overhead** | Very low — ISRs fire only 6 times per 180ms cycle (2 edges × 3 sensors) |
| **Timing Accuracy** | Excellent — sub-microsecond capture jitter with `IRAM_ATTR` ISR |
| **Complexity** | Medium-high — requires `portENTER_CRITICAL(&mux)` for all shared timestamp access in `loop()` |
| **Risk with BT + I2C** | **MEDIUM-HIGH RISK** — ESP32 BT stack can disable interrupts for 10-50ms during critical sections; I2C clock stretching can cause similar stalls. If ECHO pulse occurs during BT RF event, ISR may fire late or miss edge entirely. |
| **Code Volume** | ~120 lines (3 ISRs + critical section wrappers + timeout logic) |

**Failure Modes:**
- ECHO pulse during BT stack ISR → timestamp captured late → distance overestimated
- Consecutive BT events → missed RISING edge → stale data or false timeout

**Verdict:** ❌ **NOT RECOMMENDED** — BT Classic coexistence risk is unacceptable for a manual-control robot where sensor data is advisory, not safety-critical. The added complexity doesn't justify the accuracy gain.

---

### Option B: Timer-Based (ESP32 Hardware Timer)

**Implementation:**
```
hw_timer_t* configured for 180ms period
ISR fires TRIG pulse, arms software timeout for ECHO
Polls ECHO pin state with micros() timestamps in ISR context
Result stored in volatile, consumed by loop()
```

**Evaluation:**

| Criterion | Assessment |
|-----------|------------|
| **CPU Overhead** | Low — ISR runs 5-6 times/sec; ECHO polling within ISR adds ~20µs per check |
| **Timing Accuracy** | Good — hardware timer is drift-free, but ECHO polling has ~5µs jitter |
| **Complexity** | High — requires hardware timer config, ISR polling loop with `ets_delay_us()`, SharedData mutex |
| **Risk with BT + I2C** | **HIGH RISK** — Entire ISR (including ECHO poll loop) runs with interrupts disabled on the same core. If ISR takes >100µs, can cause BT packet loss or I2C NACK. |
| **Code Volume** | ~150 lines (timer init + ISR state machine + timeout handling + critical sections) |

**Failure Modes:**
- ISR overrun → BT disconnection due to missed keepalive
- Long ECHO time (distant object) → ISR blocks for 25ms → BT/I2C starvation

**Verdict:** ❌ **NOT RECOMMENDED** — Introduces hard real-time constraint into a soft real-time system. The firmware was explicitly designed to avoid `delay()` and blocking — a 25ms ISR violates that architecture.

---

### Option C: Loop-Based State Machine (Recommended)

**Implementation:**
```
Per-sensor state: IDLE → TRIG_START → TRIG_DONE → WAIT_RISING → MEASURING → DATA_READY → (back to IDLE)
TRIG_START: digitalWrite(HIGH) + delayMicroseconds(10) + digitalWrite(LOW) [blocking but only 10µs]
WAIT_RISING: poll ECHO pin each loop iteration until HIGH (or timeout at 40ms)
MEASURING: poll ECHO pin until LOW, calculate pulse width
Round-robin: sensor 0 → wait 60ms → sensor 1 → wait 60ms → sensor 2 → wait 60ms → repeat
```

**Evaluation:**

| Criterion | Assessment |
|-----------|------------|
| **CPU Overhead** | Medium — 3x digitalRead() per loop iteration during MEASURING state (~3µs total); negligible outside measurement window |
| **Timing Accuracy** | Good — ±50µs jitter due to loop latency; acceptable for obstacle detection (±8mm at 343m/s) |
| **Complexity** | Low — simple state machine per sensor; no ISRs, no critical sections, no hardware timer config |
| **Risk with BT + I2C** | **LOW RISK** — State machine yields control every loop iteration; BT/I2C/OLED tasks interleave naturally. Worst case: one loop iteration blocked by roboEyes.update() (~2ms) → ECHO measurement delayed but not lost. |
| **Code Volume** | ~80 lines (enum state machine + 3x sensor struct + update function) |

**Timing Breakdown:**
- `delayMicroseconds(10)` for TRIG pulse: 10µs blocking — acceptable (same as single I2C byte transfer)
- digitalRead() poll frequency: every loop iteration (~500µs avg based on existing code) — 2000 Hz sampling rate
- ECHO pulse width range: 150µs–25ms → always captured by multiple loop iterations

**Failure Modes:**
- Long `roboEyes.update()` draw (worst case ~5ms) during short ECHO pulse (150µs for close object): pulse could be missed. **Mitigation:** Cache ECHO state on RISING edge, calculate duration on FALLING edge.
- BT command storm: if 50+ commands/sec arrive, could starve sensor polling. **Mitigation:** User input is <10 commands/sec; nonissue.

**Verdict:** ✅ **RECOMMENDED** — Fits existing architecture perfectly. Non-blocking philosophy preserved. Sufficient accuracy for obstacle detection (±1cm at 30cm range). Low risk, low complexity, maintainable.

---

## 2. Crosstalk Prevention with 3 Sensors

### HC-SR04 Acoustic Crosstalk Physics

- **Ultrasonic burst:** 40 kHz carrier, 8-cycle pulse train (~200µs duration)
- **Beam pattern:** ~30° cone
- **Speed of sound:** 343 m/s at 20°C
- **Echo decay time:** ~50ms for 8m round-trip (hard reflector)

### Crosstalk Scenario

If SENSOR_1 triggers while SENSOR_2's echo is still returning:
- SENSOR_1's ECHO pin may detect SENSOR_2's reflected pulse
- Results in false positive or incorrect distance

### Minimum Safe Stagger Interval

**Conservative calculation:**
```
Max detection range:        4m (HC-SR04 spec)
Round-trip time:            (4m × 2) / 343 m/s = 23.3ms
Safety margin (reverb):     +15ms
Total:                      38ms per sensor
Minimum stagger:            40ms (rounded)
```

**Recommended stagger:** **60ms** — provides 20ms buffer for:
- Acoustic reflections in enclosed spaces
- Sensor warm-up drift (first reading after power-on can take 45ms)
- Timing jitter from loop latency

### Round-Robin Polling Rate

**Per-sensor rate:**
- 3 sensors × 60ms stagger = **180ms cycle time**
- Per-sensor update rate: **5.55 Hz**

**Is 5.55 Hz sufficient?**

| Use Case | Min Required Rate | 5.55 Hz Adequate? |
|----------|-------------------|-------------------|
| Obstacle detection at 0.2 m/s forward speed | 2 Hz (10cm refresh distance) | ✅ Yes — detects obstacle 36cm ahead |
| Emergency stop at max speed (unknown) | Depends on braking distance | ⚠️ Needs testing — if robot can't stop in 36cm, reduce speed in autonomous mode |
| Manual BT control with sensor override | 3 Hz (human reaction buffer) | ✅ Yes |

**Verdict:** 60ms stagger, 180ms cycle is **SAFE and ADEQUATE** for typical differential-drive robot speeds (0.1–0.5 m/s).

---

## 3. Pin Assignment Validation

### Proposed Pins

| Sensor | TRIG Pin | ECHO Pin |
|--------|----------|----------|
| FRONT  | GPIO 16  | GPIO 34  |
| LEFT   | GPIO 17  | GPIO 35  |
| RIGHT  | GPIO 23  | GPIO 36  |

### ESP32 GPIO Constraint Analysis

#### TRIG Pins (16, 17, 23) — OUTPUT

| GPIO | Type | Boot Constraint | Peripheral Conflict | Verdict |
|------|------|-----------------|---------------------|---------|
| **16** | Bidirectional | ⚠️ **PSRAM conflict if enabled** — but this project doesn't use PSRAM | No conflict with LEDC (Ch 0/1 on 14/25), I2C (21/22), or BT (internal RF) | ✅ Safe |
| **17** | Bidirectional | None | No conflict | ✅ Safe |
| **23** | Bidirectional | None | ⚠️ **VSPI default MOSI** — but SPI isn't used in this project | ✅ Safe |

#### ECHO Pins (34, 35, 36) — INPUT-ONLY

| GPIO | Type | Boot Constraint | ADC Conflict | Verdict |
|------|------|-----------------|--------------|---------|
| **34** | Input-only, **ADC1_CH6** | None (input-only pins don't affect boot) | ADC1 — no WiFi conflict. BT Classic **does not** disable ADC1. | ✅ Safe |
| **35** | Input-only, **ADC1_CH7** | None | Same as above | ✅ Safe |
| **36 (VP)** | Input-only, **ADC1_CH0** | None | Same as above | ✅ Safe |

#### Critical Checks

**1. Boot Strapping:**
- None of the proposed pins are boot-mode strapping pins (GPIO 0, 2, 5, 12, 15).
- Input-only pins (34/35/36) have no influence on boot mode.
- **Verdict:** ✅ No boot issues.

**2. ADC2 + WiFi Conflict:**
- ADC2 (GPIO 0, 2, 4, 12-15, 25-27) is disabled when WiFi is active.
- This project uses **Bluetooth Classic** (not WiFi).
- ECHO pins are **ADC1** (GPIO 32-39).
- **Verdict:** ✅ No ADC conflict.

**3. LEDC Timer Conflict:**
- LEDC channels 0/1 already used on GPIO 14/25 (motors).
- TRIG pins (16/17/23) are GPIO outputs, not using LEDC.
- **Verdict:** ✅ No PWM conflict.

**4. I2C Conflict:**
- I2C uses GPIO 21 (SDA) and 22 (SCL).
- No overlap with sensor pins.
- **Verdict:** ✅ No I2C conflict.

**5. BluetoothSerial Internal Pins:**
- ESP32 BT Classic uses internal RF hardware, no external GPIOs.
- **Verdict:** ✅ No BT conflict.

### Final Pin Assignment

| Sensor | TRIG Pin | ECHO Pin | Conflicts | Status |
|--------|----------|----------|-----------|--------|
| FRONT  | GPIO 16  | GPIO 34  | None — input-only ADC1 pin safe for BT Classic | ✅ APPROVED |
| LEFT   | GPIO 17  | GPIO 35  | None | ✅ APPROVED |
| RIGHT  | GPIO 23  | GPIO 36  | None | ✅ APPROVED |

---

## 4. "Smart Behavior" Layer — Mode Evaluation

### Mode 1: Safety Override (BT-Controlled, Sensors Block Crash Commands Only)

**Behavior:**
- User sends `F01` (forward) via BT → before executing, check `frontDistance`
- If `frontDistance < CRASH_THRESHOLD` (e.g., 15cm) → ignore command, motors stay stopped, play SCARED eye animation
- If distance is safe → execute command normally
- Applies to all directional commands: F/B/L/R (with per-sensor direction mapping)

**Complexity Estimate:**

| Component | Lines of Code | Notes |
|-----------|---------------|-------|
| Distance-to-direction mapping | 15 | `if (cmd=='F' && frontDist < 15) return;` logic |
| Collision detection function | 20 | `bool isSafe(cmd)` checks relevant sensors |
| Eye animation on block | 5 | `roboEyes.setMood(SCARED); blink();` |
| Integration into `processCommand()` | 10 | Add `if (!isSafe(cmd)) return;` before motor calls |
| **Total** | **~50 lines** | |

**Integration Risk:**
- **LOW** — only modifies `processCommand()` function; no new FSM required
- Existing safety watchdog and BT manager unchanged
- Touch sensor / sleep FSM unaffected

**Testing Strategy:**
- Place obstacle at 10cm → send F01 → verify motor does NOT move
- Move obstacle to 20cm → send F01 → verify motor DOES move
- Test with BT disconnect → verify sensors don't interfere with sleep FSM

**User Experience:**
- Transparent — robot feels "smarter" without changing control model
- Frustration risk: if sensor malfunctions, robot refuses all commands

**Priority:** ⭐⭐⭐ **HIGH** — Maximum safety value, minimal disruption, easy to test.

---

### Mode 2: Autonomous Explore (Activates on BT Disconnect, Robot Wanders)

**Behavior:**
- Triggered 10s after BT disconnect (same timeout as current sleep FSM)
- Replaces sleep animation with exploration behavior
- Movement algorithm (simplified):
  - If all sensors > 30cm → forward at 80 speed
  - If front < 30cm → stop, pick random direction (L/R based on which side is clearer), turn 45-90°, resume
  - If stuck (all sensors < 20cm) → reverse 1s, rotate 180°, resume
- Eyes show CURIOUS mood, look in movement direction
- Touch pet → switches to HAPPY mood for 3s, continues exploring
- BT reconnect → immediately stops motors, returns to manual control

**Complexity Estimate:**

| Component | Lines of Code | Notes |
|-----------|---------------|-------|
| Explore FSM states (FORWARD, CHECK, TURN, REVERSE, STUCK) | 80 | State machine with `switch (exploreState)` |
| Clearance evaluation function | 30 | `pickBestDirection()` compares left/right distances |
| Turn-by-time logic (no encoders) | 25 | Rotate at fixed speed for calculated duration (e.g., 500ms = ~90°) |
| Integration with sleep timeout trigger | 15 | Replace `eyesSleep()` call with `startExplore()` |
| Stop-on-reconnect logic | 10 | In BT connection handler, add `if (exploring) stopExplore();` |
| **Total** | **~160 lines** | |

**Integration Risk:**
- **MEDIUM-HIGH** — New FSM coexists with existing sleep FSM, touch FSM, and BT manager
- Risk: state collision if touch pet during turn → needs careful state priority design
- Risk: BT reconnect during reverse → must cancel movement immediately (safety watchdog helps)

**Testing Strategy:**
- Disconnect BT → wait 10s → verify robot starts moving
- Place obstacles → verify avoidance behavior (front/left/right cases)
- Reconnect BT mid-turn → verify instant stop + return to manual control
- Pet during explore → verify mood changes but behavior continues

**User Experience:**
- High engagement — robot "comes alive" when idle
- Potential annoyance if user expects sleep animation (behavioral change)
- Table-edge risk if no cliff sensors

**Priority:** ⭐⭐ **MEDIUM** — High "wow factor" but complex integration; recommend after Mode 1 proven stable.

---

### Mode 3: Hybrid (Mode 1 When BT Connected, Mode 2 When Disconnected)

**Behavior:**
- BT connected → Mode 1 (manual control with crash prevention)
- BT disconnected >10s → Mode 2 (autonomous explore)
- Combines best of both: safe manual control + engaging idle behavior

**Complexity Estimate:**

| Component | Lines of Code | Notes |
|-----------|---------------|-------|
| Mode 1 (safety override) | 50 | From Mode 1 estimate |
| Mode 2 (autonomous explore) | 160 | From Mode 2 estimate |
| Mode switching logic | 20 | `if (btConnected) { mode1(); } else if (exploring) { mode2(); }` |
| Shared sensor data access | 10 | Both modes read same `frontDist`, `leftDist`, `rightDist` |
| **Total** | **~240 lines** | |

**Integration Risk:**
- **MEDIUM** — Risk is primarily from Mode 2; Mode 1 + Mode 2 don't interact directly (mutually exclusive states)
- State machine priority must be clear: BT reconnect ALWAYS overrides explore, even mid-maneuver
- Touch pet during explore (Mode 2) vs touch during manual (Mode 1) → different behaviors; must be documented

**Testing Strategy:**
- Full regression of Mode 1 + Mode 2 test cases
- Transition testing: BT disconnect during maneuver → verify graceful switch to explore after 10s
- Touch pet in both modes → verify behavior is sensible in each context

**User Experience:**
- Best UX — robot is useful when controlled, entertaining when idle
- Seamless transitions maintain engagement

**Priority:** ⭐⭐⭐ **HIGH** (after Mode 1 validated) — This is the ultimate target mode. Recommend implementing Mode 1 first as MVP, then add Mode 2's explore FSM.

---

### Mode Recommendation Summary

| Mode | Complexity | Risk | User Value | Recommended Implementation Order |
|------|------------|------|------------|----------------------------------|
| Mode 1 | Low (50 LOC) | Low | High (safety) | **Phase 1** — Implement first |
| Mode 2 | High (160 LOC) | Medium-High | Medium (engagement) | **Phase 3** — After Mode 1 proven |
| Mode 3 | High (240 LOC) | Medium | Very High (safety + engagement) | **Phase 2** — Add Mode 2 layer to working Mode 1 |

**Recommendation:** Implement **Mode 1 → Mode 3** in two phases. Skip standalone Mode 2 — go directly from "crash override" to "hybrid."

---

## 5. Timing Budget Impact

### Current Loop Timing Profile (Estimated)

Based on code analysis, typical single loop iteration:

| Task | Frequency | Duration (µs) | Notes |
|------|-----------|---------------|-------|
| BT serial read (`SerialBT.available()`) | Every loop | 50 | DMA-backed, very fast |
| BT command parse (if newline) | ~1/sec | 200 | String ops, motor calls |
| Safety watchdog check | Every loop | 5 | Single `millis()` comparison |
| Touch sensor digitalRead | Every loop | 3 | GPIO read |
| Touch state machine | On edge only | 50 | Pet reaction, tap counting |
| BT connection state check | Every loop | 10 | `hasClient()` check |
| Sleep FSM (if active) | Every 4s (timed) | 100 | Random position calc |
| `roboEyes.update()` | Every loop | **500–2000** | **Heaviest task** — blinking/saccade rendering |
| `drawZzz()` (if sleeping) | Every loop | 300 | Text rendering + display.display() |
| **Total per iteration** | — | **~1000–3000 µs** | **Loop rate: 333–1000 Hz** |

**Key observation:** `roboEyes.update()` dominates timing. Everything else is noise.

---

### Sensor Polling Added Load

#### Option C State Machine (Recommended)

**Per loop iteration:**

| Task | Condition | Duration (µs) | Notes |
|------|-----------|---------------|-------|
| Check if sensor ready to poll | Every loop | 5 | `millis()` comparison × 3 sensors |
| Fire TRIG pulse | Once per 60ms per sensor | 10 | `delayMicroseconds(10)` — blocking |
| Poll ECHO pin (WAIT_RISING state) | Active sensor only | 3 | Single `digitalRead()` |
| Poll ECHO pin (MEASURING state) | Active sensor only | 3 | Single `digitalRead()` |
| Calculate distance | On ECHO fall | 50 | `pulseWidth / 58` + bounds check |
| **Max added per loop** | — | **~25 µs** | Worst case: TRIG + 2 reads + 1 calc |

**Impact on loop rate:**
- Current: 1000–3000 µs/iteration
- With sensors: 1025–3025 µs/iteration
- **Impact: <2.5% increase** — negligible

---

### Impact on Critical Tasks

#### BT Command Processing (Responsiveness)

**Scenario:** User presses forward on gamepad → how long until motors move?

| Step | Time (ms) | Notes |
|------|-----------|-------|
| BT packet arrival | 0 | Buffered by DMA |
| Loop detects newline | 0–3 | Max loop period (worst case with sensors: 3.025ms) |
| `processCommand()` executes | +0.2 | Motor calls |
| **Total latency** | **3.2ms** | |

**Without sensors:** 3.0ms (0.2ms difference)  
**Human perception threshold:** 100ms (JND for input lag)  
**Verdict:** ✅ **NO PERCEPTIBLE IMPACT** — 0.2ms is 150× below human detection threshold.

---

#### RoboEyes Visual Smoothness

**Current behavior:**
- `roboEyes.setFramerate(100)` → target 10ms/frame
- Actual render time: 0.5–2ms depending on blink state
- Loop rate (333–1000 Hz) >> eye framerate (100 Hz) → buffer of 3–10 loop iterations per frame

**With sensors added:**
- Loop rate drops to 330–975 Hz (–1.5% worst case)
- Still 3–10 iterations per eye frame
- **Verdict:** ✅ **NO IMPACT** — Eye animation schedule unaffected.

---

#### Safety Watchdog Response Time

**Scenario:** BT disconnects mid-movement → how fast do motors stop?

| Without Sensors | With Sensors |
|----------------|--------------|
| Next loop: 3.0ms | Next loop: 3.025ms |
| Watchdog triggers: 3000ms | Watchdog triggers: 3000ms |

**Verdict:** ✅ **NO MEANINGFUL IMPACT** — 25µs is 0.0008% of 3-second timeout.

---

### Timing Budget Summary

| Task | Current Latency | With Sensors | Change | Impact |
|------|----------------|--------------|--------|--------|
| BT command response | 3.0ms | 3.2ms | +0.2ms | None (below human JND) |
| Eye animation smoothness | 10ms/frame | 10ms/frame | None | None |
| Safety watchdog | 3000ms | 3000ms | +0.025ms | None |
| Loop iteration rate | 333–1000 Hz | 330–975 Hz | –1.5% | Negligible |

**Conclusion:** Sensor integration with Option C has **ZERO functional impact** on responsiveness or visual quality.

---

## 6. Failsafe Matrix for Sensor Subsystem

### Failure Mode 1: Wiring Error (ECHO Never Goes HIGH)

**Symptom:** Sensor in WAIT_RISING state, ECHO pin stays LOW beyond timeout.

**Detection:**
```
if (state == WAIT_RISING && (millis() - stateStartTime > 40)) {
  errorCode = SENSOR_NO_RESPONSE;
  consecutiveFailures++;
}
```

**Response:**

| Failure Count | Action | Safe State | Recovery |
|---------------|--------|------------|----------|
| 1–2 | Log WARNING, mark sensor STALE | Continue with other 2 sensors | Auto-retry next cycle (60ms later) |
| 3–10 | Log ERROR, exclude sensor from decisions | Reduce autonomous speed 50% if Mode 2 active | Still retry (may be transient) |
| >10 | Log CRITICAL, disable sensor permanently | If Mode 2: stop explore, return to sleep. If Mode 1: disable crash override for that direction. | Require power cycle to re-enable |

**BT Notification:**
- After 3 failures: send `"WRN:SENSOR_FRONT_FAIL\n"` to connected client (if any)
- App can display warning icon

---

### Failure Mode 2: Sensor Stuck (ECHO Stays HIGH)

**Symptom:** ECHO goes HIGH but never returns LOW.

**Detection:**
```
if (state == MEASURING && (millis() - stateStartTime > 40)) {
  errorCode = SENSOR_STUCK_HIGH;
  consecutiveFailures++;
  digitalWrite(TRIG, LOW); // force sensor reset
}
```

**Response:**

| Failure Count | Action | Safe State | Recovery |
|---------------|--------|------------|----------|
| 1–2 | Log WARNING, invalidate reading | Use last-known-good distance (cached) | Power-cycle sensor via `delay(100)` then retry |
| 3+ | Log ERROR, disable sensor | Same as wiring error above | Sticky disable until power cycle |

**Root Cause Mitigation:**
- Add `digitalWrite(TRIG, LOW)` in setup() — ensures clean initial state
- Before each measurement: verify `digitalRead(ECHO) == LOW` before firing TRIG

---

### Failure Mode 3: All 3 Sensors Blocked During Autonomous Mode

**Symptom:** `frontDist < 15cm && leftDist < 15cm && rightDist < 15cm` for >2 seconds.

**Detection:**
```
if (frontDist < 15 && leftDist < 15 && rightDist < 15) {
  if (allBlockedStartTime == 0) allBlockedStartTime = millis();
  if (millis() - allBlockedStartTime > 2000) {
    triggerStuckRecovery();
  }
} else {
  allBlockedStartTime = 0;
}
```

**Response (Mode 2 Autonomous Only):**

| State | Action | Duration | Notes |
|-------|--------|----------|-------|
| 1. Stop | `stopMotors()` | Immediate | Prevent collision |
| 2. Eyes SURPRISED | `roboEyes.setMood(SURPRISED)` | Visual feedback | |
| 3. Reverse | `motorA(-100); motorB(-100);` | 2 sec | Back out of corner |
| 4. Rotate | `turnRight(100);` | 3 sec (~270°) | Large turn to escape |
| 5. Resume | Return to FORWARD state | Indefinite | Retry exploration |

**If still stuck after recovery:**
- After 3 consecutive stuck-recovery cycles → **abort autonomous mode, return to sleep**
- Eyes show TIRED mood
- BT notification: `"ERR:STUCK_ABORT\n"`

**Mode 1 Behavior (Manual Control):**
- Stuck detection does NOT apply — user is in control
- Crash override still blocks individual dangerous commands

---

### Failure Mode 4: Sensor Reads Out-of-Spec Distance

**Symptom:** Calculated distance <2cm or >400cm (HC-SR04 valid range: 2–400cm).

**Detection:**
```
int distance = pulseWidth / 58; // cm
if (distance < 2 || distance > 400) {
  errorCode = SENSOR_OUT_OF_RANGE;
  distance = INVALID_DISTANCE; // Sentinel value
}
```

**Response:**
- Do NOT increment consecutiveFailures (not a hardware fault)
- Treat as "no obstacle" (distance = 400cm) for decision logic
- Log DEBUG message (not ERROR) — this is expected for open spaces

---

### Distance Threshold Recommendations

Based on typical differential-drive robot with N20 motors:

| Threshold Name | Value (cm) | Purpose |
|----------------|------------|---------|
| `CRASH_THRESHOLD` | **15cm** | Mode 1: Block manual command if closer |
| `AVOID_THRESHOLD` | **30cm** | Mode 2: Trigger avoidance maneuver |
| `STUCK_THRESHOLD` | **15cm** | Mode 2: All sensors — trigger stuck recovery |
| `CLEAR_THRESHOLD` | **50cm** | Mode 2: "Safe to proceed" forward |
| `INVALID_DISTANCE` | **-1** | Sentinel for read failure |

**Rationale:**
- 15cm crash threshold: Assumes robot width ~10cm, provides 5cm safety margin
- 30cm avoid threshold: At 0.2 m/s speed, gives 1.5s reaction time (3× 180ms sensor cycle = 540ms data age + 1s maneuver buffer)
- 50cm clear threshold: Comfortable open-space detection for confident forward movement

---

### Failsafe Summary Matrix

| Failure Mode | Detection Threshold | Safe State | Degraded Mode | Recovery |
|--------------|---------------------|------------|---------------|----------|
| No ECHO response | 40ms timeout, 3× consecutive | Disable sensor, reduce speed 50% | 2-sensor operation | Auto-retry, sticky after 10 failures |
| ECHO stuck HIGH | 40ms timeout, 3× consecutive | Disable sensor, use cached distance | 2-sensor operation | Power-cycle sensor |
| All sensors blocked | All <15cm for >2s (Mode 2 only) | Stop, reverse+turn, retry | Abort after 3 recoveries | Return to sleep FSM |
| Out-of-range reading | <2cm or >400cm | Treat as "no obstacle" | Full operation | None (expected condition) |

---

## 7. Memory Analysis (ESP32 Platform)

### ESP32 SRAM Baseline

**Available:**
- Total DRAM: ~320KB (after BT stack reserves)
- Free heap at boot (estimated from similar projects): ~250KB
- Stack per task (single-task loop firmware): ~8KB reserved

### Current Firmware Memory Footprint (Estimated)

| Component | SRAM Usage | Notes |
|-----------|------------|-------|
| BluetoothSerial object | ~22KB | BT Classic SPP stack |
| Adafruit_SSD1306 framebuffer | 1KB | 128×64 ÷ 8 bits |
| RoboEyes animation state | ~2KB | Eye position, blink timers, saccade arrays |
| String `cmdBuffer` | ~256 bytes | Dynamic allocation (fragmentation risk) |
| Global state variables | ~500 bytes | Timers, flags, counters |
| Stack usage (peak) | ~4KB | Estimated from call depth |
| **Total used** | **~30KB** | |
| **Free heap** | **~220KB** | Comfortable margin |

---

### Sensor Integration Memory Addition

#### Per-Sensor State Structure

```cpp
struct UltrasonicSensor {
  uint8_t trigPin;
  uint8_t echoPin;
  enum State { IDLE, TRIG_START, WAIT_RISING, MEASURING, DATA_READY } state;
  unsigned long stateStartTime;
  unsigned long echoStartMicros;
  unsigned long echoEndMicros;
  int distanceCm;
  uint8_t consecutiveFailures;
  uint8_t errorCode;
};

UltrasonicSensor sensors[3]; // Front, Left, Right
```

**Size:** 3 × (2 + 2 + 1 + 4 + 4 + 4 + 2 + 1 + 1) = **63 bytes**

#### Additional Globals

| Variable | Type | Size | Purpose |
|----------|------|------|---------|
| `currentSensorIndex` | uint8_t | 1 | Round-robin tracker |
| `lastSensorPollTime` | unsigned long | 4 | Stagger timer |
| `allBlockedStartTime` | unsigned long | 4 | Stuck detection timer |
| Threshold constants (6×) | const int | 12 | CRASH_THRESHOLD, AVOID_THRESHOLD, etc. |
| **Total** | — | **21 bytes** | |

#### Mode 2 Autonomous FSM State

| Variable | Type | Size | Purpose |
|----------|------|------|---------|
| `exploreState` | enum (uint8_t) | 1 | FORWARD, CHECK, TURN, REVERSE, STUCK |
| `exploreStateTimer` | unsigned long | 4 | State duration tracking |
| `turnDirection` | int8_t | 1 | -1=left, +1=right |
| `stuckRecoveryCount` | uint8_t | 1 | Consecutive stuck cycles |
| **Total** | — | **7 bytes** | |

---

### Total Added SRAM for Full Mode 3 Implementation

| Component | SRAM (bytes) |
|-----------|-------------|
| Sensor state (3×) | 63 |
| Sensor globals | 21 |
| Mode 2 FSM | 7 |
| Code (.text segment, not SRAM) | ~1200 | ← *Flash, not counted* |
| **Total SRAM** | **91 bytes** |

---

### Heap Fragmentation Risk

**Risk Factor:** `String cmdBuffer` in existing code uses dynamic allocation.

**Current:** BT command max length ~10 chars → frequent alloc/free of 10-byte chunks → potential fragmentation after extended runtime (hours).

**Sensor impact:** Zero — all sensor variables are statically allocated (`UltrasonicSensor sensors[3]`).

**Recommendation (unrelated to sensors but good practice):**
- Replace `String cmdBuffer` with `char cmdBuffer[32]` (static buffer) to eliminate fragmentation risk.

---

### Flash Usage Estimate

| Component | Flash (bytes) |
|-----------|---------------|
| Option C state machine code | ~800 |
| Distance calculation + threshold checks | ~200 |
| Mode 1 crash override logic | ~300 |
| Mode 2 explore FSM | ~1200 |
| Failsafe + error handling | ~400 |
| **Total added code** | **~2900 bytes (~3KB)** |

**Current firmware Flash (estimated):** ~150KB (with libraries)  
**ESP32 Flash capacity:** 4MB  
**Added code impact:** 0.07% of total capacity  
**Verdict:** ✅ **Negligible** — no concern.

---

### Memory Summary

| Resource | Current Usage | Added by Sensors | New Total | Capacity | Margin |
|----------|---------------|------------------|-----------|----------|--------|
| **Heap (SRAM)** | ~30KB | 91 bytes | ~30.1KB | ~250KB | **~220KB free (87%)** ✅ |
| **Flash** | ~150KB | ~3KB | ~153KB | 4MB | **~3.95MB free (98.8%)** ✅ |
| **Stack** | ~4KB peak | +200 bytes (call depth) | ~4.2KB | 8KB reserved | **~3.8KB free (47%)** ✅ |

**Conclusion:** Memory impact is **TRIVIAL** — no optimization or restructuring required. ESP32 is vastly over-provisioned for this application.

---

## Final Recommendations Summary

### Architecture Decisions

| Question | Recommendation | Rationale |
|----------|----------------|-----------|
| **1. Polling Strategy** | **Option C: Loop-based state machine** | Low risk, low complexity, fits existing architecture, sufficient ±1cm accuracy |
| **2. Crosstalk Prevention** | **60ms stagger, 180ms cycle (5.55 Hz/sensor)** | Safe acoustic separation, adequate for 0.2 m/s robot speed |
| **3. Pin Assignment** | **TRIG: 16/17/23, ECHO: 34/35/36** | No conflicts, input-only pins safe for ECHO, validated boot behavior |
| **4. Behavior Mode** | **Mode 3 (Hybrid)** implemented as Mode 1 → Mode 3 in two phases | Maximum user value, manageable complexity via phased rollout |
| **5. Timing Impact** | **<2.5% loop overhead, zero functional impact** | Sensor polling is negligible vs `roboEyes.update()` dominance |
| **6. Failsafe Design** | **3-tier degradation: warn → disable sensor → abort mode** | Graceful degradation, prevents stuck states, BT notifications for user awareness |
| **7. Memory Footprint** | **+91 bytes SRAM, +3KB Flash** | Trivial on ESP32 platform, 220KB heap still free |

---

### Implementation Roadmap

**Phase 1: Foundation + Mode 1 (MVP)**
- Implement Option C state machine (sensor polling only)
- Add crash override to `processCommand()`
- Validate pin assignments and failsafe triggers
- **Estimated effort:** 6–8 hours coding + 4 hours testing

**Phase 2: Mode 2 Integration (Mode 3 Complete)**
- Add explore FSM triggered on BT disconnect
- Integrate with existing sleep timeout mechanism
- Implement stuck recovery
- **Estimated effort:** 8–10 hours coding + 6 hours testing

**Phase 3: Refinement**
- Tune threshold distances via field testing
- Add BT notification strings for app integration
- Optimize `roboEyes` mood transitions for sensor states
- **Estimated effort:** 4 hours tuning

**Total estimated effort:** 22–28 hours (assuming single developer, includes testing).

---

## Appendix: Code Volume Breakdown

| Module | Lines of Code | Complexity |
|--------|---------------|------------|
| Sensor state machine (Option C) | 80 | Low |
| Round-robin scheduler | 30 | Low |
| Distance calculation + validation | 25 | Low |
| Failsafe detection + error handling | 60 | Medium |
| Mode 1: Crash override | 50 | Low |
| Mode 2: Explore FSM | 160 | Medium-High |
| Mode switching logic | 20 | Low |
| BT notification strings | 15 | Low |
| **Total new code** | **~440 lines** | |
| **Existing .ino size** | 620 lines | |
| **Final size (Mode 3)** | **~1060 lines** | |

**Maintainability verdict:** Single-file structure is reaching limits. Recommend refactoring to modular `.h`/`.cpp` files after Mode 3 validated (not blocking for this feature).

---

## Document Control

- **Author:** Arduino Embedded Developer Agent (Expert Mode)
- **Review Status:** Planning Complete — Ready for Implementation
- **Next Step:** Developer review, stakeholder approval, begin Phase 1 implementation
- **Key Risk:** If robot speed exceeds 0.3 m/s, re-evaluate 60ms stagger sufficiency with braking distance test

---

**END OF PLANNING DOCUMENT**
