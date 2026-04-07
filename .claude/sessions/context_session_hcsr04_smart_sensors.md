# Plan: 3× HC-SR04 Integration — ESP32 MakersLab Robot

**Fecha:** 7 de abril de 2026  
**Branch:** `feat/hcsr04-smart-sensors`  
**Base:** `main` → **Target:** `main`  
**Reviewers:** 1 required

---

## Decisiones del Usuario (2026-04-07)

| Pregunta | Decisión |
|---|---|
| Colocación de sensores | Front-center + Front-left corner + Front-right corner |
| Modo safety override (crash prevention en BT) | ❌ **No deseado** — sensores solo usados en explore |
| Trigger de explore por touch | ✅ **Hold 2 segundos** en el sensor táctil → toggle ON/OFF |
| Trigger de explore por app BT | ✅ **Botón A00** → toggle ON/OFF (pendiente UI en app) |

---

## 1. Contexto del Proyecto

| Aspecto | Detalle |
|---|---|
| Plataforma | ESP32 (Classic Bluetooth SPP) |
| Arquitectura | Cooperative `loop()`, todo con `millis()`, cero `delay()` |
| Librerías activas | BluetoothSerial, Adafruit SSD1306/GFX, FluxGarage_RoboEyes |
| LEDC canales usados | ch0 (ENA, GPIO14), ch1 (ENB, GPIO25) |
| I2C | SDA=21, SCL=22 (OLED) |

---

## 2. Análisis de Complejidad (Sequential Thinking)

- **Nivel:** Feature mediana — nuevo subsistema con capa de comportamiento
- **Riesgos principales:** bloqueo del loop() con pulseIn(), crosstalk entre sensores, conflictos de pines ESP32
- **Incógnitas resueltas:** arquitectura de lectura no-bloqueante → Loop-based state machine (recomendada por embedded expert por menor riesgo con stack BT Classic + I2C)

---

## 3. Asignación de Pines

> Todos validados sin conflictos con motores, LEDC, I2C, BT Classic, ni pines de arranque.

| Sensor | TRIG (output) | ECHO (input) | Observaciones |
|---|---|---|---|
| FRONT (centro-frontal) | GPIO **16** | GPIO **34** | input-only OK para ECHO; sin pull-up interno, HC-SR04 lo maneja |
| LEFT (izquierda-frontal) | GPIO **17** | GPIO **35** | input-only OK; ADC1 sin conflicto WiFi |
| RIGHT (derecha-frontal) | GPIO **23** | GPIO **36** | OK; el pin 23 es SPI MOSI libre |

**GPIOs actualmente usados:** 14, 19, 21, 22, 25, 26, 27, 32, 33 → sin superposición.

---

## 4. Arquitectura de Lectura — Loop-State Machine (Opción C)

**Rechazada ISR** (Option A) por riesgo moderado de conflicto con el stack BT Classic que usa FreeRTOS internamente.  
**Rechazado Hardware Timer** (Option B) por complejidad innecesaria.

### Por qué Loop-State Machine:
- Encaja perfectamente en el loop() cooperativo existente
- ±1 cm de precisión, suficiente para detección de obstáculos
- Impacto en loop(): **< 25 µs por iteración** (< 2.5 % overhead)
- Sin riesgo de colisión con BluetoothSerial ni Wire

### Ciclo de polling (round-robin, stagger 60 ms):

```
Sensor FRONT → espera 60ms → Sensor LEFT → espera 60ms → Sensor RIGHT → espera 60ms → repite
```

- Cada sensor: 5.55 Hz de refresco
- Ciclo completo: **180 ms**
- A velocidad máxima (~0.5 m/s) el robot avanza **9 cm** por ciclo → detecta obstáculo a 50 cm con 5 ciclos de antelación ✅

### Mini-FSM por sensor (enum `SonarState`):

```
SONAR_IDLE
   ↓ (triggerInterval alcanzado)
SONAR_TRIG_HIGH   → pone TRIG HIGH, guarda startTime
   ↓ (10 µs → delayMicroseconds(10) aquí es aceptable, es única vez)
SONAR_TRIG_LOW    → pone TRIG LOW, marca echoStart
   ↓
SONAR_WAIT_ECHO   → espera ECHO HIGH; timeout 40ms = sin objeto
   ↓
SONAR_MEASURE     → ECHO está HIGH, espera ECHO LOW
   ↓
SONAR_DONE        → calcula distanceCm = pulseWidth / 58; vuelve a IDLE
```

