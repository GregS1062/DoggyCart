// ============================================================
// Test.h  —  Hardware and logic test sequences
// ============================================================
//
// Usage: in DoggyCart.ino, define TEST_MODE before including headers,
//        then call runTests(car) from setup() instead of server.begin().
//
//   #define TEST_MODE
//   ...
//   void setup() {
//       car.begin();
//       runTests(car);   // runs once then halts
//   }
//
// Each test logs pass/fail to Serial at 115200 baud.
// ============================================================

#pragma once
#include "Controller.h"

// ── Helper ──────────────────────────────────────────────────

static void testLog(const char* name, bool pass) {
    Serial.print(F("  "));
    Serial.print(pass ? F("[PASS] ") : F("[FAIL] "));
    Serial.println(name);
}

static void section(const char* title) {
    Serial.println();
    Serial.print(F("=== "));
    Serial.print(title);
    Serial.println(F(" ==="));
}

// ── mix() unit tests (no hardware needed) ───────────────────

static void testMix() {
    section("Motor mix() logic");

    // Speed dead zone: |speed| < 0.10 → stopped
    MotorCommand c = MotorPair::mix(0.05f, 0.5f);
    testLog("speed dead zone stops both motors",
            c.left == 0.0f && c.right == 0.0f);

    c = MotorPair::mix(-0.05f, 0.5f);
    testLog("negative speed dead zone stops both motors",
            c.left == 0.0f && c.right == 0.0f);

    // Steering dead zone: |steer| < 0.05 → equal motor speeds
    c = MotorPair::mix(0.5f, 0.03f);
    testLog("steering dead zone gives equal motor speeds",
            fabs(c.left - c.right) < 0.001f);

    // Straight ahead at full speed
    c = MotorPair::mix(1.0f, 0.0f);
    testLog("full speed straight: both motors at 1.0",
            fabs(c.left - 1.0f) < 0.001f && fabs(c.right - 1.0f) < 0.001f);

    // At max speed, steering effect zeroes out (1 - |speed| = 0)
    c = MotorPair::mix(1.0f, 1.0f);
    testLog("max speed + full steer: steering effect is zero",
            fabs(c.left - c.right) < 0.001f);

    // Low speed + hard steer: inner motor reverses
    c = MotorPair::mix(0.10f, 1.0f);
    testLog("low speed + full right steer: right motor reverses",
            c.right < 0.0f);

    // Steering scales inversely with speed
    MotorCommand lo = MotorPair::mix(0.20f, 0.5f);
    MotorCommand hi = MotorPair::mix(0.80f, 0.5f);
    float diffLo = fabs(lo.left - lo.right);
    float diffHi = fabs(hi.left - hi.right);
    testLog("steering difference larger at low speed than high speed",
            diffLo > diffHi);

    // Reverse: both motors negative when speed < 0
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

// ── Motor hardware sequence (moves the tank) ─────────────────

static void testMotorHardware(Controller& car) {
    section("Motor hardware (tank will move)");
    Serial.println(F("  Ensure tank is on a stand or clear floor space."));
    delay(3000);

    car.restart();

    Serial.println(F("  Forward 50% for 1 s..."));
    car.setDrive(0.0f, 0.5f);
    delay(1000);
    car.pause();
    testLog("forward run completed", true);
    delay(500);

    Serial.println(F("  Reverse 50% for 1 s..."));
    car.restart();
    car.setDrive(0.0f, -0.5f);
    delay(1000);
    car.pause();
    testLog("reverse run completed", true);
    delay(500);

    Serial.println(F("  Right turn (low speed) for 1 s..."));
    car.restart();
    car.setDrive(0.8f, 0.15f);
    delay(1000);
    car.pause();
    testLog("right turn completed", true);
    delay(500);

    Serial.println(F("  Left turn (low speed) for 1 s..."));
    car.restart();
    car.setDrive(-0.8f, 0.15f);
    delay(1000);
    car.pause();
    testLog("left turn completed", true);
    delay(500);

    car.emergencyStop();
    testLog("emergency stop after hardware tests", car.isEmergencyStopped());
}

// ── LED hardware sequence ────────────────────────────────────

static void testLeds(Controller& car) {
    section("LED hardware");

    Serial.println(F("  All LEDs on for 1 s..."));
    car.setLightsMode(TurnSignals::Mode::FULL);
    delay(1000);

    Serial.println(F("  Left blink for 2 s..."));
    car.setLightsMode(TurnSignals::Mode::LEFT);
    unsigned long t = millis();
    while (millis() - t < 2000) { car.loop(); delay(10); }

    Serial.println(F("  Right blink for 2 s..."));
    car.setLightsMode(TurnSignals::Mode::RIGHT);
    t = millis();
    while (millis() - t < 2000) { car.loop(); delay(10); }

    car.setLightsMode(TurnSignals::Mode::OFF);
    testLog("LED sequence completed", true);
}

// ── Entry point ──────────────────────────────────────────────

inline void runTests(Controller& car) {
    Serial.println(F("\n\nDoggyCart test suite starting..."));

    testMix();
    testControllerState(car);
    testLeds(car);
    testMotorHardware(car);

    Serial.println(F("\nAll tests complete. Halting."));
    while (true) { delay(1000); }
}
