# Three-minute demo

1. Start node server.js and open http://localhost:3000 in Chrome or Edge. Create or sign in to the local demo account.
2. For a no-hardware demonstration, choose Play simulation, enroll the scripted gesture, then verify it. Explain that simulation checks the app flow only. With the ESP32, connect the board and point out six-axis readings and the 3D acceleration trail.
3. Enroll the same gesture three times and wait for each capture confirmation.
4. Verify the enrolled gesture. Show accepted state, token sequence, and green LED. The ASA gate then opens the dashboard.
5. Sign out and repeat with a deliberately different gesture to show rejection; repeated failures demonstrate lockout.
6. Export the attempt log to CSV. Explain that FAR/FRR require genuine and impostor trials, not the scripted demo.

## Before presenting
- Confirm Chrome or Edge. Start the local Node server and keep its terminal open.
- Close Arduino Serial Monitor before connecting the browser.
- Use a data-capable USB cable and bring a spare.
- Confirm LEDs and active buzzer polarity.
- Enroll after every board restart; the template is in RAM.
- Keep board orientation consistent and use clear swipes.
- Identify simulation as a UI fallback, not hardware measurement.
