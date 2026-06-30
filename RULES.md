# DoggyCart — Design Rules

Read this before making any change. If a proposed change conflicts with a rule, stop and ask.

---
prerequisites 

in the [all] section of /boot/firmware/config.txt Add:
dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4   


## Hardware

1. **Board:** RPI 5 4GB
2. **Power:** OVONIC Lipo Battery 850mAh 80C 11.1V 3S with XT30 Connector → MP1584EN DC-DC Buck Converter → 5 V → RPI 5 VIN; battery also → L298N motor supply directly
3. **Motor driver:** L298N; internal 5 V regulator is bypassed — RPI 5 powered from buck converter
4. **WiFi:** Soft AP, SSID `DoggyCart`, port 8080, IP 192.168.4.1. Setup is OS-level (`hostapd` + `dnsmasq`); application code does not configure WiFi.
5. **GPIO chip:** RPi5 uses `/dev/gpiochip4`. RPi3/4 use `/dev/gpiochip0`. Override with `-DGPIO_CHIP=0` in the Makefile if targeting older boards.
6. **Hardware PWM:** `dtoverlay=pwm-2chan` must be in `/boot/firmware/config.txt`. Only GPIO 12 (PWM0) and GPIO 13 (PWM1) use hardware PWM via Linux sysfs; all other PWM pins use lgpio software PWM. Never call `lgGpioClaimOutput` on pins 12 or 13.
7. **Servo:** signal → BCM GPIO 2 (physical pin 3); 5 V → physical pin 2 or 4; GND → physical pin 9. Pulse range: 1250 µs (45°, left limit) to 1667 µs (120°, right limit), center 1458 µs. Standard RC range is 1000–2000 µs; the code deliberately limits to the narrower sweep.

---

## GPIO wiring (L298N H-bridge)

Each motor bank uses a single 3-pin Dupont connector wired to three consecutive left-column (odd) header pins.

L298N pin order: **ENA → IN1 → IN2 → IN3 → IN4 → ENB**. Code constants use these exact labels.

Right bank — 3 individual wires (ENA on pin 32 cannot share a connector with IN1/IN2 on pins 13/15):

| Physical | BCM GPIO | L298N | Constant |
|---|---|---|---|
| 32 | GPIO 12 | ENA (hardware PWM0) | `RIGHT_ENA` |
| 13 | GPIO 27 | IN1 | `RIGHT_IN1` |
| 15 | GPIO 22 | IN2 | `RIGHT_IN2` |

Left bank — 3-pin Dupont connector at physical pins 29-31-33:

| Physical | BCM GPIO | L298N | Constant |
|---|---|---|---|
| 29 | GPIO  5 | IN3 | `LEFT_IN3` |
| 31 | GPIO  6 | IN4 | `LEFT_IN4` |
| 33 | GPIO 13 | ENB (hardware PWM1) | `LEFT_ENB` |

---

## Motor behavior

1. **Direction reversal (single motor per channel):** Use `reversed=true` in the `Motor` constructor to flip a motor's physical direction. Do not negate values in `mix()`.
2. **4-motor wiring:** Each side has two TT motors (front and rear) wired in parallel to a single L298N channel. Front motors are mounted with the axle forward; rear motors are mounted with the axle reversed. To compensate, front motors are wired in reverse polarity relative to rear motors on the same channel. This is a physical wiring fix — `reversed=true` cannot solve it because both motors share one channel. Do not change this wiring without verifying that all four wheels roll in the same direction for a given drive command.
3. **PWM / throttle formula:** Actual PWM duty = `(0.20 + 0.80 × |throttle|) × 255`. The 20% floor is applied in `motor.h`, not in the controller or UI layer.
4. **Dead zone:** `|speed| < 0.10` → `MotorCommand::stopped()` is returned by `mix()` before the 20% floor applies. No power to either motor.
5. **Steering mix:** `steer = steering × (1 − |speed|)`; `left = speed + steer`; `right = speed − steer`. Steering effect scales inversely with speed to prevent flipping. At low speed with hard steering the inner motor reverses, producing a pivot turn.

---

## Pause vs E-STOP — these are distinct and must stay distinct

| Method | Server effect | When to use |
|---|---|---|
| `Controller::pause()` | Halts motors only. Speed and steering state preserved. | Pause button |
| `Controller::emergencyStop()` | Halts motors AND resets speed=0, steering=0. Sets `estopped_=true`. | E-STOP button |
| `Controller::restart()` | Clears estop flag, zeros speed/steering, stops motors. | Start / Resume / Clear E-STOP |

1. Never merge `pause()` and `emergencyStop()`. `pause()` is for temporary halts with full resume. `emergencyStop()` is a safety action that wipes state.
2. All motor-command methods (`setDrive`, `setSpeed`, `setSteering`) silently return when `estopped_=true`. The server enforces this gate.

---

