// ============================================================
// webServer.h  —  HTTP server + API routing (RPi5)
//
// WiFi soft-AP is configured at the OS level, not in application code.
// On RPi5 with Raspberry Pi OS:
//   sudo apt install hostapd dnsmasq
//   Configure /etc/hostapd/hostapd.conf with ssid=DoggyCart, wpa_passphrase=rctank1
//   Configure /etc/dnsmasq.conf for DHCP on wlan0
// This server then listens on all interfaces at port 8080;
// connect via http://192.168.4.1:8080
// ============================================================
#pragma once
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include "httplib.h"
#include "gpio.h"
#include "logger.h"
#include "controller.h"
#include "scan.h"
#include "ui.h"

class WebServer {
public:
    static constexpr unsigned long CLEANUP_INTERVAL_MS = 2 * 60 * 1000; // 2 minutes

    // ssid / password are used only by the OS-level hostapd config, not here.
    // jpgDir     — must match [Tracking] jpgsPath in config.ini, the same value
    //              tracking.py writes photos to; otherwise this serves an
    //              empty/wrong directory while photos pile up elsewhere
    // maxPhotos  — number of newest photos shown in the UI gallery
    // maxJpgs    — cap on jpg files kept on disk; oldest are deleted past this
    WebServer(Controller& car, Locate& locator,
              const char* /*ssid*/, const char* /*password*/,
              std::string jpgDir, int maxPhotos, int maxJpgs)
        : car_(car), locator_(locator), jpgDir_(std::move(jpgDir)),
          maxPhotos_(maxPhotos), maxJpgs_(maxJpgs) {}

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
            json += ",\"loggingEnabled\":";
            json += logger.isEnabled() ? "true" : "false";
            json += "}";
            res.set_content(json, "application/json");
        });

        server_.Get("/api/log", [](const httplib::Request&, httplib::Response& res) {
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
            locator_.stopPan();   // server-side backstop, independent of client JS state
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

        server_.Get("/api/track", [this](const httplib::Request& req, httplib::Response& res) {
            bool on = req.has_param("on") && req.get_param_value("on") == "1";
            if (on) locator_.startPan();
            else    locator_.stopPan();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/photos", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(listPhotos_(), "application/json");
        });

        // Serve individual jpg files from jpgDir_.
        // Pattern matches /jpg/<filename> with no subdirectories.
        server_.Get(R"(/jpg/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string fname = req.matches[1].str();
            if (fname.find("..") != std::string::npos) {
                res.status = 400; return;
            }
            std::string path = jpgDir_ + fname;
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) { res.status = 404; res.set_content("not found", "text/plain"); return; }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::string content(sz > 0 ? static_cast<size_t>(sz) : 0, '\0');
            if (sz > 0) fread(&content[0], 1, static_cast<size_t>(sz), f);
            fclose(f);
            res.set_content(content, "image/jpeg");
        });

        server_.Get("/api/startlog", [](const httplib::Request&, httplib::Response& res) {
            logger.enable();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.Get("/api/clearlog", [](const httplib::Request&, httplib::Response& res) {
            logger.clear();
            res.set_content("{\"ok\":true}", "application/json");
        });

        server_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_content("Not found", "text/plain");
        });

        server_thread_ = std::thread([this] { server_.listen("0.0.0.0", 8080); });
        server_thread_.detach();

        logger.println("[WebServer] Listening on port 8080");
    }

    void loop() {
        unsigned long now = millis();
        if (now - lastCleanup_ < CLEANUP_INTERVAL_MS) return;
        lastCleanup_ = now;
        cleanupOldJpgs_();
    }
    void stop() { server_.stop(); }

private:
    static float qfloat(const httplib::Request& req, const char* key) {
        if (!req.has_param(key)) return 0.0f;
        try { return std::stof(req.get_param_value(key)); }
        catch (...) { return 0.0f; }
    }

    struct JpgEntry { std::string name; time_t mtime; };

    std::vector<JpgEntry> listJpgs_() {
        std::vector<JpgEntry> entries;
        DIR* dir = opendir(jpgDir_.c_str());
        if (!dir) return entries;

        struct dirent* de;
        while ((de = readdir(dir)) != nullptr) {
            std::string name = de->d_name;
            if (name.size() < 5 || name.substr(name.size() - 4) != ".jpg") continue;
            std::string path = jpgDir_ + name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0)
                entries.push_back({name, st.st_mtime});
        }
        closedir(dir);
        return entries;
    }

    std::string listPhotos_() {
        std::string json = "{\"photos\":[";
        std::vector<JpgEntry> entries = listJpgs_();

        std::sort(entries.begin(), entries.end(),
            [](const JpgEntry& a, const JpgEntry& b){ return a.mtime > b.mtime; });
        if (entries.size() > (size_t)maxPhotos_)
            entries.resize(maxPhotos_);

        bool first = true;
        for (const auto& e : entries) {
            if (!first) json += ",";
            json += "\"/jpg/" + e.name + "\"";
            first = false;
        }
        json += "]}";
        return json;
    }

    // Deletes the oldest jpgs in JPG_DIR beyond maxJpgs_, called every
    // CLEANUP_INTERVAL_MS from loop().
    void cleanupOldJpgs_() {
        std::vector<JpgEntry> entries = listJpgs_();
        if (entries.size() <= (size_t)maxJpgs_) return;

        std::sort(entries.begin(), entries.end(),
            [](const JpgEntry& a, const JpgEntry& b){ return a.mtime < b.mtime; });

        size_t toDelete = entries.size() - (size_t)maxJpgs_;
        for (size_t i = 0; i < toDelete; ++i) {
            std::string path = jpgDir_ + entries[i].name;
            if (remove(path.c_str()) == 0)
                logger.println("[WebServer] removed old photo: " + entries[i].name);
        }
    }

    Controller&     car_;
    Locate&         locator_;
    std::string     jpgDir_;
    int             maxPhotos_;
    int             maxJpgs_;
    unsigned long   lastCleanup_ = 0;
    httplib::Server server_;
    std::thread     server_thread_;
};