> **Nota:** El único `delayMicroseconds(10)` para el pulso TRIG es inevitable y aceptable — dura 10 µs vs. los ~3 ms de una escritura I2C al OLED.

---

## 5. Presupuesto de Timing

| Tarea en loop() | Intervalo | Tiempo máx. | Tipo |
|---|---|---|---|
| BT serial read | cada iteración | ~10 µs | polling |
| Safety watchdog | cada iteración | ~1 µs | check |
| Touch sensor | cada iteración | ~1 µs | digitalRead |
| Sleep FSM | cada iteración | ~1 µs | switch |
| roboEyes.update() | cada iteración | ~3 ms (I2C) | bloqueante suave |
| **Sonar FSM × 3** | **60 ms c/u** | **~26 µs** | **nuevo** |
| delayMicroseconds(10) | cada 60 ms por sensor | 10 µs | **única excepción** |

**Impacto en respuesta BT:** 3.0ms → 3.2ms (+0.2ms, imperceptible)  
**Impacto en animación OLED:** ninguno (framerate 100fps >> ciclo loop ~330Hz)

---

## 6. Umbrales de distancia

| Zona | Distancia | Acción |
|---|---|---|
| DANGER | < 15 cm | Stop inmediato + ANGRY eyes |
| CAUTION | 15–30 cm | Advertencia, reducir velocidad |
| CLEAR | > 50 cm | Camino libre |

---

## 7. Modo de Comportamiento — Autonomous Explore (único modo)

> **Safety override eliminado.** Los sensores solo funcionan en explore. En modo BT manual los comandos de joystick se ejecutan sin restricción de sensores.

### Cómo se activa / desactiva

| Canal | Gesto / Comando | Acción |
|---|---|---|
| Sensor táctil (GPIO 19) | **Hold 2 segundos** | Toggle explore ON ↔ OFF |
| App BT | **Botón A00** | Toggle explore ON ↔ OFF |

**Por qué hold y no taps:** los taps cortos ya están reservados (5 taps = laugh). Un hold de 2s no puede dispararse accidentalmente al acariciar al robot.

**Implementación del hold — basada en estructura ACTUAL de `onPetStart()`:**

El `.ino` fue actualizado (fix eye-wake-restore): `onPetStart()` ahora incrementa `tapCount` **antes** de chequear `eyesSleeping`. La nueva estructura es:
```
onPetStart():
  1. if (!btConnected) lastConnectedTime = millis();
  2. tap counting (tapCount++)
  3. if (eyesSleeping) { eyesWake(); return; }   ← tap ya contado
  4. if (tapCount >= TAPS_FOR_JOY) → joy
  5. normal pet
```

- Nueva var: `unsigned long touchPressStart = 0;` — registrar al **inicio absoluto** de `onPetStart()`, como línea 0 (antes del `lastConnectedTime` refresh)
- Nueva var: `bool exploreTriggerFired = false;` — guard para disparar solo una vez por hold
- En `loop()`, **después de** `isTouched = touchNow;`:
  ```cpp
  if (isTouched && !exploreTriggerFired && (millis() - touchPressStart > EXPLORE_HOLD_MS)) {
    toggleExploreMode();
    exploreTriggerFired = true;
  }
  ```
- En `onPetEnd()`: `exploreTriggerFired = false;`

**Beneficio gratuito:** `eyesWake()` ya tiene `roboEyes.blink()` (agregado en fix eye-wake-restore). Cuando `toggleExploreMode()` llame a `eyesWake()` al despertar desde sueño, el blink de apertura es automático — no hay que agregarlo.

**Constante nueva:** `const unsigned long EXPLORE_HOLD_MS = 2000;`

### FSM — `ExploreState`

