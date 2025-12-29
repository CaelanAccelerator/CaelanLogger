#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>

#include "BackendLogger.h"
#include "ThreadLogger.h"

namespace fs = std::filesystem;

// Read up to max_bytes from the end of file p (binary-safe).
// Used to avoid loading entire large log files while still finding a token near the tail.
static std::string read_tail(const fs::path& p, std::size_t max_bytes = 256 * 1024) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};

    in.seekg(0, std::ios::end);
    std::streamoff sz = in.tellg();
    if (sz <= 0) return {};

    std::streamoff start = sz > (std::streamoff)max_bytes ? (sz - (std::streamoff)max_bytes) : 0;
    in.seekg(start, std::ios::beg);

    std::string buf;
    buf.resize((size_t)(sz - start));
    in.read(buf.data(), (std::streamsize)buf.size());
    return buf;
}

// Enumerate files in dir whose last write time is >= start_time.
// This narrows the search to files potentially created/updated by this test run.
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

// Integration test:
// - Starts the BackendLogger writer thread.
// - Uses ThreadLogger to emit many lines containing a unique token.
// - Hands off the buffer, then stops the backend.
// - Scans recently modified files in the working directory and asserts the token was persisted.
//
// Notes:
// - The retry loop with small sleeps accounts for async flush/write timing.
// - logDir is ".", which assumes the implementation writes in the current directory or a child.
//   Update the directory if your backend targets a specific log folder.
TEST(LoggerIntegration, WritesExpectedTokenToFile) {
    const fs::path logDir = "."; 
    const auto start_time = fs::file_time_type::clock::now();

    const size_t bufSize = 256;
    BackendLogger backend(bufSize);
    backend.start();

    // Create a unique token to search in the produced logs to avoid false positives.
    const std::string token =
        std::string("<<TL_DL_ITEST_TOKEN_") +
        std::to_string((long long)std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        ">>";

    {
        // Scope ensures ThreadLogger destructor runs before stopping the backend.
        ThreadLogger tl(bufSize, &backend);
        for (int i = 0; i < 2000; ++i) {
            tl << "hello " << i << " " << token << '\n';
        }
        // Ensure buffered data is handed off to the backend for persistence.
        tl.handoff();
    }

    // Stop writer thread and finalize any pending writes.
    backend.stop();

    // Retry a few times to tolerate filesystem timestamp granularity and async persistence.
    bool found = false;
    for (int attempt = 0; attempt < 10 && !found; ++attempt) {
        auto files = recent_files(logDir, start_time);

        for (const auto& p : files) {   
            std::string tail = read_tail(p, 256 * 1024);
            if (tail.find(token) != std::string::npos) {
                found = true;
                break;
            }
        }

        if (!found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Helpful diagnostics on failure to aid debugging in CI/local runs.
    if (!found) {
        std::cerr << "PWD=" << fs::current_path() << "\n";
        std::cerr << "Token not found: " << token << "\n";
        auto files = recent_files(logDir, start_time);
        std::cerr << "Recent files count=" << files.size() << "\n";
        for (auto& p : files) {
            std::error_code ec;
            auto sz = fs::file_size(p, ec);
            std::cerr << "  file=" << p.string()
                << " size=" << (ec ? -1 : (long long)sz) << "\n";
        }
    }

    EXPECT_TRUE(found);
}
