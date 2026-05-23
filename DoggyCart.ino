// ============================================================
// DoggyCart.ino  —  ESP32 WiFi RC Tank
//
// Combines:
//   - Motor + WebServer control from RCESP32
//   - 4-corner LED turn signals from TurnSignals_Test
//
// Board: Generic ESP32 (esp32:esp32:esp32), /dev/ttyUSB0
// Connect: http://192.168.4.1:8080  (SSID below)
// ============================================================
#include "Logger.h"
#include "Controller.h"
#include "WebServer.h"

const char SSID[]     = "DoggyCart";
const char PASSWORD[] = "rctank1";

Logger     logger;
Controller car;
WebServer  server(car, SSID, PASSWORD);

void setup() {
    Serial.begin(115200);
    delay(1000);
    logger.begin();
    logger.println(F("RC Tank starting..."));

    car.begin();
    server.begin();

    logger.println(F("Ready."));
}

void loop() {
    server.loop();
    car.loop();     // drives turn-signal blink timing
}