```
EXPLORE_FORWARD
   → motores adelante a min(currentSpeed, EXPLORE_MAX_SPEED)
   → distFront < CAUTION_CM (30 cm): → EXPLORE_SCAN

EXPLORE_SCAN
   → motores stop, espera 1 ciclo de sonar completo (180 ms)
   → compara distLeft vs distRight:
      distLeft > distRight  → EXPLORE_TURN_LEFT
      distRight >= distLeft → EXPLORE_TURN_RIGHT
      ambos < DANGER_CM (15 cm) → EXPLORE_ESCAPE (escapeAttempts=0)

EXPLORE_TURN_LEFT
   → turnLeft(EXPLORE_MAX_SPEED), timer EXPLORE_TURN_MS (400 ms)
   → timer vence → EXPLORE_FORWARD

EXPLORE_TURN_RIGHT
   → turnRight(EXPLORE_MAX_SPEED), timer EXPLORE_TURN_MS (400 ms)
   → timer vence → EXPLORE_FORWARD

EXPLORE_ESCAPE
   → backward(EXPLORE_MAX_SPEED), timer EXPLORE_BACKUP_MS (350 ms)
   → timer vence → pivota aleatoriamente 90° → EXPLORE_FORWARD
   → si escapeAttempts >= 3 → EXPLORE_STUCK

EXPLORE_STUCK
   → stopMotors()
   → roboEyes.setMood(TIRED)
   → exploreMode = false (auto-desactiva)
   → Serial.println("[EXPLORE] Atascado — deteniendo modo exploración")
```

### Constantes nuevas

| Constante | Valor | Propósito |
|---|---|---|
| `EXPLORE_MAX_SPEED` | 150 | Velocidad máxima en explore (seguridad) |
| `EXPLORE_TURN_MS` | 400 | ms de pivot para ~90° (ajustar según motores) |
| `EXPLORE_BACKUP_MS` | 350 | ms de retroceso en escape |
| `EXPLORE_MAX_ATTEMPTS` | 3 | Intentos antes de EXPLORE_STUCK |

### Comportamiento durante explore

| Condición | Acción motores | Ojos (mood/position) |
|---|---|---|
| BT conectado + explore ON | Sensores mandan, comandos F/B/L/R/S ignorados | — |
| BT desconectado + explore ON | Sensores mandan | — |
| P heartbeat | Respondido siempre | — |
| SL1 slider | Actualiza `currentSpeed` (cap EXPLORE_MAX_SPEED aplica) | — |
| EXPLORE_FORWARD | `forward()` | HAPPY + DEFAULT |
| EXPLORE_SCAN | `stopMotors()` | DEFAULT + mira E/W alternando |
| EXPLORE_TURN_LEFT | `turnLeft()` | DEFAULT + W |
| EXPLORE_TURN_RIGHT | `turnRight()` | DEFAULT + E |
| EXPLORE_ESCAPE | `backward()` | ANGRY |
| EXPLORE_STUCK | stop | TIRED |

### Interacción con la FSM de sueño existente

- Si explore se activa mientras `eyesSleeping==true`: llamar `eyesWake()` primero
- Si explore se desactiva: `stopMotors()`, retomar comportamiento previo (BT o sueño según `btConnected`)

---

## 8. Matriz de Failsafe

| Falla | Detección | Respuesta | Estado degradado |
|---|---|---|---|
| ECHO nunca sube (cableado roto) | timeout 40ms, 3 lecturas consecutivas | deshabilitar sensor, Serial `[SONAR] ERROR sensor X` | explore continúa con 2 sensores; si es FRONT → EXPLORE_STUCK directo |
| ECHO stuck HIGH (cortocircuito) | ECHO HIGH > 100ms | marcar sensor inválido, reintentar cada 5s | igual que arriba |
| Todos < DANGER durante explore | 3 intentos de escape fallidos | EXPLORE_STUCK → auto-desactiva explore | motores detenidos, sueño normal retoma |
| BT timeout (watchdog existente) | sin comando > 3s (ya existe) | stopMotors() — sin cambios en explore | explore sigue activo si estaba ON |

---

## 9. Análisis de Memoria

| Componente | SRAM |
|---|---|
| 3× SonarSensor struct (state, timestamps, distancia, valid) | ~48 bytes |
| ExploreState FSM vars | ~24 bytes |
| Constantes de distancia | ~12 bytes |
| **Total additions** | **~84 bytes** |

