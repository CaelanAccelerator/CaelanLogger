#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <algorithm>

#include "BackendLogger.h"
#include "ThreadLogger.h"
#include "LogStream.h"
#include "Level.h"
#include <AsyncLogger.h>

namespace fs = std::filesystem;

// ----------------------- helpers -----------------------

static std::vector<fs::path> recent_files(const fs::path& dir, fs::file_time_type start_time) {
    std::vector<fs::path> out;
    if (!fs::exists(dir)) return out;

    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::error_code ec;
        auto mt = fs::last_write_time(e.path(), ec);
        if (ec) continue;
        if (mt >= start_time) out.push_back(e.path());
    }
    return out;
}

static std::size_t count_token_occurrences(const fs::path& p, const std::string& token) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return 0;

    constexpr std::size_t kChunk = 64 * 1024;
    std::string buf(kChunk, '\0');

    std::string carry;
    carry.reserve(token.size());

    std::size_t count = 0;
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = in.gcount();
        if (n <= 0) break;

        std::string view;
        view.reserve(carry.size() + (std::size_t)n);
        view.append(carry);
        view.append(buf.data(), (std::size_t)n);

        std::size_t pos = 0;
        while (true) {
            pos = view.find(token, pos);
            if (pos == std::string::npos) break;
            ++count;
            pos += token.size();
        }

        if (token.size() > 1) {
            std::size_t keep = token.size() - 1;
            if (view.size() > keep) carry.assign(view.end() - keep, view.end());
            else carry = view;
        }
        else {
            carry.clear();
        }
    }
    return count;
}

static std::unordered_map<std::string, std::size_t>
find_token_in_recent_files(const fs::path& logDir, fs::file_time_type start_time, const std::string& token) {
    std::unordered_map<std::string, std::size_t> hits;
    auto files = recent_files(logDir, start_time);
    for (const auto& p : files) {
        auto c = count_token_occurrences(p, token);
        if (c > 0) hits[p.string()] = c;
    }
    return hits;
}

