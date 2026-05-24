// ============================================================
// locate.h  —  Servo pan controller + camera IPC (RPi5)
// ============================================================
//
// Sweeps a pan servo back and forth while waiting for camera.py
// to report a detected object.  When the object's horizontal
// offset falls within ±LOCK_THRESH of centre the servo stops
// and holds position.  If no signal arrives for LOCK_TIMEOUT_MS
// the servo resumes panning.
//
// WIRING
//   Servo signal  →  BCM GPIO 18  (physical pin 12)
//   Servo 5 V     →  physical pin  2 or 4  (5 V rail)
//   Servo GND     →  physical pin  6       (GND)
//
// IPC — named FIFO: /tmp/doggycart_locate
//   Created by locate.h on begin().
//   camera.py opens it for writing when an object is detected.
//   Protocol: one ASCII float per line, newline-terminated.
//     "-0.1500\n"   object is 15 % to the left  of frame centre
//      "0.0000\n"   object is centred            → servo locks
//     "+0.3200\n"   object is 32 % to the right  of frame centre
//   Range: -1.0 (far left) … +1.0 (far right)
// ============================================================
#pragma once
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "arduino_compat.h"
#include "gpio.h"
#include "Logger.h"

class Locate {
public:
    // ── Tuning constants ─────────────────────────────────────

    static constexpr uint8_t     SERVO_PIN        = 18;    // BCM GPIO 18, physical pin 12
    static constexpr int         PAN_MIN_US       = 1000;  // full-left pulse width
    static constexpr int         PAN_MAX_US       = 2000;  // full-right pulse width
    static constexpr int         PAN_CENTER_US    = 1500;  // centre pulse width
    static constexpr int         PAN_STEP_US      = 15;    // μs moved per step
    static constexpr int         PAN_STEP_MS      = 20;    // ms between steps (~50 steps/s)
    static constexpr float       LOCK_THRESH      = 0.08f; // |offset| < this → lock
    static constexpr unsigned long LOCK_TIMEOUT_MS = 500;  // ms without signal → resume pan

    static constexpr const char* PIPE_PATH = "/tmp/doggycart_locate";

    // ── State ────────────────────────────────────────────────

    enum class State { IDLE, PANNING, LOCKED };

    // ── Lifecycle ────────────────────────────────────────────

    void begin() {
        // Create the FIFO; EEXIST is fine (left over from a previous run)
        if (mkfifo(PIPE_PATH, 0666) < 0 && errno != EEXIST)
            logger.printf("[Locate] mkfifo: %s", strerror(errno));

        // Open O_RDWR so this process always holds a writer reference.
        // That prevents camera.py from blocking on open() and stops the
        // C++ read end from seeing spurious EOF when Python closes its side.
        fd_ = open(PIPE_PATH, O_RDWR | O_NONBLOCK);
        if (fd_ < 0)
            logger.printf("[Locate] pipe open: %s", strerror(errno));

        pos_us_ = PAN_CENTER_US;
        servoWrite(SERVO_PIN, pos_us_);
        logger.println("[Locate] Ready.");
    }

    void startPan() {
        state_   = State::PANNING;
        dir_     = 1;
        lastStep_ = millis();
        logger.println("[Locate] Panning started.");
    }

    void stopPan() {
        state_ = State::IDLE;
        servoWrite(SERVO_PIN, pos_us_);
        logger.println("[Locate] Panning stopped.");
    }

    // Call from the main loop
    void loop() {
        readPipe_();

        if (state_ == State::IDLE) return;

        if (state_ == State::LOCKED) {
            // Resume panning if camera goes silent
            if (millis() - lastSignal_ > LOCK_TIMEOUT_MS) {
                state_ = State::PANNING;
                logger.println("[Locate] Lock lost — resuming pan.");
            }
            return;
        }

        // PANNING: advance servo on schedule
        if (millis() - lastStep_ >= (unsigned long)PAN_STEP_MS) {
            lastStep_ = millis();
            advancePan_();
        }
    }

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        servoWrite(SERVO_PIN, 0);   // 0 = disable PWM signal; servo holds last position
    }

    State state()    const { return state_; }
    int   servoPos() const { return pos_us_; }

private:
    // ── Pipe read ────────────────────────────────────────────

    void readPipe_() {
        if (fd_ < 0) return;

        char buf[64];
        ssize_t n = read(fd_, buf, sizeof(buf) - 1);
        if (n <= 0) return;   // EAGAIN = no data; both are non-fatal

        buf[n] = '\0';
        float offset = strtof(buf, nullptr);
        lastSignal_  = millis();

        if (fabsf(offset) < LOCK_THRESH) {
            if (state_ == State::PANNING) {
                state_ = State::LOCKED;
                logger.printf("[Locate] Locked  offset=%.3f  pos=%d us", offset, pos_us_);
            }
        } else {
            // Steer toward object: bias pan direction
            dir_   = (offset > 0.0f) ? 1 : -1;
            state_ = State::PANNING;
        }
    }

    // ── Servo sweep ──────────────────────────────────────────

    void advancePan_() {
        pos_us_ += dir_ * PAN_STEP_US;
        if (pos_us_ >= PAN_MAX_US) { pos_us_ = PAN_MAX_US; dir_ = -1; }
        if (pos_us_ <= PAN_MIN_US) { pos_us_ = PAN_MIN_US; dir_ =  1; }
        servoWrite(SERVO_PIN, pos_us_);
    }

    // ── Members ──────────────────────────────────────────────

    int           fd_         = -1;
    int           pos_us_     = PAN_CENTER_US;
    int           dir_        = 1;
    State         state_      = State::IDLE;
    unsigned long lastStep_   = 0;
    unsigned long lastSignal_ = 0;
};
