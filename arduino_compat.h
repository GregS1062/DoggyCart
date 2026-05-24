// ============================================================
// arduino_compat.h  —  Arduino API shim for Raspberry Pi 5
//
// Provides the subset of Arduino.h used by this project:
//   millis(), delay(), constrain(), F() macro, String typedef,
//   Serial object, uint8_t / unsigned long types.
//
// Include this instead of <Arduino.h>.
// ============================================================
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

// ── Time ─────────────────────────────────────────────────────

inline unsigned long millis() {
    using namespace std::chrono;
    return static_cast<unsigned long>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ── Math ─────────────────────────────────────────────────────

template<typename T>
inline T constrain(T v, T lo, T hi) { return std::clamp(v, lo, hi); }

// ── String ───────────────────────────────────────────────────

// Arduino's String class → std::string (same interface for methods used here)
using String = std::string;

// F() flash-string macro is a no-op on Linux; string literals live in normal RAM
#define F(x) (x)

// ── Serial ───────────────────────────────────────────────────

struct SerialClass {
    void begin(int /*baud*/) {}

    void println(const char* s)         { printf("%s\n", s); }
    void println(const std::string& s)  { printf("%s\n", s.c_str()); }
    void println()                      { printf("\n"); }

    void print(const char* s)           { printf("%s", s); }
    void print(const std::string& s)    { printf("%s", s.c_str()); }

    template<typename T>
    void println(T val)  { std::cout << val << '\n'; }
    template<typename T>
    void print(T val)    { std::cout << val; }
};

// Defined inline (C++17) — no separate .cpp needed
inline SerialClass Serial;
