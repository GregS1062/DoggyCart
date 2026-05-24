// ============================================================
// main.cpp  —  DoggyCart entry point (Raspberry Pi 5)
//
// Replaces the Arduino setup()/loop() pattern with int main().
//
// Run:   sudo ./doggycart
// (sudo required for lgpio GPIO access)
//
// WiFi AP setup (one-time OS configuration):
//   sudo apt install hostapd dnsmasq
//   Set ssid=DoggyCart, wpa_passphrase=rctank1 in hostapd.conf
//   Connect to http://192.168.4.1:8080
// ============================================================
#include <csignal>
#include <cstdio>
#include <atomic>
#include "gpio.h"
#include "Logger.h"
#include "Controller.h"
#include "WebServer.h"
#include "locate.h"
#include "arduino_compat.h"

// ── Globals defined here; extern'd in gpio.h / Logger.h ─────

int    GPIO_HANDLE = -1;   // lgpio chip handle (see gpio.h)
Logger logger;             // single instance shared across headers

// ── WiFi AP credentials (used by hostapd config, not code) ──

static const char SSID[]     = "DoggyCart";
static const char PASSWORD[] = "rctank1";

// ── Signal handling ──────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void sigHandler(int) { g_running = false; }

// ── Entry point ──────────────────────────────────────────────

int main() {
    if (gpioBegin() < 0) {
        fprintf(stderr, "Failed to open GPIO chip %d — run with sudo?\n", GPIO_CHIP);
        return 1;
    }

    logger.begin();
    logger.println("DoggyCart starting...");

    Controller car;
    car.begin();

    Locate locator;
    locator.begin();
    locator.startPan();

    WebServer server(car, SSID, PASSWORD);
    server.begin();

    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    logger.println("Ready.");

    while (g_running) {
        car.loop();
        locator.loop();
        delay(10);
    }

    logger.println("Shutting down.");
    server.stop();
    locator.close();
    car.close();             // stops motors before GPIO is closed
    gpioEnd();
    return 0;
}
