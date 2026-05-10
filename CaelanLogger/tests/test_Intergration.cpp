#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AsyncLogger.h"
#include "BackendLogger.h"

namespace fs = std::filesystem;

// ----------------------- helpers -----------------------

static void purge_log_dir(const fs::path &logDir)
{
    std::error_code ec;

    if (!fs::exists(logDir))
        return;

    for (auto &e : fs::directory_iterator(logDir))
    {
        fs::remove(e.path(), ec);
        ec.clear();
    }
}

static std::vector<fs::path> log_files(const fs::path &dir)
{
    std::vector<fs::path> files;

    if (!fs::exists(dir))
        return files;

    for (auto &e : fs::directory_iterator(dir))
    {
        if (e.is_regular_file())
            files.push_back(e.path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

static std::string read_all_logs(const fs::path &logDir)
{
    std::string all;

    for (const auto &p : log_files(logDir))
    {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        all += ss.str();
        all += '\n';
    }

    return all;
}

static std::size_t count_occurrences(const std::string &text, const std::string &token)
{
    std::size_t count = 0;
    std::size_t pos = 0;

    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        ++count;
        pos += token.size();
    }

    return count;
}

// FileUtil::roll() writes dropped delta and resets the counter.
// So all "dropped: N" lines should be summed.
static std::size_t count_dropped_delta(const std::string &text)
{
    std::size_t total = 0;

    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
    {
        if (line.rfind("dropped: ", 0) == 0)
        {
            try
            {
                total += static_cast<std::size_t>(std::stoull(line.substr(9)));
            }
            catch (...)
            {
            }
        }
    }

    return total;
}

static std::string make_unique_token(const char *tag)
{
    auto now = std::chrono::high_resolution_clock::now()
                   .time_since_epoch()
                   .count();

    return std::string("<<") + tag + "_" + std::to_string(now) + ">>";
}

// For tests after the first singleton use:
// 1. restart/stop old state so old dropped counters get rolled
// 2. purge old files
// 3. restart clean logger for this test
static void restart_clean_logger(size_t bufSize, const fs::path &logDir)
{
    AsyncLogger::getInstance().restart(bufSize);
    AsyncLogger::getInstance().shutdown();

    purge_log_dir(logDir);

    AsyncLogger::getInstance().restart(bufSize);
}

// ----------------------- tests -----------------------

TEST(LoggerIntegration, SingleThread_LoggedPlusDroppedEqualsAttempted)
{
    const fs::path logDir = fs::current_path() / "log";
    purge_log_dir(logDir);

    const size_t bufSize = 6400;
    const int kLines = 50'000;
    const int kHandoffEvery = 200;

    const std::string token = make_unique_token("SINGLE");
    const std::string payload(180, 'X');

    AsyncLogger::init(bufSize);

    for (int i = 0; i < kLines; ++i)
    {
        LOG(INFO) << "L=" << i << " " << token << " " << payload;

        if ((i + 1) % kHandoffEvery == 0)
            AsyncLogger::getInstance().tls().handoff();
    }

    AsyncLogger::getInstance().tls().handoff(true);
    AsyncLogger::getInstance().shutdown();

    const std::string logs = read_all_logs(logDir);

    const std::size_t logged = count_occurrences(logs, token);
    const std::size_t dropped = count_dropped_delta(logs);

    EXPECT_EQ(logged + dropped, static_cast<std::size_t>(kLines))
        << "logged=" << logged << " dropped=" << dropped;
}

TEST(LoggerIntegration, MultiThread_LoggedPlusDroppedEqualsAttempted)
{
    const fs::path logDir = fs::current_path() / "log";

    const size_t bufSize = 2000;
    const int kThreads = 6;
    const int kLinesPerThread = 5000;
    const int kHandoffEvery = 300;

    restart_clean_logger(bufSize, logDir);

    std::vector<std::string> tokens;
    tokens.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t)
    {
        const std::string tag = "MT_" + std::to_string(t);
        tokens.push_back(make_unique_token(tag.c_str()));
    }

    const std::string payload(120, 'X');

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]
                             {
            for (int i = 0; i < kLinesPerThread; ++i)
            {
                LOG(INFO) << "T=" << t
                          << " I=" << i
                          << " " << tokens[t]
                          << " " << payload;

                if ((i + 1) % kHandoffEvery == 0)
                    AsyncLogger::getInstance().tls().handoff();
            }

            AsyncLogger::getInstance().tls().handoff(true); });
    }

    for (auto &th : threads)
        th.join();

    AsyncLogger::getInstance().shutdown();

    const std::string logs = read_all_logs(logDir);

    std::size_t logged = 0;
    for (const auto &token : tokens)
        logged += count_occurrences(logs, token);

    const std::size_t dropped = count_dropped_delta(logs);
    const std::size_t expected =
        static_cast<std::size_t>(kThreads) * static_cast<std::size_t>(kLinesPerThread);

    EXPECT_EQ(logged + dropped, expected)
        << "logged=" << logged << " dropped=" << dropped;
}

TEST(LeakCheck, BackendLoggerDestroysCleanly)
{
    {
        BackendLogger bl(2000, "/tmp/test_log");
        bl.start();
        bl.stop();
    }

    SUCCEED();
}