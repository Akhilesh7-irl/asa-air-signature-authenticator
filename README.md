# Air-Signature Authenticator (ASA)

A Logic Design course prototype: a local username/password step followed by an ESP32 + MPU6050 air-signature check. The firmware classifies acceleration as U/D/L/R, gyro rotation rate as signed X/Y/Z tokens (GXp/GXn, GYp/GYn, GZp/GZn), and pauses as P. A finite state machine decides acceptance. The local dashboard also plots the live 3-axis acceleration trail and shows the 3 gyro channels.

This is a localhost course demo, not a production identity or security product. The web server validates the browser-reported ASA ACCEPT event; a determined user could forge that request. Production MFA would require a cryptographic challenge-response protocol and protected device keys. Enrollment and lockout state live in RAM on the board and clear after reset. Password hashes are stored locally in data/accounts.json; do not use a real or reused password.

## Included
- server.js — loopback-only local web server, account registration/login, password hashing, short-lived sessions.
- dashboard/index.html — sign-in flow, ASA second-factor gate, Web Serial interface, 3D sensor-space acceleration trail, six-axis readouts, simulation, attempt export.
- firmware/asa/asa.ino — ESP32 sketch for MPU6050 accel/gyro, gesture FSM, enrollment, verification, LEDs, and buzzer.
- docs/ — state diagrams, demonstration guide, evaluation worksheet, firmware/dashboard tracks.

## Requirements
- Node.js 18 or later on Windows or WSL2.
- Arduino IDE with Espressif ESP32 board support for uploading firmware.
- Chrome or Edge for Web Serial when using a real ESP32.

## Start the dashboard
1. Open PowerShell in this asa folder.
2. Run `node server.js`. Keep the terminal open.
3. Open `http://localhost:3000` in Chrome or Edge. Do not open the HTML via file://.
4. On first launch, create the local demo account. Later, use “I already have an account.”
5. Without hardware, choose Play simulation, then Enroll gesture, then Verify gesture. The first scripted verification accepts; later attempts demonstrate rejection and lockout. Simulation exercises the dashboard flow and does not prove sensor accuracy.

## ESP32 + MPU6050 wiring
| Part | ESP32 pin |
|---|---:|
| MPU6050 SDA / SCL | GPIO 21 / 22 |
| MPU6050 VCC / GND | 3.3 V / GND |
| Green LED | GPIO 25 through 220 ohm resistor |
| Red LED | GPIO 26 through 220 ohm resistor |
| Active buzzer | GPIO 27 |

Check the breakout voltage requirements. Firmware expects I2C address 0x68 or 0x69.

## Flash and use the board
1. Open firmware/asa/asa.ino in Arduino IDE, select the ESP32 board and port, then compile and upload.
2. Close Serial Monitor before connecting the browser; only one application can own the port.
3. In the dashboard, choose Connect board and select the ESP32 serial port. Keep the sensor still while it boots.
4. Enroll the same clear gesture three times, then verify it once. Board orientation should stay consistent. Enrollment is lost on ESP32 restart.

## Signal meaning and limits
The sketch samples the MPU6050 at 100 Hz and sends timestamped accelerometer (ax, ay, az in g) and gyroscope (gx, gy, gz in degrees/second) values at 50 Hz over 115200-baud serial. The plotted trace maps measured acceleration samples into a 3D sensor-coordinate view; gravity is included and the plot is not global hand position. Recovering physical position by integrating an IMU drifts quickly because small orientation and sensor errors accumulate. The gesture FSM remains the acceptance decision and now matches both acceleration direction and signed gyro-axis tokens; the vector trail is live telemetry for inspection, not yet an independent biometric matcher.

If the browser shows no sensor, check wiring and I2C address. For a course demonstration, the simulation is available without the ESP32.

See docs/FIRMWARE_TRACK.md, docs/DASHBOARD_TRACK.md, docs/state_diagrams.md, docs/metrics_table.md, and docs/demonstration.md.
