// ============================================================
// Controller.h  —  Motor arbitration (RPi5)
// ============================================================
#pragma once
#include "Logger.h"
#include "Motor.h"

class Controller {
public:
    void begin() {
        motors_.begin();
        logger.println(F("[Controller] Ready."));
    }

    void loop() {}

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

    // Halts motors but preserves speed/steering state — used by Pause
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
        logger.println(F("[Controller] E-STOP"));
    }

    void restart() {
        estopped_ = false;
        speed_    = 0.0f;
        steering_ = 0.0f;
        motors_.stop();
        logger.println(F("[Controller] Restarted."));
    }

    // Call before gpioEnd().
    void close() {
        motors_.stop();
    }

    bool  isEmergencyStopped() const { return estopped_; }
    float speed()              const { return speed_; }
    float steering()           const { return steering_; }
    MotorCommand activeCommand() const { return motors_.activeCommand(); }

private:
    void apply() {
        motors_.apply(MotorPair::mix(speed_, steering_));
    }

    MotorPair motors_;
    float     speed_    = 0.0f;
    float     steering_ = 0.0f;
    bool      estopped_ = false;
};