static std::unordered_map<std::string, std::size_t>
retry_find(const fs::path& logDir,
    fs::file_time_type start_time,
    const std::string& token,
    int attempts = 60,
    int sleep_ms = 50) {
    for (int i = 0; i < attempts; ++i) {
        auto hits = find_token_in_recent_files(logDir, start_time, token);
        if (!hits.empty()) return hits;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return {};
}

static std::string make_unique_token(const char* tag) {
    auto t = (long long)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::string("<<") + tag + "_" + std::to_string(t) + ">>";
}

static std::string make_payload(std::size_t bytes) {
    return std::string(bytes, 'X');
}

static void cleanup_files_with_token(const fs::path& logDir,
    fs::file_time_type start_time,
    const std::string& token) {
    auto hits = find_token_in_recent_files(logDir, start_time, token);
    for (auto& kv : hits) {
        std::error_code ec;
        fs::remove(fs::path(kv.first), ec);
    }
}

static void cleanup_files_with_tokens(const fs::path& logDir,
    fs::file_time_type start_time,
    const std::vector<std::string>& tokens) {
    for (const auto& t : tokens) cleanup_files_with_token(logDir, start_time, t);
}

static std::size_t compute_safe_payload_len(std::size_t maxLine,
    std::string_view worst_prefix,
    std::size_t desired_payload_len,
    std::size_t extra_suffix_bytes) {
    if (worst_prefix.size() + extra_suffix_bytes >= maxLine) return 0;
    std::size_t cap = maxLine - worst_prefix.size() - extra_suffix_bytes;
    return std::min(desired_payload_len, cap);
}

// ----------------------- tests -----------------------

TEST(LoggerIntegration, HeavySingleThread_ManyHandoffs_NoLoss_WithinMaxLine) {
    const fs::path logDir = fs::path("./log");
    auto start_time = fs::file_time_type::clock::now() - std::chrono::seconds(3);

    const size_t bufSize = 6400;
    const int kLines = 50'000;
    const int kHandoffEvery = 200;
    const std::string token = make_unique_token("TL_HEAVY_1T");

    constexpr std::size_t kMaxLine = 1028;
    constexpr std::size_t kLevelPrefix = 5; 
    constexpr std::size_t kNewline = 1;

    const std::string worst_user_prefix =
        std::string("L=") + std::to_string(kLines - 1) + " " + token + " ";

    const std::size_t desiredPayloadLen = 180;
    const std::size_t payloadLen = compute_safe_payload_len(
        kMaxLine,
        worst_user_prefix,
        desiredPayloadLen,
        kLevelPrefix + kNewline
    );

    ASSERT_GT(payloadLen, 0u);
    const std::string payload = make_payload(payloadLen);

    
    AsyncLogger::init(bufSize);
    for (int i = 0; i < kLines; ++i) {
        LOG(INFO) << "L=" << i << " " << token << " " << payload;

        if ((i + 1) % kHandoffEvery == 0) {
            AsyncLogger::getInstance().tls().handoff();
        }
    }
    AsyncLogger::getInstance().tls().handoff();
    
	AsyncLogger::getInstance().shutdown();

    auto hits = retry_find(logDir, start_time, token);
    if (hits.empty()) {
        std::cerr << "CWD=" << fs::current_path() << "\n";
    }
    ASSERT_FALSE(hits.empty()) << "Token not found: " << token;

    std::size_t total = 0;
    for (auto& kv : hits) total += kv.second;

    EXPECT_EQ(total, (std::size_t)kLines);
    cleanup_files_with_token(logDir, start_time, token);
}

TEST(LoggerIntegration, HeavyMultiThread_PerThreadNoLoss_WithinMaxLine) {
    const fs::path logDir = fs::path("./log");
    auto start_time = fs::file_time_type::clock::now() - std::chrono::seconds(3);

    const size_t bufSize = 2000;
    const int kThreads = 6;
    const int kLinesPerThread = 5000;
    const int kHandoffEvery = 300;

    constexpr std::size_t kMaxLine = 1028;
    constexpr std::size_t kLevelPrefix = 5; // "INFO "
    constexpr std::size_t kNewline = 1;

    std::vector<std::string> tokens;
    tokens.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        const std::string tag = "TL_MT_T" + std::to_string(t);
        tokens.push_back(make_unique_token(tag.c_str()));
    }

    std::size_t payloadLen = (std::size_t)-1;
    for (int t = 0; t < kThreads; ++t) {
        const std::string worst_user_prefix =
            std::string("T=") + std::to_string(kThreads - 1) +
            " I=" + std::to_string(kLinesPerThread - 1) +
            " " + tokens[t] + " ";

        payloadLen = std::min(payloadLen, compute_safe_payload_len(
            kMaxLine,
            worst_user_prefix,
            /*desired*/120,
            kLevelPrefix + kNewline
        ));
    }
    ASSERT_GT(payloadLen, 0u);
    const std::string payload = make_payload(payloadLen);

    std::vector<std::thread> ths;
    ths.reserve(kThreads);

    AsyncLogger::getInstance().restart(bufSize);

    for (int t = 0; t < kThreads; ++t) {
        ths.emplace_back([&, t] {
            for (int i = 0; i < kLinesPerThread; ++i) {
                LOG(INFO) << "T=" << t << " I=" << i << " " << tokens[t] << " " << payload;

                if ((i + 1) % kHandoffEvery == 0) {
                    AsyncLogger::getInstance().tls().handoff(); 
                }
            }
            AsyncLogger::getInstance().tls().handoff();
            });
    }

    for (auto& th : ths) th.join();
	AsyncLogger::getInstance().shutdown();

    for (int t = 0; t < kThreads; ++t) {
        auto hits = retry_find(logDir, start_time, tokens[t]);
        if (hits.empty()) {
            std::cerr << "CWD=" << fs::current_path() << "\n";
        }
        ASSERT_FALSE(hits.empty()) << "Missing token for thread " << t;

        std::size_t total = 0;
        for (auto& kv : hits) total += kv.second;

        EXPECT_EQ(total, (std::size_t)kLinesPerThread)
            << "Thread " << t << " token count mismatch.";
    }

    cleanup_files_with_tokens(logDir, start_time, tokens);
}
