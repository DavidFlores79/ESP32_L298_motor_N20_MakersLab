# ESP32 L298 Motor N20 — MakersLab Robot

Arduino sketch for a two-wheeled robot controlled via Classic Bluetooth from the **MakersLab** Flutter app.

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32 (Classic Bluetooth) |
| Motor driver | L298N dual H-bridge |
| Motors | 2× N20 DC gear motors |
| Display | SSD1306 OLED 128×64 (I2C, address 0x3C) |
| Touch sensor | TTP223 capacitive touch (GPIO 19) |

### Pin Mapping

| Signal | GPIO |
|---|---|
| Motor A IN1 | 32 |
| Motor A IN2 | 33 |
| Motor A ENA (PWM) | 14 |
| Motor B IN3 | 27 |
| Motor B IN4 | 26 |
| Motor B ENB (PWM) | 25 |
| OLED SDA | 21 (ESP32 default) |
| OLED SCL | 22 (ESP32 default) |
| Touch sensor | 19 |

## Libraries Required

- `BluetoothSerial` (ESP32 Arduino core)
- `Adafruit GFX`
- `Adafruit SSD1306`
- [`FluxGarage_RoboEyes`](https://github.com/FluxGarage/RoboEyes)

## Bluetooth Protocol

Bluetooth device name: **`ESP32-MakersLab-Car`**

Commands are newline-terminated ASCII strings sent from the MakersLab gamepad app:

| Command | Action |
|---|---|
| `F01` | Forward |
| `B01` | Backward |
| `L01` | Turn left |
| `R01` | Turn right |
| `S00` | Stop |
| `SL1:XX` | Set speed (0–255) |
| `A00` / `B00` / `X00` / `Y00` | Face buttons (free assignment) |
| `P` | Heartbeat ping → replies `K` |

Speed is mapped from the slider value to the `[60, 255]` PWM range. A safety timeout stops motors if no movement command is received for 3 seconds.

## OLED Animated Eyes

The display runs the **RoboEyes** library and reacts to robot state:

| State | Eyes |
|---|---|
| Moving forward | Happy |
| Moving backward | Tired |
| Turning left/right | Gaze left/right |
| Idle / stopped | Default |
| BT disconnected (10 s) | Sleep mode with animated zzz |
| Wake from sleep (BT reconnect or touch) | Wide open + blink animation, then idle |
| Touch detected (awake) | Happy + squint; 5 taps → laugh animation |
| BT reconnected | Wide open + blink, then happy idle |
