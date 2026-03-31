// ABOUTME: ESP32 motor controller (L298 + N20) with Classic Bluetooth SPP
// ABOUTME: Receives gamepad commands from MakersLab Flutter app via BluetoothSerial
// ABOUTME: OLED 128x64 shows animated robot eyes via FC_RoboEyes library

#include "BluetoothSerial.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
  #error Bluetooth Classic no está habilitado. Verifica la configuración del SDK.
#endif

BluetoothSerial SerialBT;

// =====================
// OLED + ROBOEYES
// =====================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1   // no reset pin
#define OLED_ADDRESS 0x3C   // typical SSD1306 I2C address

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RoboEyes<Adafruit_SSD1306> roboEyes(display);

// =====================
// CONFIGURACIÓN DE PINES
// =====================

// Motor A
const int IN1 = 32;
const int IN2 = 33;
const int ENA = 14;

// Motor B
const int IN3 = 27;
const int IN4 = 26;
const int ENB = 25;

// =====================
// CONFIG PWM
// =====================

const int freq       = 1000;
const int resolution = 8;
const int chA        = 0;
const int chB        = 1;

// =====================
// PARÁMETROS DE VELOCIDAD
// =====================

const int minSpeed   = 60;   // Velocidad mínima útil del motor (para no-cero)
const int maxSpeed   = 255;  // Velocidad máxima

int currentSpeed = 150;      // Velocidad actual (ajustable con slider SL1)

// =====================
// BUFFER BLUETOOTH
// =====================

String cmdBuffer = "";

// Tiempo del último comando de movimiento recibido.
// Si pasan más de cmdTimeoutMs ms sin un comando → stop de seguridad.
unsigned long lastMoveTime   = 0;
const unsigned long cmdTimeoutMs = 3000;

// =====================
// TOUCH SENSOR (TTP223)
// =====================

const int TOUCH_PIN = 19;

bool isTouched         = false;
unsigned long petTimer = 0;
const unsigned long petCooldown = 3000; // ms feliz tras soltar

// Conteo de taps para reacción especial
int tapCount               = 0;
unsigned long firstTapTime = 0;
const unsigned long tapWindow   = 4000; // ventana de 4 s para contar taps
const int TAPS_FOR_JOY          = 5;

// =====================
// ESTADO BT / SUEÑO OLED
// =====================

bool btConnected  = false;
bool eyesSleeping = false;

unsigned long lastConnectedTime     = 0;
const unsigned long eyeSleepTimeout = 10000; // 10 s sin BT → ojos somnolientos

// Estado de la animación de sueño
enum SleepState { SLEEP_A, STIR_A, SLEEP_B, STIR_B };
SleepState sleepState   = SLEEP_A;
unsigned long sleepStateTimer = 0;
unsigned long sleepMoveTimer  = 0;
bool stirBlinked        = false;

// =====================
// SETUP
// =====================

void setup() {
  Serial.begin(115200);

  // ── OLED ──────────────────────────────────────────────
  Wire.begin();  // SDA=21, SCL=22 (ESP32 defaults)
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] No encontrado — verifica cableado.");
  } else {
    roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100); // 100 fps max framerate
    display.setRotation(0); // no rotation
    roboEyes.setAutoblinker(true);
    roboEyes.setIdleMode(true, 2, 6);  // parpadeo/mirada idle cada 2-6 s
    Serial.println("[OLED] RoboEyes OK");
  }

  // ── Bluetooth ─────────────────────────────────────────
  SerialBT.begin("ESP32-MakersLab-Car");
  Serial.println("Bluetooth iniciado → esperando conexión...");

  // ── Motores ───────────────────────────────────────────
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  ledcSetup(chA, freq, resolution);
  ledcSetup(chB, freq, resolution);

  ledcAttachPin(ENA, chA);
  ledcAttachPin(ENB, chB);

  // ── Touch sensor ──────────────────────────────────────
  pinMode(TOUCH_PIN, INPUT);

  stopMotors();
  lastConnectedTime = millis(); // countdown empieza desde el boot
}

// =====================
// LOOP PRINCIPAL
// =====================

