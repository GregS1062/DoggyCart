// ============================================================
// WebServer.h  —  HTTP server + API routing (RPi5)
//
// Replaces ESPAsyncWebServer + WiFi.softAP with cpp-httplib.
//
// Dependency: place httplib.h in this directory.
//   Download the single-header release from the cpp-httplib project
//   (search: "cpp-httplib yhirose") or install via:
//   sudo apt install libcpp-httplib-dev   (if available in your distro)
//
// WiFi soft-AP is configured at the OS level, not in application code.
// On RPi5 with Raspberry Pi OS:
//   sudo apt install hostapd dnsmasq
//   Configure /etc/hostapd/hostapd.conf with ssid=DoggyCart, wpa_passphrase=rctank1
//   Configure /etc/dnsmasq.conf for DHCP on wlan0
// This server then listens on all interfaces at port 8080;
// connect via http://192.168.4.1:8080 (or whatever IP hostapd assigns).
// ============================================================
#pragma once
#include <string>
#include <thread>
#include "httplib.h"
#include "arduino_compat.h"
#include "Logger.h"
#include "Controller.h"
#include "UI.h"

class WebServer {
public:
    // ssid / password kept in the signature so main.cpp is unchanged;
    // they are used only by the OS-level hostapd config, not here.
    WebServer(Controller& car, const char* /*ssid*/, const char* /*password*/)
        : car_(car) {}

    void begin() {
        logger.println("[WebServer] Starting HTTP server");

        server_.Get("/", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(UI(), "text/html");
        });
        server_.Get("/index.html", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(UI(), "text/html");
        });

        server_.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
            std::string json = "{\"ok\":true,\"emergencyStopped\":";
            json += car_.isEmergencyStopped() ? "true" : "false";
            json += "}";
            res.set_content(json, "application/json");
        });

        server_.Get("/api/log", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(logger.getContent(), "text/plain");
        });

        server_.Get("/api/drive", [this](const httplib::Request& req, httplib::Response& res) {
            car_.setDrive(qfloat(req, "steer"), qfloat(req, "speed"));
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/speed", [this](const httplib::Request& req, httplib::Response& res) {
            car_.setSpeed(qfloat(req, "value"));
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/steer", [this](const httplib::Request& req, httplib::Response& res) {
            car_.setSteering(qfloat(req, "value"));
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/pause", [this](const httplib::Request&, httplib::Response& res) {
            car_.pause();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/estop", [this](const httplib::Request&, httplib::Response& res) {
            car_.emergencyStop();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/start", [this](const httplib::Request&, httplib::Response& res) {
            car_.restart();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/resume", [this](const httplib::Request&, httplib::Response& res) {
            car_.restart();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/startlog", [this](const httplib::Request&, httplib::Response& res) {
            logger.enable();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/clearlog", [this](const httplib::Request&, httplib::Response& res) {
            logger.clear();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_content("Not found", "text/plain");
        });

        // Run server in background thread; main thread handles signals.
        server_thread_ = std::thread([this] { server_.listen("0.0.0.0", 8080); });
        server_thread_.detach();

        logger.println("[WebServer] Listening on port 8080");
    }

    void loop() {}  // cpp-httplib handles requests in its own thread pool

    void stop() { server_.stop(); }

private:
    static float qfloat(const httplib::Request& req, const char* key) {
        if (!req.has_param(key)) return 0.0f;
        try { return std::stof(req.get_param_value(key)); }
        catch (...) { return 0.0f; }
    }

    Controller&     car_;
    httplib::Server server_;
    std::thread     server_thread_;
};
