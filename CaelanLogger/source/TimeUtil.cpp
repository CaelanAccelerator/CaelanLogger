// Time.cpp (Linux only)

#include "TimeUtil.h"

#include <ctime>
#include <cstdio>
#include <string>
#include <time.h>   // clock_gettime

namespace LogTime {

    namespace {

        // Get current local time components with milliseconds.
        // - sec: seconds since epoch
        // - ms : milliseconds [0,999]
        inline void now_sec_ms(std::time_t& sec, int& ms) {
            timespec ts{};
            ::clock_gettime(CLOCK_REALTIME, &ts);
            sec = static_cast<std::time_t>(ts.tv_sec);
            ms = static_cast<int>(ts.tv_nsec / 1000000); // ns -> ms
        }

        // Cache "YYYY-MM-DD HH:MM:SS" once per second (thread-local).
        inline const char* cached_datetime_prefix(std::time_t sec) {
            // "YYYY-MM-DD HH:MM:SS" => 19 chars + '\0'
            thread_local std::time_t cached_sec = 0;
            thread_local char prefix[20];

            if (sec != cached_sec) {
                cached_sec = sec;
                std::tm tm_buf{};
                ::localtime_r(&sec, &tm_buf);

                // 19 chars + null terminator
                std::snprintf(prefix, sizeof(prefix),
                    "%04d-%02d-%02d %02d:%02d:%02d",
                    tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                    tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
            }
            return prefix;
        }

        // Cache "HH:MM:SS" once per second (thread-local).
        inline const char* cached_time_prefix(std::time_t sec) {
            // "HH:MM:SS" => 8 chars + '\0'
            thread_local std::time_t cached_sec = 0;
            thread_local char prefix[9];

            if (sec != cached_sec) {
                cached_sec = sec;
                std::tm tm_buf{};
                ::localtime_r(&sec, &tm_buf);

                std::snprintf(prefix, sizeof(prefix),
                    "%02d:%02d:%02d",
                    tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
            }
            return prefix;
        }

    } // anonymous namespace

    // Default timestamp string for logging.
    // Recommended: same as nowDateString() for clarity.
    std::string nowString() {
        return nowDateString();
    }

    // "HH:MM:SS.mmm"
    std::string nowTimeOnlyString() {
        std::time_t sec;
        int ms;
        now_sec_ms(sec, ms);

        const char* prefix = cached_time_prefix(sec);

        // "HH:MM:SS.mmm" => 12 chars
        char buf[16];
        const int n = std::snprintf(buf, sizeof(buf), "%s.%03d", prefix, ms);
        return std::string(buf, static_cast<size_t>(n));
    }

    // "YYYY-MM-DD HH:MM:SS.mmm"
    std::string nowDateString() {
        std::time_t sec;
        int ms;
        now_sec_ms(sec, ms);

        const char* prefix = cached_datetime_prefix(sec);

        // "YYYY-MM-DD HH:MM:SS.mmm" => 23 chars
        char buf[32];
        const int n = std::snprintf(buf, sizeof(buf), "%s.%03d", prefix, ms);
        return std::string(buf, static_cast<size_t>(n));
    }

} // namespace LogTime
