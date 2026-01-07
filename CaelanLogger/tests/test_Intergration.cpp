#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>

#include "BackendLogger.h"
#include "ThreadLogger.h"

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

// Streaming count of token occurrences (binary-safe, doesn't load whole file).
static std::size_t count_token_occurrences(const fs::path& p, const std::string& token) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return 0;

    constexpr std::size_t kChunk = 64 * 1024;
    std::string buf;
    buf.resize(kChunk);

    std::string carry; // keep overlap to not miss matches across chunk boundary
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

        // keep last (token.size()-1) bytes as overlap
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

// Find all recent files that contain token, and count occurrences per file.
static std::unordered_map<std::string, std::size_t>
find_token_in_recent_files(const fs::path& logDir,
    fs::file_time_type start_time,
    const std::string& token) {
    std::unordered_map<std::string, std::size_t> hits;
    auto files = recent_files(logDir, start_time);
    for (const auto& p : files) {
        auto c = count_token_occurrences(p, token);
        if (c > 0) hits[p.string()] = c;
    }
    return hits;
}

static std::string make_unique_token(const char* tag) {
    auto t = (long long)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::string("<<") + tag + "_" + std::to_string(t) + ">>";
}

static std::string make_payload(std::size_t bytes) {
    // deterministic payload, avoid huge allocations in loops
    std::string s;
    s.resize(bytes, 'X');
    return s;
}

// Delete only files that contain *our* token, so tests don't pollute workspace.
static void cleanup_files_with_token(const fs::path& logDir,
    fs::file_time_type start_time,
    const std::string& token) {
    auto hits = find_token_in_recent_files(logDir, start_time, token);
    for (auto& kv : hits) {
        std::error_code ec;
        fs::remove(fs::path(kv.first), ec);
        // ignore errors on cleanup
    }
}

// Retry helper: because backend is async and filesystem timestamps can be coarse.
static std::unordered_map<std::string, std::size_t>
retry_find(const fs::path& logDir, fs::file_time_type start_time, const std::string& token,
    int attempts = 10, int sleep_ms = 50) {
    for (int i = 0; i < attempts; ++i) {
        auto hits = find_token_in_recent_files(logDir, start_time, token);
        if (!hits.empty()) return hits;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return {};
}

TEST(LoggerIntegration, HeavySingleThread_ManyHandoffs_NoLoss) {
    const fs::path logDir = ".";
    auto start_time = fs::file_time_type::clock::now() - std::chrono::seconds(1);

    const size_t bufSize = 6400;           
    const int kLines = 50'000;            
    const int kHandoffEvery = 200;        
    const std::string token = make_unique_token("TL_HEAVY_1T");
    const std::string payload = make_payload(180); 

    BackendLogger backend(bufSize);
    backend.start();

    {
        ThreadLogger tl(bufSize, &backend);
        for (int i = 0; i < kLines; ++i) {
            tl << "L=" << i << " " << token << " " << payload << "\n";
            if ((i + 1) % kHandoffEvery == 0) tl.handoff();
        }
        tl.handoff();
    }

    backend.stop();

    auto hits = retry_find(logDir, start_time, token);
    if (hits.empty()) {
        std::cerr << "Token not found: " << token << "\n";
        std::cerr << "PWD=" << fs::current_path() << "\n";
    }
    ASSERT_FALSE(hits.empty());

    std::size_t total = 0;
    for (auto& kv : hits) total += kv.second;

    EXPECT_EQ(total, (std::size_t)kLines) << "Expected exactly one token per line.";
    cleanup_files_with_token(logDir, start_time, token);
}

TEST(LoggerIntegration, HeavyMultiThread_PerThreadNoLoss) {
    const fs::path logDir = ".";
    auto start_time = fs::file_time_type::clock::now() - std::chrono::seconds(1);

    const size_t bufSize = 2000;
    const int kThreads = 6;
    const int kLinesPerThread = 5000;
    const int kHandoffEvery = 300;
    const std::string payload = make_payload(120);

    BackendLogger backend(bufSize);
    backend.start();

    std::vector<std::string> tokens;
    tokens.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        tokens.push_back(make_unique_token(("TL_MT_T" + std::to_string(t)).c_str()));
    }

    std::vector<std::thread> ths;
    ths.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        ths.emplace_back([&, t] {
            ThreadLogger tl(bufSize, &backend);
            for (int i = 0; i < kLinesPerThread; ++i) {
                tl << "T=" << t << " I=" << i << " " << tokens[t] << " " << payload << "\n";
                if ((i + 1) % kHandoffEvery == 0) tl.handoff();
            }
            tl.handoff();
            });
    }

    for (auto& th : ths) th.join();
    backend.stop();

    // 对每个 token 分别扫描（避免一个大 token 混在一起不好定位问题）
    for (int t = 0; t < kThreads; ++t) {
        auto hits = retry_find(logDir, start_time, tokens[t]);
        ASSERT_FALSE(hits.empty()) << "Missing token for thread " << t;

        std::size_t total = 0;
        for (auto& kv : hits) total += kv.second;

        EXPECT_EQ(total, (std::size_t)kLinesPerThread)
            << "Thread " << t << " token count mismatch.";
        
    }cleanup_files_with_token(logDir, start_time, tokens[0]);
}