**Flash adicional estimado:** ~3 KB  
**Heap libre ESP32:** ~220 KB (87% disponible) → impacto despreciable ✅

---

## 10. Archivos a Modificar

Solo **un archivo** `.ino` — el proyecto es monolítico por diseño.

| Sección nueva / modificada | Ubicación sugerida | LOC |
|---|---|---|
| Defines pines sensores | después de "CONFIG PINES" | 8 |
| Constantes explore + umbrales distancia | antes de variables globales | 12 |
| Struct SonarSensor + enum SonarState | después de "PARÁMETROS DE VELOCIDAD" | 30 |
| Variables globales sensores | después de definición de struct | 15 |
| `bool exploreMode` + `ExploreState` enum + vars FSM | junto a `sleepState` vars | 15 |
| Nuevas vars touch: `touchPressStart`, `exploreTriggerFired` | junto a TOUCH SENSOR vars | 4 |
| `setupSonars()` | llamado desde `setup()` | 15 |
| `updateSonars()` | llamado desde `loop()` | 45 |
| `toggleExploreMode()` helper | antes de `loop()` | 15 |
| `updateExplore()` FSM | llamado desde `loop()` cuando `exploreMode` | 70 |
| Modificar `onPetStart()` | `touchPressStart = millis()` como línea 0 (antes del `lastConnectedTime` refresh) | 2 |
| Modificar `onPetEnd()` | reset `exploreTriggerFired` | 2 |
| Añadir hold-check en `loop()` | sección touch sensor | 5 |
| Modificar `processCommand()` — botón A00 | toggle `exploreMode` | 5 |
| Modificar `processCommand()` — comandos F/B/L/R/S | skip si `exploreMode` | 8 |
| **Total** | | **~252 LOC** |

---

## 11. Convenciones del Proyecto (a respetar)

- Comentarios en **español**
- Identificadores en **inglés**
- Zero `delay()` — se permite un único `delayMicroseconds(10)` para pulso TRIG
- FSMs con `enum` + `switch`
- Timing exclusivamente con `millis()` / `micros()`
- `Serial.println()` con prefijo `[SONAR]` / `[EXPLORE]` para debugging

---

## 12. Branch Strategy

```
git checkout -b feat/hcsr04-smart-sensors main
```

- **Commits sugeridos:**
  1. `feat: add sonar pin definitions and SonarSensor struct`
  2. `feat: implement non-blocking sonar polling FSM`
  3. `feat: add safety override for BT-controlled movement`
  4. `feat: add autonomous explore FSM for BT-disconnected mode`
  5. `test: calibrate thresholds and validate sensor read accuracy`

---

## 11. Protocolo BT — Actualización

Añadir a la tabla de comandos existente:

| Comando | Acción nueva |
|---|---|
| `A00` | Toggle explore mode ON ↔ OFF |
| `F01`, `B01`, `L01`, `R01`, `S00` | **Ignorados** si `exploreMode == true` |
| `P` | Heartbeat → siempre respondido, siempre resetea watchdog |
| `SL1:XX` | Actualiza `currentSpeed` (explore usa `min(currentSpeed, EXPLORE_MAX_SPEED)`) |

**Para la app Flutter:** el botón A debe mostrar estado visual: icono diferente (🤖 explorar / ⏹ detener). El robot NO envía confirmación de estado — la app mantiene su propio toggle local. Si se quiere feedback bidireccional se puede añadir `SerialBT.print("EX:1\n")` / `"EX:0\n"` en `toggleExploreMode()`.

---

## Iteraciones

| Fecha | Cambio |
|---|---|
| 2026-04-07 v1 | Primera versión — análisis con Sequential Thinking + Context7 + arduino-embedded-developer agent |
| 2026-04-07 v2 | Refinado con decisiones del usuario: no safety override, explore por hold-touch + A00, placement front-3 |
| 2026-04-07 v3 | Actualizado por cambios en `.ino` (fix eye-wake-restore): nueva estructura de `onPetStart()`, `touchPressStart` va como línea 0; `eyesWake()` ya tiene `blink()` gratis |
