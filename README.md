# Assignment 3
Brynn Rogers, Josiah Thomas, and Alec Jones

# Server Usage
`./server -p [port] -s [logpath]`
- `port`: The port that the server runs on.
- `logpath`: The path for the logs. The file will be created but the folder must exist beforehand.

# Client Usage
`./client -p [port] -l [logpath] -s [ip]`
- `port`: The port that the server runs on.
- `logpath`: The path for the logs. The file will be created but the folder must exist beforehand.
- `ip`: The IP the server runs on.

# LED Pin Setup
- Wire 1: RPI Pin 11 to board row.
- LED Long to Wire 1.
- Wire 2: RPI Pin 6 to GND board row.
- LED Short to GND board row.

# Motion Detector Pin Setup
Assuming Motion Detector is rotated pins closest. 
- Wire 1: RPI Pin 2 to Detector Left Pin.
- Wire 2: RPI Pin 13 to Detector Middle Pin.
- Wire 3: Detector Right Pin to GND board row.

# Protocol Steps
1. Server binds to port.
2. Client sends `SYN`.
3. Server sends `SYN+ACK`.
4. Client sends `ACK`.
5. Client sends blink parameters. (500ms, 5 times)
6. Server sends `ACK`.
7. Client blocks for motion.
8. Server blinks LED.
9. Server sends `ACK`.
10. Client sends `FIN`.
11. Sever sends `FIN+ACK`.
12. Client exits.
13. Server waits for new connection.