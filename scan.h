// ============================================================
// scan.h  —  Servo pan controller + camera IPC (RPi5)
// ============================================================
//
// Launches tracking.py as a subprocess, sweeps a pan servo while
// waiting for a detected person, then locks and steers the car to follow.
//
// WIRING
//   Servo signal  →  BCM GPIO 18  (physical pin 12)
//   Servo 5 V     →  physical pin  2 or 4
//   Servo GND     →  physical pin  6
//
// STATES
//   IDLE    : nothing running
//   PANNING : no person in view — servo sweeps 45–120 ° slowly,
//             motors are stopped
//   LOCKED  : person centred in frame — car drives forward,
//             servo holds position
//
//   While PANNING, if a detection arrives off-centre the servo is
//   nudged toward the person and the motors begin steering.  The car
//   reaches LOCKED when the person is within LOCK_THRESH of centre.
//   When no detection arrives for LOCK_TIMEOUT_MS the car stops and
//   PANNING resumes.
//
// IPC — two named FIFOs, both created by begin():
//
//   PIPE_DATA  /tmp/doggycart_data   tracking.py → scan.h
//     First message : "hello"  — tracking.py finished initialising
//     Thereafter    : one normalised float per line
//                     "-0.3200\n"  object 32 % left of frame centre
//                      "0.0000\n"  centred
//                     "+0.1500\n"  15 % right
//                     Range: -1.0 (far left) … +1.0 (far right)
//
//   PIPE_CMD   /tmp/doggycart_cmd    scan.h → tracking.py
//     "SEND\n"  request next detection (sent on hello + on lock loss)
//     "WAIT\n"  object centred — pause sending
//     "ack\n"   object off-centre — send next detection
//
// ============================================================
#pragma once
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "arduino_compat.h"
#include "gpio.h"
#include "logger.h"
#include "controller.h"

class Locate {
public:
    // ── Tuning constants ─────────────────────────────────────

    static constexpr uint8_t      SERVO_PIN        = 18;    // BCM GPIO 18, physical pin 12
    static constexpr int          PAN_MIN_US       = 1250;  // 45 ° — left scan limit
    static constexpr int          PAN_MAX_US       = 1667;  // 120 ° — right scan limit
    static constexpr int          PAN_CENTER_US    = 1458;  // midpoint (≈ 82.5 °)
    static constexpr int          PAN_STEP_US      = 15;    // μs per scan step
    static constexpr int          PAN_STEP_MS      = 20;    // ms between scan steps (~50 steps/s)
    static constexpr float        LOCK_THRESH      = 0.08f; // |offset| < this → person centred
    static constexpr unsigned long LOCK_TIMEOUT_MS = 500;   // ms without signal → resume scan

    // Following parameters — applied whenever a person is detected
    static constexpr float        FOLLOW_SPEED     = 0.40f; // forward throttle (0–1)
    static constexpr float        FOLLOW_STEER     = 0.75f; // scale offset → steering (0–1)
    static constexpr int          SERVO_FOLLOW_US  = 60;    // max servo nudge per detection (μs)

    static constexpr const char* PIPE_DATA       = "/tmp/doggycart_data";
    static constexpr const char* PIPE_CMD        = "/tmp/doggycart_cmd";
    static constexpr const char* TRACKING_SCRIPT = "/home/greg/Projects/DoggyCart/tracking.py";

    // ── State ────────────────────────────────────────────────

    enum class State { IDLE, PANNING, LOCKED };

    // ── Lifecycle ────────────────────────────────────────────

    void setController(Controller* c) { controller_ = c; }

    void begin() {
        makeFifo_(PIPE_DATA);
        makeFifo_(PIPE_CMD);

        // Open O_RDWR so this process always holds both reader and writer
        // references.  This prevents tracking.py from blocking on open() and
        // stops the read end from seeing spurious EOF when Python reconnects.
        fd_data_ = open(PIPE_DATA, O_RDWR | O_NONBLOCK);
        if (fd_data_ < 0)
            logger.printf("[Locate] data pipe open: %s", strerror(errno));

        fd_cmd_ = open(PIPE_CMD, O_RDWR | O_NONBLOCK);
        if (fd_cmd_ < 0)
            logger.printf("[Locate] cmd pipe open: %s", strerror(errno));

        pos_us_ = PAN_CENTER_US;
        servoWrite(SERVO_PIN, pos_us_);

        launchTracker_();

        logger.println("[Locate] Ready — scanning until person detected.");
    }

    void startPan() {
        state_      = State::PANNING;
        following_  = false;
        dir_        = 1;
        lastStep_   = millis();
        logger.println("[Locate] Scanning started.");
    }

    void stopPan() {
        state_     = State::IDLE;
        following_ = false;
        if (controller_) controller_->pause();
        servoWrite(SERVO_PIN, pos_us_);
        logger.println("[Locate] Scanning stopped.");
    }

