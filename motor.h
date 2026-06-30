// ============================================================
// motor.h  —  RPi5 differential-drive motors (lgpio software PWM)
// ============================================================
//
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
//
// ============================================================

#pragma once
#include <algorithm>
#include <cmath>
#include "gpio.h"
#include "logger.h"

template<typename T>
inline T constrain(T v, T lo, T hi) { return std::clamp(v, lo, hi); }

constexpr uint8_t RIGHT_ENA  = 12;   // ENA  — hardware PWM0, physical pin 32
constexpr uint8_t RIGHT_IN1  = 27;   // IN1,              physical pin 13
constexpr uint8_t RIGHT_IN2  = 22;   // IN2,              physical pin 15
constexpr uint8_t LEFT_IN3   =  5;   // IN3,              physical pin 29
constexpr uint8_t LEFT_IN4   =  6;   // IN4,              physical pin 31
constexpr uint8_t LEFT_ENB   = 13;   // ENB  — hardware PWM1, physical pin 33

struct MotorCommand {
    float left  = 0.0f;
    float right = 0.0f;
    static MotorCommand stopped() { return {0.0f, 0.0f}; }
};

class Motor {
public:
    Motor(uint8_t enPin, uint8_t in1Pin, uint8_t in2Pin, bool reversed = false)
        : en_(enPin), in1_(in1Pin), in2_(in2Pin), reversed_(reversed) {}

    void begin() {
        pinMode(in1_, OUTPUT);
        pinMode(in2_, OUTPUT);
        ledcAttach(en_, 1000, 8);   // 1 kHz, 8-bit resolution (core 3.x API)
        brake();
    }

    void set(float throttle) {
        throttle_ = constrain(throttle, -1.0f, 1.0f);
        float eff = reversed_ ? -throttle_ : throttle_;
        logger.printf("[Motors] en=%d throttle=%.2f eff=%.2f in1=%d in2=%d",
                      en_, throttle_, eff,
                      (eff > 0) ? 1 : 0,
                      (eff < 0) ? 1 : 0);

        if (eff > 0.0f) {
            logger.printf("[Motor] forward");
            digitalWrite(in1_, HIGH);
            digitalWrite(in2_, LOW);
        } else if (eff < 0.0f) {
            logger.printf("[Motor] reversed");
            digitalWrite(in1_, LOW);
            digitalWrite(in2_, HIGH);
        } else {
            brake();
            return;
        }
        ledcWrite(en_, (int)((0.20f + 0.80f * fabs(throttle_)) * 255.0f));
    }

    void brake() {
        logger.printf("[Motor] en=%d brake", en_);
        digitalWrite(in1_, LOW);
        digitalWrite(in2_, LOW);
        ledcWrite(en_, 0);
        throttle_ = 0.0f;
    }

    float throttle() const { return throttle_; }

private:
    uint8_t en_, in1_, in2_;
    bool    reversed_;
    float   throttle_ = 0.0f;
};

class MotorPair {
public:
    MotorPair()
        : right_(RIGHT_ENA, RIGHT_IN1, RIGHT_IN2, false)
        , left_ (LEFT_ENB,  LEFT_IN3,  LEFT_IN4,  false)
    {}

    void begin() { right_.begin(); left_.begin(); }

    void apply(const MotorCommand& cmd) {
        left_.set(cmd.left);
        right_.set(cmd.right);
    }

    void stop() { left_.brake(); right_.brake(); }

    // speed    : -1.0 (full reverse) to +1.0 (full forward)
    // steering : -1.0 (full left)   to +1.0 (full right)
    //
    // Rules (from RULES.md):
    //   |speed| < 0.10 → dead zone, no power
    //   steering effect scales inversely with speed to prevent flipping at high speed
    //   at low speed + hard steering the inner motor reverses (pivot turn)
    static MotorCommand mix(float speed, float steering) {
        speed    = constrain(speed,    -1.0f, 1.0f);
        steering = constrain(steering, -1.0f, 1.0f);

        if (fabs(speed)    < 0.10f) return MotorCommand::stopped();
        if (fabs(steering) < 0.05f) steering = 0.0f;

        float steer = steering * (1.0f - fabs(speed));
        MotorCommand cmd;
        cmd.left  = constrain(speed + steer, -1.0f, 1.0f);
        cmd.right = constrain(speed - steer, -1.0f, 1.0f);
        return cmd;
    }

    MotorCommand activeCommand() const {
        return { left_.throttle(), right_.throttle() };
    }

private:
    Motor right_;
    Motor left_;
};
