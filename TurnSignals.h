// ============================================================
// TurnSignals.h  —  4-corner LED turn signal controller
// ============================================================
//
// WIRING (5-pin Dupont connector)
//   Pin 1 → GND
//   Pin 2 → GPIO 4   Front Left
//   Pin 3 → GPIO 5   Front Right
//   Pin 4 → GPIO 16  Rear Left
//   Pin 5 → GPIO 17  Rear Right
//
// Modes:
//   OFF   — all LEDs off
//   FULL  — all LEDs on solid
//   LEFT  — front-left + rear-left blink; opposite side solid
//   RIGHT — front-right + rear-right blink; opposite side solid
// ============================================================

#pragma once
#include <Arduino.h>

class TurnSignals {
public:
    enum class Mode : uint8_t { OFF, FULL, LEFT, RIGHT };

    void begin() {
        pinMode(PIN_FL, OUTPUT);
        pinMode(PIN_FR, OUTPUT);
        pinMode(PIN_RL, OUTPUT);
        pinMode(PIN_RR, OUTPUT);
        allOff();
    }

    void setMode(Mode m) {
        mode_        = m;
        lastToggle_  = millis();
        blinkOn_     = true;
        applyMode();
    }

    Mode mode() const { return mode_; }

    // Call from loop() — handles non-blocking blink timing
    void loop() {
        if (mode_ == Mode::OFF || mode_ == Mode::FULL) return;
        unsigned long now = millis();
        if (now - lastToggle_ >= BLINK_MS) {
            lastToggle_ = now;
            blinkOn_    = !blinkOn_;
            applyMode();
        }
    }

    const char* modeName() const {
        switch (mode_) {
            case Mode::OFF:   return "off";
            case Mode::FULL:  return "full";
            case Mode::LEFT:  return "left";
            case Mode::RIGHT: return "right";
            default:          return "off";
        }
    }

private:
    static constexpr uint8_t       PIN_FL   =  4;
    static constexpr uint8_t       PIN_FR   =  5;
    static constexpr uint8_t       PIN_RL   = 16;
    static constexpr uint8_t       PIN_RR   = 17;
    static constexpr unsigned long BLINK_MS = 500;

    Mode          mode_       = Mode::OFF;
    unsigned long lastToggle_ = 0;
    bool          blinkOn_    = false;

    void write(bool fl, bool fr, bool rl, bool rr) {
        digitalWrite(PIN_FL, fl ? HIGH : LOW);
        digitalWrite(PIN_FR, fr ? HIGH : LOW);
        digitalWrite(PIN_RL, rl ? HIGH : LOW);
        digitalWrite(PIN_RR, rr ? HIGH : LOW);
    }

    void allOff() { write(false, false, false, false); }

    void applyMode() {
        switch (mode_) {
            case Mode::OFF:   allOff();                                        break;
            case Mode::FULL:  write(true,     true,     true,     true);     break;
            case Mode::LEFT:  write(blinkOn_, true,     blinkOn_, true);     break;
            case Mode::RIGHT: write(true,     blinkOn_, true,     blinkOn_); break;
        }
    }
};
