#include <gtest/gtest.h>

#include <regex>
#include <string>
#include <thread>
#include <chrono>

#include "TimeUtil.h"  // Adjust to your actual header path

namespace {

    // Validate "YYYY-MM-DD HH:MM:SS.mmm"
    bool IsValidDateTimeMs(const std::string& s) {
        // Capture groups:
        // 1=year, 2=month, 3=day, 4=hour, 5=minute, 6=second, 7=millisecond
        static const std::regex re(
            R"(^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2})\.(\d{3})$)"
        );

        std::smatch m;
        if (!std::regex_match(s, m, re)) return false;

        auto toInt = [](const std::ssub_match& sm) { return std::stoi(sm.str()); };

        const int year = toInt(m[1]);
        const int month = toInt(m[2]);
        const int day = toInt(m[3]);
        const int hour = toInt(m[4]);
        const int min = toInt(m[5]);
        const int sec = toInt(m[6]);
        const int ms = toInt(m[7]);

        // Basic range checks (kept simple; not validating month/day lengths or leap years here)
        if (year < 1970 || year > 3000) return false;
        if (month < 1 || month > 12) return false;
        if (day < 1 || day > 31) return false;
        if (hour < 0 || hour > 23) return false;
        if (min < 0 || min > 59) return false;
        if (sec < 0 || sec > 60) return false;   // allow leap second
        if (ms < 0 || ms > 999) return false;

        return true;
    }

    // Validate "HH:MM:SS" or "HH:MM:SS.mmm"
    bool IsValidTimeOnly(const std::string& s) {
        // Two accepted formats: with milliseconds or without milliseconds
        static const std::regex re_ms(R"(^(\d{2}):(\d{2}):(\d{2})\.(\d{3})$)");
        static const std::regex re_s(R"(^(\d{2}):(\d{2}):(\d{2})$)");

        std::smatch m;
        auto toInt = [](const std::ssub_match& sm) { return std::stoi(sm.str()); };

        if (std::regex_match(s, m, re_ms)) {
            const int hh = toInt(m[1]);
            const int mm = toInt(m[2]);
            const int ss = toInt(m[3]);
            const int ms = toInt(m[4]);

            // Basic range checks
            return (0 <= hh && hh <= 23) &&
                (0 <= mm && mm <= 59) &&
                (0 <= ss && ss <= 60) &&
                (0 <= ms && ms <= 999);
        }

        if (std::regex_match(s, m, re_s)) {
            const int hh = toInt(m[1]);
            const int mm = toInt(m[2]);
            const int ss = toInt(m[3]);

            // Basic range checks
            return (0 <= hh && hh <= 23) &&
                (0 <= mm && mm <= 59) &&
                (0 <= ss && ss <= 60);
        }

        return false;
    }

} // anonymous namespace

TEST(LogTimeTest, NowDateString_FormatAndRange) {
    // Ensure nowDateString() returns a non-empty string and matches the expected format.
    const std::string s = LogTime::nowDateString();
    ASSERT_FALSE(s.empty());
    EXPECT_TRUE(IsValidDateTimeMs(s)) << "nowDateString() returned: " << s;
}

TEST(LogTimeTest, NowTimeOnlyString_FormatAndRange) {
    // Ensure nowTimeOnlyString() returns a non-empty string and matches accepted time-only formats.
    const std::string s = LogTime::nowTimeOnlyString();
    ASSERT_FALSE(s.empty());
    EXPECT_TRUE(IsValidTimeOnly(s)) << "nowTimeOnlyString() returned: " << s;
}

TEST(LogTimeTest, NowString_NotEmptyAndCallable) {
    // We don't know the exact format of nowString(), so we only check:
    // 1) it's non-empty
    // 2) repeated calls work (no crash / no undefined behavior)
    const std::string a = LogTime::nowString();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const std::string b = LogTime::nowString();

    EXPECT_FALSE(a.empty());
    EXPECT_FALSE(b.empty());

    // If your nowString() is designed so that lexicographic order follows time order,
    // you may enable this check:
    // EXPECT_LE(a, b) << "Expected nowString() to be non-decreasing lexicographically.";
}

TEST(LogTimeTest, NowDateString_LexOrderNonDecreasing) {
    // With a fixed-width "YYYY-MM-DD HH:MM:SS.mmm" format,
    // lexicographic order should match chronological order.
    const std::string a = LogTime::nowDateString();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const std::string b = LogTime::nowDateString();

    ASSERT_TRUE(IsValidDateTimeMs(a));
    ASSERT_TRUE(IsValidDateTimeMs(b));

    EXPECT_LE(a, b) << "Expected lexicographic order to follow time order. a=" << a << " b=" << b;
}
