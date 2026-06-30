// ============================================================
// main.cpp  —  DoggyCart entry point (Raspberry Pi 5)
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
#include <cstring>
#include <cerrno>
#include <atomic>
#include <unistd.h>
#include <sys/wait.h>
#include <INIReader.h>
#include "gpio.h"
#include "logger.h"
#include "controller.h"
#include "webServer.h"
#include "scan.h"
#include "Test.h"


// ── Globals defined here; extern'd in gpio.h / logger.h ─────

int    GPIO_HANDLE = -1;   // lgpio chip handle (see gpio.h)
Logger logger;             // single instance shared across headers

// ── WiFi AP credentials (used by hostapd config, not code) ──

static const char SSID[]     = "DoggyCart";
static const char PASSWORD[] = "doggy";

// ── Signal handling ──────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void sigHandler(int) { g_running = false; }

// ── tracking.py subprocess ───────────────────────────────────
//
// Launched via fork()+exec() (not std::system+"&") so the real PID is
// known here. That lets us SIGTERM/SIGKILL it on shutdown — relying on
// std::system's backgrounded shell left it orphaned, outliving doggyCart
// and holding the camera open even after the service stopped.

static pid_t launchTracking(const std::string& env, const std::string& python) {
    std::string cmd = env + " && exec " + python;

    pid_t pid = fork();
    if (pid < 0) {
        logger.printf("[main] fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);   // only reached if execl fails
    }
    return pid;
}

static void stopTracking(pid_t pid) {
    if (pid <= 0) return;

    logger.printf("[main] Stopping tracking.py (pid %d)", pid);
    kill(pid, SIGTERM);

    int status;
    for (int i = 0; i < 30; ++i) {   // wait up to ~3 s for clean exit + camera release
        if (waitpid(pid, &status, WNOHANG) == pid) return;
        usleep(100000);
    }

    logger.println("[main] tracking.py did not exit in time — sending SIGKILL");
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

// ── Entry point ──────────────────────────────────────────────

int main(int argc, char* argv[]) {

    bool testMode = (argc > 1 && strcmp(argv[1], "--test") == 0);

    logger.begin();

    if (testMode) {
        logger.println("DoggyCart test mode starting...");
        if (gpioBegin() < 0) {
            fprintf(stderr, "Failed to open GPIO chip %d — run with sudo?\n", GPIO_CHIP);
            return 1;
        }
        Controller car;
        car.begin();
        runTests(car);
        car.close();
        gpioEnd();
        return 0;
    }

    logger.println("DoggyCart starting...");

    INIReader reader("/etc/DoggyCart/config.ini");

    if (reader.ParseError() != 0) {
        logger.println("Error loading 'config.ini");
        return 1;
    }

    const int defaultNbrPhotos = 5;
    const int defaultMaxJpgsInDirectory = 15;

    // Read values with default fallbacks
    std::string env = reader.Get("Activate", "env","UNKNOWN");
    std::string python = reader.Get("Activate", "python", "UNKNOWN");
    std::string jpgsPathLit = reader.Get("Tracking", "jpgsPath", "UNKNOWN");
    int photosInRowIni = reader.GetInteger("Tracking", "photos", defaultNbrPhotos);
    int maxJPGS = reader.GetInteger("Tracking", "maxjpgs", defaultMaxJpgsInDirectory);

    if (jpgsPathLit == "UNKNOWN")
        logger.println("[main] WARNING: 'jpgsPath' missing from config.ini — photos will not be found");

    // Options not implemented at this time
    
    //int temp = reader.GetInteger("Temp", "temp", -1);
    //bool trackingFlag = reader.GetBoolean("Tracking", "tracking", false);

    // Launch python subsystem
    logger.println("launch: " + env + " && exec " + python);
    pid_t trackingPid = launchTracking(env, python);

    if (gpioBegin() < 0) {
        fprintf(stderr, "Failed to open GPIO chip %d — run with sudo?\n", GPIO_CHIP);
        return 1;
    }

    Controller car;
    car.begin();

    Locate locator;
    locator.setController(&car);
    locator.begin();

    WebServer server(car, locator, SSID, PASSWORD, jpgsPathLit, photosInRowIni, maxJPGS);
    server.begin();

    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    logger.println("Ready.");

    while (g_running) {
        car.loop();
        locator.loop();
        server.loop();
        delay(10);
    }

    logger.println("Shutting down.");
    server.stop();
    locator.close();
    car.close();             // stops motors before GPIO is closed
    gpioEnd();
    stopTracking(trackingPid);
    return 0;
}
