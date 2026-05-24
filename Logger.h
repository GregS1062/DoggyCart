// ============================================================
// Logger.h  —  dual stdout + file logger (RPi5)
// ============================================================
//
// Drop-in replacement for the ESP32 version.
// LittleFS → POSIX FILE* at LOG_PATH.
// Serial    → printf() / stdout.
// millis()  → std::chrono via arduino_compat.h.
//
// Usage:
//   logger.println("msg");          // const char* or std::string
//   logger.printf("val=%d", n);     // formatted
//   logger.getContent()             // returns log file as std::string
//   logger.clear()                  // delete and reopen log file
//
// File capped at MAX_BYTES; cleared on overflow.
// ============================================================
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "arduino_compat.h"

class Logger {
public:
    static constexpr size_t      MAX_BYTES = 8192;
    static constexpr const char* PATH      = "/tmp/doggycart.log";

    bool begin() {
        ready_ = true;
        printf("[Logger] started (file logging disabled until Start Log)\n");
        return true;
    }

    void enable() {
        enabled_ = true;
        openFile_();
        write_("[Logger] file logging enabled");
    }

    bool isEnabled() const { return enabled_; }

    void println(const char* msg)        { write_(msg); }
    void println(const std::string& msg) { write_(msg.c_str()); }

    void printf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        write_(buf);
    }

    std::string getContent() {
        if (!ready_) return "(logger not ready)";
        if (file_) {
            fflush(file_);
            fclose(file_);
            file_ = nullptr;
        }
        FILE* f = fopen(PATH, "r");
        std::string s;
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                s.resize(static_cast<size_t>(sz));
                fread(&s[0], 1, static_cast<size_t>(sz), f);
            }
            fclose(f);
        }
        if (enabled_) openFile_();
        return s.empty() ? std::string("(empty)") : s;
    }

    void clear() {
        if (!ready_) return;
        if (file_) { fclose(file_); file_ = nullptr; }
        remove(PATH);
        fileSize_ = 0;
        write_("[Logger] cleared");
    }

private:
    bool   ready_    = false;
    bool   enabled_  = false;
    FILE*  file_     = nullptr;
    size_t fileSize_ = 0;

    void openFile_() {
        file_ = fopen(PATH, "a");
        if (file_) {
            fseek(file_, 0, SEEK_END);
            fileSize_ = static_cast<size_t>(ftell(file_));
        }
    }

    void write_(const char* msg) {
        ::printf("%s\n", msg);
        if (!ready_ || !enabled_) return;
        if (!file_) openFile_();
        if (!file_) return;

        if (fileSize_ > MAX_BYTES) {
            fclose(file_);
            remove(PATH);
            file_     = fopen(PATH, "w");
            fileSize_ = 0;
            if (!file_) return;
            const char* ovf = "--- overflow: log cleared ---\n";
            fputs(ovf, file_);
            fflush(file_);
            fileSize_ = strlen(ovf);
        }

        ::fprintf(file_, "[%lums] %s\n", millis(), msg);
        fflush(file_);
        fileSize_ += 17 + strlen(msg);   // overestimate, same as original
    }
};

extern Logger logger;