void loop() {
  // Leer bytes disponibles por Bluetooth
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();

    if (c == '\n') {
      processCommand(cmdBuffer);
      cmdBuffer = "";
    } else if (c != '\r') {
      cmdBuffer += c;
    }
  }

  // Stop de seguridad: sin comando de movimiento por más de cmdTimeoutMs
  if (lastMoveTime > 0 && (millis() - lastMoveTime > cmdTimeoutMs)) {
    stopMotors();
    lastMoveTime = 0;
  }

  // ── Sensor táctil (TTP223) ────────────────────────────
  bool touchNow = digitalRead(TOUCH_PIN);
  if (touchNow && !isTouched)  onPetStart();
  if (!touchNow && isTouched) { onPetEnd(); petTimer = millis(); }
  isTouched = touchNow;
  if (!isTouched && petTimer > 0 && (millis() - petTimer > petCooldown)) {
    petTimer = 0;
    onPetDone();
  }

  // ── Gestión conexión BT / sueño OLED ──────────────────
  bool nowConnected = SerialBT.hasClient();

  if (nowConnected && !btConnected) {
    // BT recién conectado → despertar OLED
    btConnected = true;
    eyesWake();
  } else if (!nowConnected && btConnected) {
    // BT recién desconectado → motores off, ojos cansados, iniciar cuenta
    btConnected = false;
    lastConnectedTime = millis();
    stopMotors();
    roboEyes.setMood(TIRED);
  }

  if (!btConnected && !eyesSleeping &&
      (millis() - lastConnectedTime > eyeSleepTimeout)) {
    eyesSleep();
  }

  if (eyesSleeping) updateSleepCycle();

  roboEyes.update();

  // Dibujar zzz animado encima de los ojos si está en modo sueño
  if (eyesSleeping) {
    drawZzz();
  }
}

// =====================
// CARICIA / PET
// =====================

void onPetStart() {
  // Despertar suave si estaba durmiendo
  if (eyesSleeping) {
    eyesWake();
    if (!btConnected) lastConnectedTime = millis();
    Serial.println("[TOUCH] Despertado por caricia");
    return; // solo despertar, no contar como caricia todavía
  }

  if (!btConnected) lastConnectedTime = millis();

  // Conteo de taps para reacción especial
  unsigned long now = millis();
  if (tapCount == 0 || (now - firstTapTime > tapWindow)) {
    tapCount    = 1;
    firstTapTime = now;
  } else {
    tapCount++;
  }

  if (tapCount >= TAPS_FOR_JOY) {
    tapCount = 0;
    onJoyReaction();
    return;
  }

  // Caricia normal
  roboEyes.setMood(HAPPY);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setHeight(18, 18);  // ojos entrecerrados de placer
  roboEyes.blink();
  Serial.println("[TOUCH] Caricia detectada");
}

void onJoyReaction() {
  roboEyes.setMood(HAPPY);
  roboEyes.setHeight(36, 36);
  roboEyes.setPosition(DEFAULT);
  roboEyes.laughAnimationDuration = 2000; // 2 s de rebote (default 500ms)
  roboEyes.anim_laugh();         // ojos rebotan arriba y abajo
  if (!btConnected) lastConnectedTime = millis();
  Serial.println("[TOUCH] ¡Reaccion de alegria!");
}

void onPetEnd() {
  roboEyes.setHeight(36, 36);  // restaurar altura completa
  Serial.println("[TOUCH] Caricia terminada");
}

void onPetDone() {
  if (btConnected) {
    roboEyes.setMood(DEFAULT);
  } else if (!eyesSleeping) {
    roboEyes.setMood(TIRED);   // volver a cansado si BT desconectado
  }
  Serial.println("[TOUCH] Vuelta al estado normal");
}

// =====================
// SUEÑO / DESPERTAR OLED
// =====================

void eyesSleep() {
  eyesSleeping    = true;
  sleepState      = SLEEP_A;
  sleepStateTimer = millis();
  sleepMoveTimer  = millis();
  stirBlinked     = false;
  roboEyes.setIdleMode(false);
  roboEyes.setMood(TIRED);
  roboEyes.setFramerate(15); // movimiento lento durante sueño
  roboEyes.setHeight(4, 4);  // ojos casi cerrados
  roboEyes.eyeLxNext = 0;    // ojos izquierda → zzz derecha
  roboEyes.eyeLyNext = 38;
  Serial.println("[OLED] Entrando en sueño");
}

void eyesWake() {
  eyesSleeping = false;
  roboEyes.setFramerate(100); // restaurar velocidad normal
  roboEyes.setHeight(36, 36);
  roboEyes.setMood(HAPPY);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setIdleMode(true, 2, 6);
  roboEyes.blink();
  Serial.println("[OLED] Despertando");
}

