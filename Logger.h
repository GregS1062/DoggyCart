// ============================================================
// Logger.h  —  dual Serial + LittleFS file logger
// ============================================================
//
// Usage:
//   logger.println("msg");          // char* or F() macro
//   logger.println(someString);     // String
//   logger.printf("val=%d", n);     // formatted
//
//   logger.getContent()             // flush, read, reopen — safe while enabled
//   logger.clear()                  // delete log and reopen if enabled
//
// File: /log.txt on LittleFS.  Capped at MAX_BYTES; clears on overflow.
// File handle stays open between writes — only closed for read or clear.
// Serial: raw message (no timestamp).
// File:   [<ms>ms] message.
// ============================================================
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>

class Logger {
public:
    static constexpr size_t      MAX_BYTES = 8192;
    static constexpr const char* PATH      = "/log.txt";

    bool begin() {
        if (!LittleFS.begin(true)) {
            Serial.println(F("[Logger] LittleFS mount failed"));
            return false;
        }
        ready_ = true;
        Serial.println("[Logger] started (file logging disabled until Start Log)");
        return true;
    }

    void enable() {
        enabled_ = true;
        openFile_();
        write_("[Logger] file logging enabled");
    }

    bool isEnabled() const { return enabled_; }

    void println(const char* msg)                { write_(msg); }
    void println(const String& msg)              { write_(msg.c_str()); }
    void println(const __FlashStringHelper* msg) { String s(msg); write_(s.c_str()); }

    void printf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        write_(buf);
    }

    // Flush and close the write handle, read the file, then reopen for appending.
    String getContent() {
        if (!ready_) return F("(logger not ready)");
        if (file_) {
            file_.flush();
            file_.close();
        }
        File f = LittleFS.open(PATH, "r");
        String s = f ? f.readString() : String(F("(empty)"));
        if (f) f.close();
        if (enabled_) openFile_();
        return s;
    }

    void clear() {
        if (!ready_) return;
        if (file_) file_.close();
        LittleFS.remove(PATH);
        fileSize_ = 0;
        write_("[Logger] cleared");   // reopens file if enabled; Serial-only otherwise
    }

private:
    bool   ready_    = false;
    bool   enabled_  = false;
    File   file_;
    size_t fileSize_ = 0;

    void openFile_() {
        file_     = LittleFS.open(PATH, "a");
        fileSize_ = file_ ? (size_t)file_.size() : 0;
    }

    void write_(const char* msg) {
        Serial.println(msg);
        if (!ready_ || !enabled_) return;
        if (!file_) openFile_();
        if (!file_) return;

        if (fileSize_ > MAX_BYTES) {
            file_.close();
            LittleFS.remove(PATH);
            file_     = LittleFS.open(PATH, "w");
            fileSize_ = 0;
            if (!file_) return;
            const char* ovf = "--- overflow: log cleared ---";
            file_.println(ovf);
            file_.flush();
            fileSize_ = strlen(ovf) + 2;
        }

        file_.print('[');
        file_.print(millis());
        file_.print(F("ms] "));
        file_.println(msg);
        file_.flush();
        // Overestimate: '[' + 10-digit millis + 'ms] ' + msg + '\r\n'
        fileSize_ += 17 + strlen(msg);
    }
};

extern Logger logger;