## API routes — each has one job

| Route | Calls | Used by |
|---|---|---|
| `/api/pause`    | `car_.pause()`                       | Pause button (preserves state) |
| `/api/estop`    | `car_.emergencyStop()`               | E-STOP button (resets state) |
| `/api/start`    | `car_.restart()`                     | Start button (fresh start) |
| `/api/resume`   | `car_.restart()`                     | Pause→Resume (client resends saved state) |
| `/api/drive`    | `car_.setDrive(steer, speed)`        | Slider input (blocked while estopped) |
| `/api/speed`    | `car_.setSpeed(value)`               | Single-axis speed update (blocked while estopped) |
| `/api/steer`    | `car_.setSteering(value)`            | Single-axis steering update (blocked while estopped) |
| `/api/status`   | Returns JSON `{emergencyStopped}`    | syncStatus polling |
| `/api/track`    | `locator_.startPan()` / `stopPan()` | Track button (`?on=1` / `?on=0`) |
| `/api/photos`   | Lists recent JPGs as JSON            | Photo button |
| `/api/log`      | Returns log file as text             | View Log button |
| `/api/startlog` | `logger.enable()`                    | Start Log button |
| `/api/clearlog` | `logger.clear()`                     | Clear Log button |

1. Do not consolidate `/api/pause` and `/api/estop` — they have different server-side effects.
2. All routes use **HTTP GET**. Parameters are query strings. Do not add POST routes — the UI uses fire-and-forget `fetch(url)` with no method options.

---

## State ownership

1. **Client (JS)** owns the UI state machine: `appState` ∈ {initial, running, paused, estopped}.
2. **Client** saves and restores speed/steering on Pause/Resume via `saveState()` / `restoreState()`.
3. **Server** enforces the estop gate — motor commands are silently ignored when `estopped_=true`.
4. **UI is fire-and-forget** — JS sends commands and mirrors expected state locally; only `syncStatus` polls the server (every 2 s via `setInterval`).

---

## Speed and Steering

1. Sliders use `onchange` events, not `oninput`, to prevent spamming the web server.
2. Speed range: 0–100 (step 5) in the UI, mapped to 0.0–1.0 server-side. Direction buttons multiply speed by ±1 before the API call.
3. Steering range: 0–100 (step 5), initial value 50. Mapped to −1.0 to +1.0 server-side. Values 45–55 produce near-zero differential (straight ahead).
4. Speed dead zone: `|speed| < 0.10` → no power to either motor (see Motor behavior §3).
5. Steering effect decreases as speed increases; at full speed the effect is zero (see Motor behavior §4).

---

## IPC — scan.h / tracking.py

1. **FIFO ownership:** `scan.h::begin()` creates both FIFOs before `tracking.py` is launched. `tracking.py` only opens them. Do not move FIFO creation to `tracking.py`.
2. **Single launcher:** `main.cpp` launches `tracking.py` via `fork()`+`exec()` (not `std::system()` — that backgrounds it with `&` and loses the PID, leaving it orphaned and still holding the camera after shutdown), using the `[Activate] env` / `python` values from `/etc/DoggyCart/config.ini` (this activates the venv with the YOLO deps). The returned PID is `SIGTERM`'d (then `SIGKILL`'d after a timeout) on shutdown so the camera is released. `scan.h` does not fork/exec it — do not add a second launch there or start it manually/as a systemd service; a competing instance will corrupt the FIFO stream.
3. **FIFO paths are a fixed contract:** `/tmp/doggycart_data` and `/tmp/doggycart_cmd` are hardcoded in both `scan.h` and `tracking.py`. Renaming either requires changing both.
4. **JPG directory must match config.ini:** `webServer.h` hardcodes `JPG_DIR`. `tracking.py` reads `jpgsPath` from `/etc/DoggyCart/config.ini`. Both must point to the same directory.
5. **IPC protocol:**
   - `tracking.py → scan.h` via `doggycart_data`: first message is `"hello"` (ready handshake); thereafter one normalised float per line (e.g. `"-0.3200\n"` = 32% left of frame centre; range −1.0 … +1.0).
   - `scan.h → tracking.py` via `doggycart_cmd`: `"SEND\n"` = request next detection; `"WAIT\n"` = person centred, pause sending; `"ack\n"` = person off-centre, send next detection.
6. **Detection thresholds:** Only YOLO detections with confidence > 0.80 are used. Only `"person"` / `"human"` class. Centred threshold: |offset| < 0.08. Lock timeout: 500 ms without a signal → resume scan.

---

## Tracking state machine (Locate)

States: `IDLE` → `PANNING` ↔ `LOCKED`