// Máquina de estados del ciclo de sueño
void updateSleepCycle() {
  unsigned long now = millis();

  switch (sleepState) {

    // ── FASE A: zzz derecha, ojos izquierda ──────────────
    case SLEEP_A:
      // deriva muy sutil dentro de la zona izquierda
      if (now - sleepMoveTimer > 4000) {
        sleepMoveTimer = now;
        roboEyes.eyeLxNext = random(3, 8);
        roboEyes.eyeLyNext = random(34, 40);
      }
      if (now - sleepStateTimer > 6000) {
        sleepState  = STIR_A;
        sleepStateTimer = now;
        stirBlinked = false;
        roboEyes.setHeight(18, 4);   // ojo izquierdo se entreabre
        roboEyes.eyeLxNext = 12;     // deriva levemente al centro (sin saltar)
        roboEyes.eyeLyNext = 42;
      }
      break;

    // ── STIR A: ojo izquierdo abre y parpadea ────────────
    case STIR_A:
      if (!stirBlinked) {
        roboEyes.blink(true, false); // parpadea solo el ojo izquierdo
        stirBlinked = true;
      }
      if (now - sleepStateTimer > 2500) {
        sleepState  = SLEEP_B;
        sleepStateTimer = now;
        sleepMoveTimer  = now;
        roboEyes.setHeight(4, 4);    // volver a casi cerrados
        roboEyes.eyeLxNext = 40;     // desliza lentamente a la derecha
        roboEyes.eyeLyNext = 38;
      }
      break;

    // ── FASE B: zzz izquierda, ojos derecha ──────────────
    case SLEEP_B:
      // deriva muy sutil dentro de la zona derecha
      if (now - sleepMoveTimer > 4000) {
        sleepMoveTimer = now;
        roboEyes.eyeLxNext = random(37, 43);
        roboEyes.eyeLyNext = random(34, 40);
      }
      if (now - sleepStateTimer > 6000) {
        sleepState  = STIR_B;
        sleepStateTimer = now;
        stirBlinked = false;
        roboEyes.setHeight(4, 18);   // ojo derecho se entreabre
        roboEyes.eyeLxNext = 28;     // deriva levemente al centro (sin saltar)
        roboEyes.eyeLyNext = 42;
      }
      break;

    // ── STIR B: ojo derecho abre y parpadea ──────────────
    case STIR_B:
      if (!stirBlinked) {
        roboEyes.blink(false, true); // parpadea solo el ojo derecho
        stirBlinked = true;
      }
      if (now - sleepStateTimer > 2500) {
        sleepState  = SLEEP_A;
        sleepStateTimer = now;
        sleepMoveTimer  = now;
        roboEyes.setHeight(4, 4);    // volver a casi cerrados
        roboEyes.eyeLxNext = 5;      // desliza lentamente a la izquierda
        roboEyes.eyeLyNext = 38;
      }
      break;
  }
}

// Dibuja zzz flotando hacia arriba.
// Lado derecho (x=107-128) cuando ojos están a la izquierda (SLEEP_A / STIR_A).
// Lado izquierdo (x=0-24)  cuando ojos están a la derecha  (SLEEP_B / STIR_B).
void drawZzz() {
  unsigned long t = millis();
  bool onRight = (sleepState == SLEEP_A || sleepState == STIR_A);

  if (onRight) {
    display.fillRect(107, 0, 21, 64, SSD1306_BLACK);
    int xPos[3] = {122, 114, 108};
    display.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 3; i++) {
      unsigned long phase = (t + (unsigned long)i * 1000UL) % 3000UL;
      if (phase < 2000) {
        display.setTextSize(1);
        int y = map((int)phase, 0, 2000, 55, 3);
        display.setCursor(xPos[i], y);
        display.print(i == 0 ? "z" : "Z");
      }
    }
  } else {
    display.fillRect(0, 0, 25, 64, SSD1306_BLACK);
    int xPos[3] = {17, 10, 3};
    display.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 3; i++) {
      unsigned long phase = (t + (unsigned long)i * 1000UL) % 3000UL;
      if (phase < 2000) {
        display.setTextSize(1);
        int y = map((int)phase, 0, 2000, 55, 3);
        display.setCursor(xPos[i], y);
        display.print(i == 0 ? "z" : "Z");
      }
    }
  }
  display.display();
}

