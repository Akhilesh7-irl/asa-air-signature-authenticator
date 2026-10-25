# Firmware track

F0 I2C discovery for MPU6050. F1 reads all six motion channels at 100 Hz (acceleration ±2 g and gyroscope ±250 degrees/second). F2 quantizes acceleration into U/D/L/R and signed gyro rates into GXp/GXn, GYp/GYn, GZp/GZn; P marks a still pause. Per-axis gyro thresholds are learned across the three enrollments. F3 checks equal token counts and positional majority. F4 FSM matcher compares the combined sequence and ends on 2 seconds of silence. F5 lockout counts failed attempts and clears after cooldown. F6 timestamped six-axis JSON telemetry at 50 Hz plus LED and buzzer feedback. F7 tune thresholds and validate on the physical board.

Pins and timing constants live near the top of asa.ino. Default cooldown is 10 seconds for a presentation; change it to 30000 ms for a longer variant. The sketch uses direct Wire register access and requires no sensor library. The browser sends Q after connecting to request board/enrollment state.

Bench checks once hardware is available: keep the board still at startup; verify flat-board magnitude near 1 g; inspect gyro values near zero at rest; make a clear translation followed by a deliberate wrist rotation on each axis; check one token per motion and one pause token; test ten slow and ten fast swipes per direction. Mapping signs depend on board orientation.
