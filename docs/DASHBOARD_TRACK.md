# Dashboard track

W0 motion-studio dashboard. W1 Web Serial selection and newline-delimited JSON parsing. W2 E/V/R/Q/T commands and gesture enrollment/verification. W3 six-axis telemetry, 3D acceleration trail in sensor coordinates, attempt log, and CSV export. W4 scripted no-hardware demo. W5 local username/password and ASA second-factor flow on the Node loopback server. W6 verify in current Chrome or Edge with a real ESP32.

Run node server.js from the project folder and open http://localhost:3000. The first account registration creates a local password hash file. This is a course demo: the server trusts the browser's ASA acceptance report and is not suitable for real authentication. Browser serial is unavailable in Firefox and Safari. Keep Arduino Serial Monitor closed while connected because the serial port is exclusive.