// =====================
// PROCESAMIENTO DE COMANDOS
// =====================
//
// Protocolo MakersLab Gamepad (texto + \n):
//   F01  → Adelante        (joystick arriba)
//   B01  → Atrás           (joystick abajo)
//   L01  → Girar izquierda (joystick izquierda)
//   R01  → Girar derecha   (joystick derecha)
//   S00  → Stop            (joystick centrado / soltado)
//   A00  → Botón A         (acción libre)
//   B00  → Botón B         (acción libre)  ← distinto de B01 (atrás)
//   X00  → Botón X         (acción libre)
//   Y00  → Botón Y         (acción libre)
//   SL1:XX → Slider 1      (velocidad directa 0–255; 0 = sin movimiento)
//   P      → Heartbeat ping → responder "K"
// =====================

void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[BT] CMD: ");
  Serial.println(cmd);

  // --- Heartbeat ---
  if (cmd == "P") {
    SerialBT.print("K");
    if (lastMoveTime > 0) lastMoveTime = millis();
    return;
  }

  // Necesitamos al menos 3 caracteres para los comandos de dirección/botón
  if (cmd.length() < 3) return;

  char dir      = cmd.charAt(0);
  String suffix = cmd.substring(1); // "01", "00", "02"

  // --- Comandos de joystick (movimiento) ---
  if (dir == 'F' && suffix == "01") {
    forward(currentSpeed);
    lastMoveTime = millis();
    return;
  }
  if (dir == 'B' && suffix == "01") {
    backward(currentSpeed);
    lastMoveTime = millis();
    return;
  }
  if (dir == 'L' && suffix == "01") {
    turnLeft(currentSpeed);
    lastMoveTime = millis();
    return;
  }
  if (dir == 'R' && suffix == "01") {
    turnRight(currentSpeed);
    lastMoveTime = millis();
    return;
  }
  if (dir == 'S' && suffix == "00") {
    stopMotors();
    lastMoveTime = 0;
    return;
  }

  // --- Slider SL1: mapeo 0–255 → 0 (stop) o minSpeed–maxSpeed ---
  if (cmd.startsWith("SL1:")) {
    int val = cmd.substring(4).toInt();
    currentSpeed = (val == 0) ? 0 : map(val, 1, 255, minSpeed, maxSpeed);
    Serial.print("[BT] Velocidad SL1: ");
    Serial.println(currentSpeed);
    return;
  }

  // --- Botones de cara (A00 / B00 / X00 / Y00) ---
  // Asigna acciones según las necesidades del módulo.
  if (suffix == "00") {
    switch (dir) {
      case 'A': /* acción botón A */ break;
      case 'B': /* acción botón B */ break;
      case 'X': /* acción botón X */ break;
      case 'Y': /* acción botón Y */ break;
      default: break;
    }
  }
}

// =====================
// UTILIDADES
// =====================

int normalizeSpeed(int speed) {
  speed = constrain(speed, -255, 255);
  if (speed == 0) return 0;

  int absSpeed = abs(speed);
  if (absSpeed < minSpeed) absSpeed = minSpeed;

  return (speed > 0) ? absSpeed : -absSpeed;
}

// =====================
// MOTOR GENÉRICO
// =====================

void setMotor(int in1, int in2, int channel, int speed) {
  speed    = normalizeSpeed(speed);
  int pwm  = abs(speed);

  if (speed > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else if (speed < 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  } else {
    // Freno pasivo (LOW/LOW). Cambiar a HIGH/HIGH para freno activo.
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }

  ledcWrite(channel, pwm);
}

// =====================
// MOTORES
// =====================

void motorA(int speed) { setMotor(IN1, IN2, chA, speed); }
void motorB(int speed) { setMotor(IN3, IN4, chB, speed); }

// =====================
// MOVIMIENTOS
// =====================

void forward(int speed) {
  motorA(speed);  motorB(speed);
  roboEyes.setMood(HAPPY);
  roboEyes.setPosition(DEFAULT);
}

void backward(int speed) {
  motorA(-speed); motorB(-speed);
  roboEyes.setMood(TIRED);
  roboEyes.setPosition(DEFAULT);
}

void turnRight(int speed) {
  motorA(speed);  motorB(-speed);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(E);
}

void turnLeft(int speed) {
  motorA(-speed); motorB(speed);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(W);
}

void stopMotors() {
  motorA(0);      motorB(0);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
}
