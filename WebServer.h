// ============================================================
// WebServer.h  —  WiFi HTTP server + API routing (ESP32)
// Uses ESPAsyncWebServer — request callbacks run on Core 0
// (same core as the WiFi stack), so loop() is a no-op.
// ============================================================
#pragma once
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "Logger.h"
#include "Controller.h"
#include "UI.h"

class WebServer {
public:
    WebServer(Controller& car, const char* ssid, const char* password)
        : car_(car), ssid_(ssid), password_(password), server_(8080) {}

    void begin() {
        logger.println(F("[WebServer] Starting WiFi AP"));
        WiFi.softAP(ssid_, password_);
        logger.printf("[WebServer] AP IP: %s", WiFi.softAPIP().toString().c_str());

        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
            req->send(200, "text/html", UI());
        });
        server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest* req) {
            req->send(200, "text/html", UI());
        });

        server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String json = "{\"ok\":true,\"emergencyStopped\":";
            json += car_.isEmergencyStopped() ? "true" : "false";
            json += ",\"lights\":\"";
            json += car_.lightsModeStr();
            json += "\"}";
            req->send(200, "application/json", json);
        });

        server_.on("/api/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", logger.getContent());
        });

        server_.on("/api/drive", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.setDrive(qfloat(req, "steer"), qfloat(req, "speed"));
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/speed", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.setSpeed(qfloat(req, "value"));
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/steer", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.setSteering(qfloat(req, "value"));
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/pause", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.pause();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/estop", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.emergencyStop();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/start", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.restart();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/resume", HTTP_GET, [this](AsyncWebServerRequest* req) {
            car_.restart();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/startlog", HTTP_GET, [this](AsyncWebServerRequest* req) {
            logger.enable();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/clearlog", HTTP_GET, [this](AsyncWebServerRequest* req) {
            logger.clear();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/api/lights", HTTP_GET, [this](AsyncWebServerRequest* req) {
            String mode = qstr(req, "mode");
            if      (mode == "left")  car_.setLightsMode(TurnSignals::Mode::LEFT);
            else if (mode == "right") car_.setLightsMode(TurnSignals::Mode::RIGHT);
            else if (mode == "full")  car_.setLightsMode(TurnSignals::Mode::FULL);
            else                      car_.setLightsMode(TurnSignals::Mode::OFF);
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "text/plain", "Not found");
        });

        server_.begin();
        logger.println(F("[WebServer] Listening on port 8080"));
    }

    void loop() {}  // AsyncWebServer dispatches requests internally on Core 0

private:
    float qfloat(AsyncWebServerRequest* req, const char* key) {
        if (!req->hasParam(key)) return 0.0f;
        return req->getParam(key)->value().toFloat();
    }

    String qstr(AsyncWebServerRequest* req, const char* key) {
        if (!req->hasParam(key)) return String();
        return req->getParam(key)->value();
    }

    Controller&    car_;
    const char*    ssid_;
    const char*    password_;
    AsyncWebServer server_;
};