1. **IDLE:** scanning not active. No servo sweep, no motor commands from Locate.
2. **PANNING:** no lock — servo sweeps 1250–1667 µs in 15 µs steps every 20 ms. If a person is detected off-centre, servo and motors steer toward the person while remaining in PANNING.
3. **LOCKED:** person centred (|offset| < 0.08) — car drives forward at `FOLLOW_SPEED = 0.40`. If no detection for 500 ms, motors stop and state returns to PANNING.
4. Locate calls `controller_->setDrive()`, which is blocked while `estopped_=true`. Tracking only has effect when the car is started.
5. E-STOP from the UI also disables tracking: JS sets `tracking=false` and calls `/api/track?on=0`.
6. When Track is toggled off manually, `sendDrive()` is called immediately with the current slider values to restore manual control.

---

## Lifecycle and deployment

1. **Must run as root:** `sudo ./doggycart` — lgpio requires root for `/dev/gpiochip4`.
2. **Shutdown sequence (order matters):** `server.stop()` → `locator.close()` → `car.close()` → `gpioEnd()`. `car.close()` stops motors before GPIO is released. Reversing the order leaves motors running while the chip is closed.
3. **Main loop rate:** `delay(10)` in `main.cpp` gives ~100 Hz. Do not add blocking calls inside `Controller::loop()` or `Locate::loop()`.
4. **Logging is off by default.** Logger always prints to stdout. File logging starts only after `/api/startlog` is called. Log file: `/tmp/doggycart.log`, capped at 8192 bytes (cleared on overflow with a notice).

---

## UI Layout

```
┌─────────────────────────────────────┐
│         DoggyCart Controller        │
├──────────┬──────────┬───────────────┤
│  Start   │  Pause   │    E-STOP     │
├──────────┴──────────┴───────────────┤
│ Steering │     [slider]             │
├──────────┼──────────┬───────────────┤
│ Direction│  Forward │   Backward    │
├──────────┴──────────┴───────────────┤
│ Speed    │     [slider]             │
├──────────┬──────────┬───────────────┤
│ Track    │  Photo   │    Refresh    │
├──────────┼──────────┼───────────────┤
│ Start Log│ View Log │   Clear Log   │
└──────────┴──────────┴───────────────┘
[log output — hidden until View Log clicked]
```

---

## UI behaviors

Button color key: yellow `#E7F527` = idle/ready; green `#0c0` = active/on; blue `#00f` = paused; red `#c00` = danger.

**Start**
1. Idle color: yellow. Running color: green.
2. From initial or estopped: calls `/api/start`, resets sliders to zero, direction to Forward.
3. From paused: calls `/api/resume` and restores saved speed/steering/direction.

**Pause**
1. Idle color: yellow. Paused color: blue. Second click (resume) returns to yellow.
2. On pause: saves current speed/steering/direction via `saveState()`, calls `/api/pause`. Motors stop; server state is preserved.
3. On resume: calls `/api/resume`, restores sliders, resends drive command.

**E-STOP**
1. Idle color: red `#c00` (always distinct — never yellow). Triggered color: yellow `#ff0`.
2. On trigger: calls `/api/estop`, disables tracking (`/api/track?on=0`), sets `appState='estopped'`.
3. On second click (clear): calls `/api/start`, resets to initial state, returns button to red.
4. Start button also clears E-STOP (same server effect as second click on E-STOP).

**Direction (Forward / Backward)**
1. Active button: green. Inactive button: yellow.
2. Sets `direction` variable (±1) which multiplies speed in `sendDrive()`. Immediately resends the current drive command.

**Steering / Speed sliders**
1. Use `onchange` (not `oninput`). Only fire when the user releases the slider.
2. Slider changes are ignored (`sendDrive` is not called) while `tracking=true`.

**Track**
1. Idle color: yellow. Active color: green.
2. On activate: calls `/api/track?on=1`. Manual slider-driven drive commands are suppressed.
3. On deactivate: calls `/api/track?on=0`, then immediately calls `sendDrive()` with current slider values.

**Photo**
1. Toggles the photo panel. On show: fetches `/api/photos`, renders up to 20 most-recent thumbnails sorted by mtime. Second click hides the panel.

**Refresh**
1. Manually triggers `syncStatus()`, which fetches `/api/status` and updates the status indicator.
2. `syncStatus` also runs automatically every 2 s.

**Start Log / View Log / Clear Log**
1. Start Log: enables file logging; button becomes disabled and relabeled "Logging" to prevent double-enable.
2. View Log: fetches `/api/log` and shows content in a scrollable panel (hidden by default, left-justified).
3. Clear Log: calls `/api/clearlog`, clears the display panel, hides it.

---

## Testing

1. `Test.h` provides a hardware + logic test suite. To use: replace `server.begin()` with `runTests(car)` in `main.cpp`. The suite runs once then halts in `while(true)`. Never ship with `runTests` active.
2. `testMix()` requires no hardware. `testControllerState()` verifies E-STOP gating without moving motors. `testMotorHardware()` physically drives the cart — place it on a stand first.
