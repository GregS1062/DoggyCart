// ============================================================
// Controller.h  —  Motor + turn-signal arbitration (ESP32)
// ============================================================
#pragma once
#include "Logger.h"
#include "Motor.h"
#include "TurnSignals.h"

class Controller {
public:
    void begin() {
        motors_.begin();
        lights_.begin();
        logger.println(F("[Controller] Ready."));
    }

    // Call from loop() — drives turn-signal blink timing
    void loop() {
        lights_.loop();
    }

    // ── Motor control ────────────────────────────────────────

    void setSpeed(float speed) {
        if (estopped_) return;
        speed_ = constrain(speed, -1.0f, 1.0f);
        apply();
    }

    void setSteering(float steering) {
        if (estopped_) return;
        steering_ = constrain(steering, -1.0f, 1.0f);
        apply();
    }

    void setDrive(float steering, float speed) {
        if (estopped_) return;
        steering_ = constrain(steering, -1.0f, 1.0f);
        speed_    = constrain(speed,    -1.0f, 1.0f);
        apply();
    }

    // Halts motors but preserves speed/steering/lights state — used by Pause
    void pause() {
        logger.println(F("[Controller] Paused"));
        motors_.stop();
    }

    // Halts motors and resets all state to safe defaults — used by E-STOP
    void emergencyStop() {
        estopped_ = true;
        speed_    = 0.0f;
        steering_ = 0.0f;
        motors_.stop();
        lights_.setMode(TurnSignals::Mode::OFF);
        logger.println(F("[Controller] E-STOP"));
    }

    void restart() {
        estopped_ = false;
        speed_    = 0.0f;
        steering_ = 0.0f;
        motors_.stop();
        logger.println(F("[Controller] Restarted."));
    }

    bool  isEmergencyStopped() const { return estopped_; }
    float speed()              const { return speed_; }
    float steering()           const { return steering_; }
    MotorCommand activeCommand() const { return motors_.activeCommand(); }

    // ── Lights control ───────────────────────────────────────

    void setLightsMode(TurnSignals::Mode m) { lights_.setMode(m); }
    TurnSignals::Mode lightsMode()    const { return lights_.mode(); }
    const char*       lightsModeStr() const { return lights_.modeName(); }

private:
    void apply() {
        motors_.apply(MotorPair::mix(speed_, steering_));
    }

    MotorPair   motors_;
    TurnSignals lights_;
    float       speed_    = 0.0f;
    float       steering_ = 0.0f;
    bool        estopped_ = false;
};
