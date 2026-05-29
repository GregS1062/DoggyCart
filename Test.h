// ============================================================
// Test.h  —  Hardware and logic test sequences (RPi5)
// ============================================================
//
// Usage: in main.cpp, pass --test on the command line (or call
//        runTests(car) directly from main() instead of server.begin()).
//
//   int main() {
//       gpioBegin();
//       logger.begin();
//       car.begin();
//       runTests(car);   // runs once then halts
//   }
//
// Each test logs pass/fail to stdout.
// ============================================================

#pragma once
#include <cstdio>
#include "controller.h"

// ── Helper ──────────────────────────────────────────────────

static void testLog(const char* name, bool pass) {
    printf("  %s %s\n", pass ? "[PASS]" : "[FAIL]", name);
}

static void section(const char* title) {
    printf("\n=== %s ===\n", title);
}

// ── mix() unit tests (no hardware needed) ───────────────────

static void testMix() {
    section("Motor mix() logic");

    MotorCommand c = MotorPair::mix(0.05f, 0.5f);
    testLog("speed dead zone stops both motors",
            c.left == 0.0f && c.right == 0.0f);

    c = MotorPair::mix(-0.05f, 0.5f);
    testLog("negative speed dead zone stops both motors",
            c.left == 0.0f && c.right == 0.0f);

    c = MotorPair::mix(0.5f, 0.03f);
    testLog("steering dead zone gives equal motor speeds",
            fabs(c.left - c.right) < 0.001f);

    c = MotorPair::mix(1.0f, 0.0f);
    testLog("full speed straight: both motors at 1.0",
            fabs(c.left - 1.0f) < 0.001f && fabs(c.right - 1.0f) < 0.001f);

    c = MotorPair::mix(1.0f, 1.0f);
    testLog("max speed + full steer: steering effect is zero",
            fabs(c.left - c.right) < 0.001f);

    c = MotorPair::mix(0.10f, 1.0f);
    testLog("low speed + full right steer: right motor reverses",
            c.right < 0.0f);

    MotorCommand lo = MotorPair::mix(0.20f, 0.5f);
    MotorCommand hi = MotorPair::mix(0.80f, 0.5f);
    float diffLo = fabs(lo.left - lo.right);
    float diffHi = fabs(hi.left - hi.right);
    testLog("steering difference larger at low speed than high speed",
            diffLo > diffHi);

    c = MotorPair::mix(-0.5f, 0.0f);
    testLog("reverse: both motors negative",
            c.left < 0.0f && c.right < 0.0f);
}

// ── Controller state tests (no hardware movement) ────────────

static void testControllerState(Controller& car) {
    section("Controller state");

    car.restart();
    testLog("restart clears estop", !car.isEmergencyStopped());

    car.emergencyStop();
    testLog("emergencyStop sets estop", car.isEmergencyStopped());

    car.setDrive(0.5f, 0.5f);
    testLog("drive commands ignored while estopped",
            car.activeCommand().left == 0.0f &&
            car.activeCommand().right == 0.0f);

    car.restart();
    testLog("restart clears estop again", !car.isEmergencyStopped());
}

// ── Motor hardware sequence (moves the cart) ─────────────────

static void testMotorHardware(Controller& car) {
    section("Motor hardware (cart will move)");
    printf("  Ensure cart is on a stand or clear floor space.\n");
    delay(3000);

    car.restart();

    printf("  Forward 50%% for 1 s...\n");
    car.setDrive(0.0f, 0.5f);
    delay(1000);
    car.pause();
    testLog("forward run completed", true);
    delay(500);

    printf("  Reverse 50%% for 1 s...\n");
    car.restart();
    car.setDrive(0.0f, -0.5f);
    delay(1000);
    car.pause();
    testLog("reverse run completed", true);
    delay(500);

    printf("  Right turn (low speed) for 1 s...\n");
    car.restart();
    car.setDrive(0.8f, 0.15f);
    delay(1000);
    car.pause();
    testLog("right turn completed", true);
    delay(500);

    printf("  Left turn (low speed) for 1 s...\n");
    car.restart();
    car.setDrive(-0.8f, 0.15f);
    delay(1000);
    car.pause();
    testLog("left turn completed", true);
    delay(500);

    car.emergencyStop();
    testLog("emergency stop after hardware tests", car.isEmergencyStopped());
}

// ── Entry point ──────────────────────────────────────────────

inline void runTests(Controller& car) {
    printf("\n\nDoggyCart test suite starting...\n");

    testMix();
    testControllerState(car);
    testMotorHardware(car);

    printf("\nAll tests complete. Halting.\n");
    while (true) { delay(1000); }
}