    // Call from the main loop
    void loop() {
        readPipe_();

        if (state_ == State::IDLE) return;

        if (state_ == State::LOCKED) {
            if (millis() - lastSignal_ > LOCK_TIMEOUT_MS) {
                // Person lost — stop car, resume scan
                following_ = false;
                if (controller_) controller_->pause();
                state_ = State::PANNING;
                sendCmd_("SEND");
                logger.println("[Locate] Lock lost — resuming scan.");
            }
            return;
        }

        // PANNING
        if (!following_) {
            // No person detected: sweep servo slowly back and forth
            if (millis() - lastStep_ >= (unsigned long)PAN_STEP_MS) {
                lastStep_ = millis();
                advancePan_();
            }
        } else {
            // Person detected but off-centre: servo + motors driven by readPipe_
            // Stop following if signal disappears
            if (millis() - lastSignal_ > LOCK_TIMEOUT_MS) {
                following_ = false;
                if (controller_) controller_->pause();
                logger.println("[Locate] Target lost — scanning.");
            }
        }
    }

    void close() {
        if (fd_data_ >= 0) { ::close(fd_data_); fd_data_ = -1; }
        if (fd_cmd_  >= 0) { ::close(fd_cmd_);  fd_cmd_  = -1; }
        servoWrite(SERVO_PIN, 0);   // 0 = disable PWM; servo holds last position
        if (tracker_pid_ > 0) {
            kill(tracker_pid_, SIGTERM);
            waitpid(tracker_pid_, nullptr, 0);
            tracker_pid_ = -1;
        }
    }

    State state()    const { return state_; }
    int   servoPos() const { return pos_us_; }

private:
    // ── FIFO + subprocess ────────────────────────────────────

    void makeFifo_(const char* path) {
        if (mkfifo(path, 0666) < 0 && errno != EEXIST)
            logger.printf("[Locate] mkfifo %s: %s", path, strerror(errno));
    }

    void launchTracker_() {
        tracker_pid_ = fork();
        if (tracker_pid_ == 0) {
            execl("/usr/bin/python3", "python3", TRACKING_SCRIPT, nullptr);
            _exit(1);   // execl only returns on error
        } else if (tracker_pid_ > 0) {
            logger.printf("[Locate] tracking.py launched (pid %d)", (int)tracker_pid_);
        } else {
            logger.printf("[Locate] fork failed: %s", strerror(errno));
            tracker_pid_ = -1;
        }
    }

    void sendCmd_(const char* cmd) {
        if (fd_cmd_ < 0) return;
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%s\n", cmd);
        if (::write(fd_cmd_, buf, n) < 0 && errno != EAGAIN)
            logger.printf("[Locate] cmd write (%s): %s", cmd, strerror(errno));
    }

    // ── Pipe read ────────────────────────────────────────────

    void readPipe_() {
        if (fd_data_ < 0) return;

        char buf[64];
        ssize_t n = read(fd_data_, buf, sizeof(buf) - 1);
        if (n <= 0) return;   // EAGAIN = no data; non-fatal

        buf[n] = '\0';

        // Handshake: tracking.py signals it finished initialising
        if (strncmp(buf, "hello", 5) == 0) {
            logger.println("[Locate] tracking.py ready — sending SEND.");
            sendCmd_("SEND");
            lastSignal_ = millis();
            return;
        }

        float offset = strtof(buf, nullptr);
        lastSignal_  = millis();

        if (fabsf(offset) < LOCK_THRESH) {
            // Person is centred — lock on and drive straight
            if (state_ == State::PANNING) {
                state_     = State::LOCKED;
                following_ = true;
                logger.printf("[Locate] Locked  offset=%.3f  pos=%d us", offset, pos_us_);
            }
            sendCmd_("WAIT");
            if (controller_)
                controller_->setDrive(0.0f, FOLLOW_SPEED);
        } else {
            // Person detected but off-centre:
            //   1. Nudge servo toward the person (proportional to offset)
            //   2. Steer the car in that direction while driving forward
            int nudge = (int)(offset * (float)SERVO_FOLLOW_US);
            pos_us_   = constrain(pos_us_ + nudge, PAN_MIN_US, PAN_MAX_US);
            servoWrite(SERVO_PIN, pos_us_);

            dir_       = (offset > 0.0f) ? 1 : -1;
            following_ = true;
            state_     = State::PANNING;
            sendCmd_("ack");

            if (controller_)
                controller_->setDrive(offset * FOLLOW_STEER, FOLLOW_SPEED);
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

    Controller*   controller_  = nullptr;
    int           fd_data_     = -1;
    int           fd_cmd_      = -1;
    pid_t         tracker_pid_ = -1;
    int           pos_us_      = PAN_CENTER_US;
    int           dir_         = 1;
    bool          following_   = false;  // true when a person is actively detected
    State         state_       = State::IDLE;
    unsigned long lastStep_    = 0;
    unsigned long lastSignal_  = 0;
};
