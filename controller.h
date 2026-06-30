// ============================================================
// controller.h  —  Motor arbitration (RPi5)
// ============================================================
#pragma once
#include <mutex>
#include "logger.h"
#include "motor.h"

class Controller {
public:
    void begin() {
        motors_.begin();
        logger.println(F("[Controller] Ready."));
    }

    void loop() {}

    // ── Motor control ────────────────────────────────────────

    void setSpeed(float speed) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (estopped_) return;
        speed_ = constrain(speed, -1.0f, 1.0f);
        apply();
    }

    void setSteering(float steering) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (estopped_) return;
        steering_ = constrain(steering, -1.0f, 1.0f);
        apply();
    }

    void setDrive(float steering, float speed) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (estopped_) return;
        steering_ = constrain(steering, -1.0f, 1.0f);
        speed_    = constrain(speed,    -1.0f, 1.0f);
        apply();
    }

    // Halts motors but preserves speed/steering state — used by Pause
    void pause() {
        std::lock_guard<std::mutex> lk(mutex_);
        logger.println(F("[Controller] Paused"));
        motors_.stop();
    }

    // Halts motors and resets all state to safe defaults — used by E-STOP
    void emergencyStop() {
        std::lock_guard<std::mutex> lk(mutex_);
        estopped_ = true;
        speed_    = 0.0f;
        steering_ = 0.0f;
        motors_.stop();
        logger.println(F("[Controller] E-STOP"));
    }

    void restart() {
        std::lock_guard<std::mutex> lk(mutex_);
        estopped_ = false;
        speed_    = 0.0f;
        steering_ = 0.0f;
        motors_.stop();
        logger.println(F("[Controller] Restarted."));
    }

    // Call before gpioEnd().
    void close() {
        std::lock_guard<std::mutex> lk(mutex_);
        motors_.stop();
    }

    bool  isEmergencyStopped() const { std::lock_guard<std::mutex> lk(mutex_); return estopped_; }
    float speed()              const { std::lock_guard<std::mutex> lk(mutex_); return speed_; }
    float steering()           const { std::lock_guard<std::mutex> lk(mutex_); return steering_; }
    MotorCommand activeCommand() const { std::lock_guard<std::mutex> lk(mutex_); return motors_.activeCommand(); }

private:
    void apply() {
        motors_.apply(MotorPair::mix(speed_, steering_));
    }

    mutable std::mutex mutex_;
    MotorPair motors_;
    float     speed_    = 0.0f;
    float     steering_ = 0.0f;
    bool      estopped_ = false;
};
