// ============================================================
// scan.h  —  Servo pan controller + camera IPC (RPi5)
// ============================================================
//
// tracking.py is launched by main.cpp (via config.ini, in the
// venv with the YOLO deps), not by this file. scan.h sweeps a
// pan servo while waiting for a detected person, then locks and
// steers the car to follow.
//
// WIRING
//   Servo signal  →  BCM GPIO 2  (physical pin 3)
//   Servo 5 V     →  physical pin  2 or 4
//   Servo GND     →  physical pin  9
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
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "gpio.h"
#include "logger.h"
#include "controller.h"

class Locate {
public:
    // ── Tuning constants ─────────────────────────────────────

    static constexpr uint8_t      SERVO_PIN        = 2;    // BCM GPIO 2, physical pin 3
    static constexpr int          PAN_MIN_US       = 1250;  // 45 ° — left scan limit
    static constexpr int          PAN_MAX_US       = 1667;  // 120 ° — right scan limit
    static constexpr int          PAN_CENTER_US    = 1458;  // midpoint (≈ 82.5 °)
    static constexpr int          PAN_STEP_US      = 27;    // μs per scan step (~4.9°, range is 417 μs / 75°)
    static constexpr unsigned long SERVO_MOVE_MS   = 1000;  // min ms between any servo move — pan or follow-nudge
    static constexpr float        LOCK_THRESH      = 0.08f; // |offset| < this → person centred
    static constexpr unsigned long LOCK_TIMEOUT_MS = 500;   // ms without signal → resume scan
    static constexpr unsigned long CONNECT_TIMEOUT_MS = 10000; // ms to wait for tracking.py "hello"

    // Following parameters — applied whenever a person is detected
    static constexpr float        FOLLOW_SPEED     = 0.40f; // forward throttle (0–1)
    static constexpr float        FOLLOW_STEER     = 0.75f; // scale offset → steering (0–1)
    static constexpr int          SERVO_FOLLOW_US  = 60;    // max servo nudge per detection (μs)
    static constexpr float        OFFSET_SMOOTH_A  = 0.3f;  // EMA weight on new sample (lower = smoother, laggier)

    static constexpr const char* PIPE_DATA       = "/tmp/doggycart_data";
    static constexpr const char* PIPE_CMD        = "/tmp/doggycart_cmd";

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
        servoWrite(SERVO_PIN, 0);   // tracking starts off — servo unpowered

        connectStart_ = millis();
        logger.println("[Locate] Ready — idle, servo unpowered. Waiting for tracking.py to connect...");
    }

    void startPan() {
        std::lock_guard<std::mutex> lk(mutex_);
        state_      = State::PANNING;
        following_  = false;
        dir_        = 1;
        lastServoMove_ = millis();
        haveSmoothed_ = false;
        sendCmd_("SEND");   // tell tracking.py to start sending detections
        logger.println("[Locate] Scanning started.");
    }

    void stopPan() {
        std::lock_guard<std::mutex> lk(mutex_);
        state_     = State::IDLE;
        following_ = false;
        sendCmd_("WAIT");   // tell tracking.py to stop sending detections
        if (controller_) controller_->pause();
        servoWrite(SERVO_PIN, 0);   // de-power servo when tracking is off
        logger.println("[Locate] Scanning stopped.");
    }

    // Call from the main loop
    void loop() {
        std::lock_guard<std::mutex> lk(mutex_);
        readPipe_();

        if (!connected_ && !connectFailed_ &&
            millis() - connectStart_ > CONNECT_TIMEOUT_MS) {
            connectFailed_ = true;
            logger.println("[Locate] tracking.py failed to connect — no \"hello\" received.");
        }

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
            if (millis() - lastServoMove_ >= SERVO_MOVE_MS) {
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
        std::lock_guard<std::mutex> lk(mutex_);
        if (fd_data_ >= 0) { ::close(fd_data_); fd_data_ = -1; }
        if (fd_cmd_  >= 0) { ::close(fd_cmd_);  fd_cmd_  = -1; }
        servoWrite(SERVO_PIN, 0);   // 0 = disable PWM; servo holds last position
    }

    State state()    const { std::lock_guard<std::mutex> lk(mutex_); return state_; }
    int   servoPos() const { std::lock_guard<std::mutex> lk(mutex_); return pos_us_; }

private:
    // ── FIFO + subprocess ────────────────────────────────────

    void makeFifo_(const char* path) {
        if (mkfifo(path, 0666) < 0 && errno != EEXIST)
            logger.printf("[Locate] mkfifo %s: %s", path, strerror(errno));
    }

    void sendCmd_(const char* cmd) {
        if (fd_cmd_ < 0) return;
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%s\n", cmd);
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (::write(fd_cmd_, buf, n) >= 0) return;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                logger.printf("[Locate] cmd write (%s): %s", cmd, strerror(errno));
                return;
            }
            usleep(1000);   // 1 ms back-off before retry
        }
        logger.printf("[Locate] cmd write (%s): pipe full after retries", cmd);
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
            if (!connected_) {
                connected_ = true;
                logger.println("[Locate] tracking.py connected.");
            }
            if (state_ == State::IDLE) {
                logger.println("[Locate] tracking.py ready — idle, sending WAIT.");
                sendCmd_("WAIT");
            } else {
                logger.println("[Locate] tracking.py ready — sending SEND.");
                sendCmd_("SEND");
                lastSignal_ = millis();
            }
            return;
        }

        // Ignore stray detections while idle — prevents a buffered/in-flight
        // message from re-activating PANNING/LOCKED after stopPan().
        if (state_ == State::IDLE) return;

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
            //   1. Nudge servo toward the person (proportional to a smoothed
            //      offset — raw per-frame YOLO box noise otherwise makes the
            //      servo jitter on every detection). Throttled to at most
            //      one move per SERVO_MOVE_MS, same as the pan sweep — only
            //      the steering motors react to every detection in real time.
            //   2. Steer the car in that direction while driving forward
            smoothedOffset_ = haveSmoothed_
                ? OFFSET_SMOOTH_A * offset + (1.0f - OFFSET_SMOOTH_A) * smoothedOffset_
                : offset;
            haveSmoothed_ = true;

            if (millis() - lastServoMove_ >= SERVO_MOVE_MS) {
                int nudge = (int)(smoothedOffset_ * (float)SERVO_FOLLOW_US);
                pos_us_   = constrain(pos_us_ + nudge, PAN_MIN_US, PAN_MAX_US);
                servoWrite(SERVO_PIN, pos_us_);
                lastServoMove_ = millis();
            }

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
        lastServoMove_ = millis();
    }

    // ── Members ──────────────────────────────────────────────

    mutable std::mutex mutex_;
    Controller*   controller_  = nullptr;
    int           fd_data_     = -1;
    int           fd_cmd_      = -1;
    int           pos_us_      = PAN_CENTER_US;
    int           dir_         = 1;
    bool          following_   = false;  // true when a person is actively detected
    float         smoothedOffset_ = 0.0f; // EMA of offset, used to damp servo nudges
    bool          haveSmoothed_   = false;
    State         state_       = State::IDLE;
    unsigned long lastServoMove_ = 0;
    unsigned long lastSignal_  = 0;
    unsigned long connectStart_  = 0;
    bool          connected_     = false;
    bool          connectFailed_ = false;
};
