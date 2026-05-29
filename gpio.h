// ============================================================
// gpio.h  —  lgpio + hardware PWM wrapper for Raspberry Pi 5
//
// Digital I/O:   lgpio  (sudo apt install liblgpio-dev, link: -llgpio)
// Hardware PWM:  Linux sysfs  (/sys/class/pwm/)
//
// Hardware PWM setup (one-time):
//   Add to /boot/firmware/config.txt:  dtoverlay=pwm-2chan
//   Reboot, then verify:               ls /sys/class/pwm/
//   Expected:                          pwmchip2  (RPi5 default)
//   If your kernel uses a different chip number, override with
//   -DHW_PWM_CHIP='"/sys/class/pwm/pwmchipN"' in the Makefile.
//
// WIRING SUMMARY — see motor.h and scan.h for full details.
//   Right motor  individual wires  physical 13, 15, 32  GPIO 27, 22, 12
//   Left  motor  3-pin Dupont      physical 29, 31, 33  GPIO  5,  6, 13
//   Pan servo    signal wire       physical 12           GPIO 18
//
// RPi5 uses /dev/gpiochip4 for the 40-pin header.
// RPi3/4 use /dev/gpiochip0 — override with -DGPIO_CHIP=0 if needed.
// ============================================================
#pragma once
#include <lgpio.h>
#include <cstdint>
#include <cstdio>

// ── lgpio handle ─────────────────────────────────────────────

#ifndef GPIO_CHIP
#define GPIO_CHIP 4
#endif

extern int GPIO_HANDLE;   // defined in main.cpp

inline int gpioBegin() {
    GPIO_HANDLE = lgGpiochipOpen(GPIO_CHIP);
    if (GPIO_HANDLE < 0)
        fprintf(stderr, "[gpio] lgGpiochipOpen(%d) failed: %d\n", GPIO_CHIP, GPIO_HANDLE);
    return GPIO_HANDLE;
}

inline void gpioEnd() {
    if (GPIO_HANDLE >= 0) {
        lgGpiochipClose(GPIO_HANDLE);
        GPIO_HANDLE = -1;
    }
}

// ── Arduino-compatible constants ─────────────────────────────

static const int OUTPUT = 1;
static const int HIGH   = 1;
static const int LOW    = 0;

// ── Digital I/O ──────────────────────────────────────────────

inline void pinMode(uint8_t pin, int /*mode*/) {
    lgGpioClaimOutput(GPIO_HANDLE, 0, pin, 0);
}

inline void digitalWrite(uint8_t pin, int val) {
    lgGpioWrite(GPIO_HANDLE, pin, val ? 1 : 0);
}

// ── Hardware PWM (sysfs, RPi5) ───────────────────────────────
//
// GPIO 12 → PWM channel 0    GPIO 13 → PWM channel 1
// These pins are driven entirely by the RP1 PWM hardware;
// lgpio is NOT used for them (do not call lgGpioClaimOutput).
// All other pins use lgpio software PWM (lgTxPwm).

#ifndef HW_PWM_CHIP
#define HW_PWM_CHIP "/sys/class/pwm/pwmchip2"
#endif

static inline bool hwPwmPin_(uint8_t pin) { return pin == 12 || pin == 13; }
static inline int  hwPwmCh_ (uint8_t pin) { return pin == 13 ? 1 : 0; }

static void pwmSysWrite_(const char* path, const char* val) {
    FILE* f = fopen(path, "w");
    if (f) { fputs(val, f); fclose(f); }
}

// ── ledcAttach / ledcWrite ────────────────────────────────────
//
// Mirror the ESP32 Arduino 3.x LEDC API. Frequency is fixed at
// 1 kHz. duty8 is 0–255 (8-bit resolution).
//
// Hardware PWM pins (12, 13): configured via sysfs.
// All other pins:             configured via lgTxPwm.

inline void ledcAttach(uint8_t pin, float /*freqHz*/, int /*bits*/) {
    if (hwPwmPin_(pin)) {
        char path[128], val[32];
        int ch = hwPwmCh_(pin);
        // Export channel — ignore error if already exported from a previous run
        snprintf(val, sizeof(val), "%d", ch);
        pwmSysWrite_(HW_PWM_CHIP "/export", val);
        // Period: 1 kHz = 1,000,000 ns  (must be written before duty_cycle)
        snprintf(path, sizeof(path), HW_PWM_CHIP "/pwm%d/period", ch);
        pwmSysWrite_(path, "1000000");
        // Start at 0 % duty
        snprintf(path, sizeof(path), HW_PWM_CHIP "/pwm%d/duty_cycle", ch);
        pwmSysWrite_(path, "0");
        // Enable the channel
        snprintf(path, sizeof(path), HW_PWM_CHIP "/pwm%d/enable", ch);
        pwmSysWrite_(path, "1");
    } else {
        lgTxPwm(GPIO_HANDLE, pin, 1000.0f, 0.0f, 0, 0);
    }
}

// ── Servo control ────────────────────────────────────────────
//
// Standard RC servo: 50 Hz, 1000–2000 μs pulse width.
//   1000 μs = full left   1500 μs = centre   2000 μs = full right
//   0 μs    = disable signal (servo holds last position)

inline void servoWrite(uint8_t pin, int width_us) {
    lgTxServo(GPIO_HANDLE, pin, width_us, 50, 0, 0);
}

inline void ledcWrite(uint8_t pin, int duty8) {
    if (hwPwmPin_(pin)) {
        // duty_cycle in ns; constrained to [0, period] = [0, 1,000,000]
        long ns = (long)((duty8 / 255.0f) * 1000000.0f);
        char path[128], val[32];
        snprintf(path, sizeof(path), HW_PWM_CHIP "/pwm%d/duty_cycle", hwPwmCh_(pin));
        snprintf(val, sizeof(val), "%ld", ns);
        pwmSysWrite_(path, val);
    } else {
        float pct = (duty8 / 255.0f) * 100.0f;
        lgTxPwm(GPIO_HANDLE, pin, 1000.0f, pct, 0, 0);
    }
}
