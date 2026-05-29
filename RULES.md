# DoggyCart — Design Rules

Read this before making any change. If a proposed change conflicts with a rule, stop and ask.

---

## Hardware

- **Board:** RPI 5 4GB
- **Power:** 3× 18650 in series (9–12.6 V) →  MP1584EN DC-DC Buck Converter → 5 V → RPI 5 VIN; battery also → L298N motor supply directly
- **Motor driver:** L298N; internal 5 V regulator is bypassed — RPI 5 powered from buck converter
- **WiFi:** Soft AP, SSID `DoggyCart`, port 8080, IP 192.168.4.1

---

## GPIO constraints


// WIRING (L298N H-bridge)
// Each motor bank uses a single 3-pin Dupont connector wired to
// three consecutive left-column (odd) header pins.
//
// Right bank — 3 individual wires (ENA on pin 32 cannot share a connector
//              with IN1/IN2 on pins 13/15):
//   physical 32  BCM GPIO 12  →  ENA  (hardware PWM0)
//   physical 13  BCM GPIO 27  →  IN1
//   physical 15  BCM GPIO 22  →  IN2
//
// Left bank — 3-pin Dupont connector at physical pins 29-31-33:
//   physical 29  BCM GPIO  5  →  IN3
//   physical 31  BCM GPIO  6  →  IN4
//   physical 33  BCM GPIO 13  →  ENB  (hardware PWM1)

---

## Pause vs E-STOP — these are distinct and must stay distinct

| Method | Server effect | When to use |
|---|---|---|
| `Controller::pause()` | Halts motors only. Speed, steering| Pause button |
| `Controller::emergencyStop()` | Halts motors AND resets speed=0, steering=0, | E-STOP button |
| `Controller::restart()` | Clears estop flag, zeros speed/steering, stops motors. | Start / Resume / Clear E-STOP |

**Never merge these.** `pause()` is for temporary halts with full resume. `emergencyStop()` is a safety action that wipes state.

---

## API routes — each has one job

| Route | Calls | Used by |
|---|---|---|
| `/api/pause`  | `car_.pause()`         | Pause button (preserves state) |
| `/api/estop`  | `car_.emergencyStop()` | E-STOP button (resets state) |
| `/api/start`  | `car_.restart()`       | Start button (fresh start) |
| `/api/resume` | `car_.restart()`       | Pause→Resume (client resends saved state) |
| `/api/drive`  | `car_.setDrive()`      | Slider input (blocked while estopped) |
| `/api/refresh`| `/api/status`          | Refresh UI |

Do not consolidate `/api/pause` and `/api/estop` — they have different server-side effects.

---

## State ownership

- **Client (JS)** owns the UI state machine: `appState` ∈ {initial, running, paused, estopped}
- **Client** saves and restores speed/steering on Pause/Resume via `saveState()` / `restoreState()`
- **Server** enforces the estop gate — motor commands are silently ignored when `estopped_=true`
- **UI is fire-and-forget** — JS sends commands and mirrors expected state locally; only Refresh polls the server

---

## Speed and Steering behavior

- Speed and steering web controls (sliders) must use onchange rather than oninput events.  This is to prevent webpage from spamming webserver.
- Steering is done by differential motor speed. As speed increases, steering effect decreases to prevent flipping.
- `mix()` formula: `steer = steering × (1 − |speed|)`; left = speed + steer; right = speed − steer
- Speed dead zone: `|speed| < 0.10` → no power to either motor (prevents straining without moving)
- At low speed with hard steering the inner motor reverses, producing a pivot turn

---

## UI Layout

```
┌─────────────────────────────────────┐
│         DoggyCart Controller        │
├──────────┬──────────┬───────────────┤
│  Start   │  Pause   │    E-STOP     │   ← red
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

## UI behaviors

- Fire-and-forget: JS sends commands and updates its own state; no polling except the 2 s syncStatus
- JavaScript mirrors server state locally — no round-trips needed for button feedback

Start:
    Color: yellow while idle, green when running
    Initial startup.
    Starts controller after Pause and E-Stop
Pause:
    Color: yellow while idle, toggles to blue, toggles back to yellow.
    When toggled on, motors and servo are stopped but all states are preserved.
    when toggled off, motors and servo states are resumed.
E-Stop:
    Color: yellow while idle, red when toggled on.  
    Start or E-Stop toggle will resume.  No state is preserved.
Steering:
    Range 0 to 100.
    Intial state is 50.
    Straight ahead (equal power to both sides) will be between 45 and 55.
    Differential power between motors decreases a speed increases.
Speed:
    Range 0 to 100;
    0 = no power to wheels. 20% power is added beyond 0 to apply enough intial energy to move vehicle/
Track:
    Manual control is the default.
    Tracking controls speed and steering of vehicle.
    Toggling Track button, starts tracking and toggling off reverts to manual control.
    When Track is selected speed and direction are initially inherited from manual state.
Photo: (hidden array)
    tracking.py stores photos in /jpg directory
    Pressing Photo displays an array of the most recent photos in a table row that is not visible by default.
Refresh:
    Sincle the ui.h is a fire and forget webpage, it needs to be periodically refreshed from the states held on the server.
Start Log:
    Logging is not the default.
    Toggling log starts logging.
View Log: (hidden by default)
    When pressed, log is read from the log file and displayed.
    Log is left justified.
Clear Log:
    Log is cleared
    Log viewing panel is hidden.

