#include <chrono>
#include <random>
#include <thread>
#include <array>
#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <algorithm>

// POSIX write/open (Ubuntu/WSL)
#include <fcntl.h>
#include <unistd.h>

#include "AsyncLogger.h"   // your logger

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// ---------------- Timer ----------------
class Timer {
    Clock::time_point start_;
public:
    Timer() : start_(Clock::now()) {}
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
};

// ---------------- Small compute workload (prevents optimizer from removing work) ----------------
static inline uint64_t xorshift64(uint64_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

static inline uint64_t do_work(uint64_t seed, int rounds) {
    // deterministic compute
    for (int i = 0; i < rounds; ++i) seed = xorshift64(seed + (uint64_t)i);
    return seed;
}

// ---------------- Sync logger (simple baseline) ----------------
class SyncLogger {
public:
    explicit SyncLogger(fs::path dir, std::string prefix = "sync")
        : dir_(std::move(dir)), prefix_(std::move(prefix))
    {
        std::error_code ec;
        fs::create_directories(dir_, ec);

        std::string filename = prefix_ + "_bench.log";
        fs::path full = dir_ / filename;

        fd_ = ::open(full.string().c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            throw std::runtime_error(std::string("SyncLogger open failed: ") + std::strerror(errno));
        }
    }

    ~SyncLogger() {
        if (fd_ >= 0) ::close(fd_);
    }

    // writes: "INFO " + msg + "\n" to match your async prefix/newline behavior
    void log_info(const char* msg, size_t len) {
        std::lock_guard<std::mutex> lk(mu_);
        write_all("INFO ", 5);
        write_all(msg, len);
        write_all("\n", 1);
    }

private:
    void write_all(const char* data, size_t len) {
        size_t off = 0;
        while (off < len) {
            ssize_t n = ::write(fd_, data + off, len - off);
            if (n > 0) { off += (size_t)n; continue; }
            if (n < 0 && errno == EINTR) continue;
            throw std::runtime_error(std::string("SyncLogger write failed: ") + std::strerror(errno));
        }
    }

private:
    fs::path dir_;
    std::string prefix_;
    int fd_{ -1 };
    std::mutex mu_;
};

// ---------------- Results ----------------
struct BenchResult {
    double ms = 0.0;
    uint64_t lines = 0;
    uint64_t bytes = 0;
    uint64_t work_acc = 0; // checksum to keep work "real"
};

static std::string make_payload(size_t bytes) {
    return std::string(bytes, 'X');
}

// Fixed-width prefix so message length is stable-ish
static inline void build_message(
    std::string& out,
    size_t tid,
    size_t i,
    uint64_t x,
    const std::string& payload)
{
    // no "INFO " and no trailing '\n' here (logger adds it)
    char head[128];
    int n = std::snprintf(head, sizeof(head),
        "T=%02zu I=%08zu X=%016llx ",
        tid, i, (unsigned long long)x);

    out.clear();
    out.append(head, head + std::max(0, n));
    out.append(payload);
}

// ---------------- Benchmark runners ----------------
static BenchResult run_sync(size_t threads,
    size_t lines_per_thread,
    int work_rounds,
    size_t payload_bytes,
    fs::path log_dir)
{
    SyncLogger slog(std::move(log_dir), "sync");
    const std::string payload = make_payload(payload_bytes);

    std::atomic<uint64_t> checksum{ 0 };

    Timer t;
    std::vector<std::thread> ths;
    ths.reserve(threads);

    for (size_t tid = 0; tid < threads; ++tid) {
        ths.emplace_back([&, tid] {
            thread_local std::string msg;
            msg.reserve(256 + payload.size());

            uint64_t local = 0;
            for (size_t i = 0; i < lines_per_thread; ++i) {
                uint64_t x = do_work((uint64_t)tid * 1315423911ull + i, work_rounds);
                local ^= x;

                build_message(msg, tid, i, x, payload);
                slog.log_info(msg.data(), msg.size());
            }
            checksum.fetch_xor(local, std::memory_order_relaxed);
            });
    }

    for (auto& th : ths) th.join();

    BenchResult r;
    r.ms = t.elapsed_ms();
    r.lines = (uint64_t)threads * (uint64_t)lines_per_thread;

    // bytes written per line (match "INFO " + msg + "\n")
    // msg length = fixed header + payload_bytes, but header varies slightly if digits change; close enough.
    // For exact bytes, you can count per log, but that adds overhead.
    r.bytes = 0; // optional
    r.work_acc = checksum.load(std::memory_order_relaxed);
    return r;
}

static BenchResult run_async(size_t threads,
    size_t lines_per_thread,
    int work_rounds,
    size_t payload_bytes,
    size_t handoff_every,
    size_t async_buf_size)
{
    // IMPORTANT: call init BEFORE first getInstance() use
    AsyncLogger::init(async_buf_size);

    const std::string payload = make_payload(payload_bytes);
    std::atomic<uint64_t> checksum{ 0 };

    Timer t;
    std::vector<std::thread> ths;
    ths.reserve(threads);

    for (size_t tid = 0; tid < threads; ++tid) {
        ths.emplace_back([&, tid] {
            thread_local std::string msg;
            msg.reserve(256 + payload.size());

            uint64_t local = 0;
            for (size_t i = 0; i < lines_per_thread; ++i) {
                uint64_t x = do_work((uint64_t)tid * 1315423911ull + i, work_rounds);
                local ^= x;

                build_message(msg, tid, i, x, payload);
                LOG(INFO) << msg;

                if (handoff_every && ((i + 1) % handoff_every == 0)) {
                    AsyncLogger::getInstance().tls().handoff();
                }
            }
            AsyncLogger::getInstance().tls().handoff();
            checksum.fetch_xor(local, std::memory_order_relaxed);
            });
    }

    for (auto& th : ths) th.join();

    // ensure backend drains
    AsyncLogger::getInstance().shutdown();

    BenchResult r;
    r.ms = t.elapsed_ms();
    r.lines = (uint64_t)threads * (uint64_t)lines_per_thread;
    r.bytes = 0; // optional
    r.work_acc = checksum.load(std::memory_order_relaxed);
    return r;
}

static void print_result(const char* name, const BenchResult& r,
    size_t threads, size_t lines_per_thread, int work_rounds)
{
    const double sec = r.ms / 1000.0;
    const double lines_per_sec = (sec > 0) ? (double)r.lines / sec : 0.0;
    const double work_ops = (double)r.lines * (double)work_rounds; // "rounds" as proxy for work
    const double work_per_sec = (sec > 0) ? work_ops / sec : 0.0;

    std::cout << "\n[" << name << "]\n";
    std::cout << "  time: " << r.ms << " ms\n";
    std::cout << "  lines: " << r.lines << " (" << threads << " * " << lines_per_thread << ")\n";
    std::cout << "  lines/sec: " << lines_per_sec << "\n";
    std::cout << "  work(rounds)/sec: " << work_per_sec << "\n";
    std::cout << "  checksum: 0x" << std::hex << r.work_acc << std::dec << "\n";
}

static void set_env(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

int main() {
    std::cout << "Starting logger benchmark...\n";

    // ---- Tunables ----
    const size_t threads = 8;
    const size_t lines_per_thread = 50'000;   // increase for more stable results
    const size_t payload_bytes = 256;         // affects IO pressure
    const int work_rounds = 32;               // "compute per log"
    const size_t handoff_every = 256;
    const size_t async_buf_size = 64 * 1024;

    // Put logs somewhere fast/easy (WSL: /tmp is good)
    const fs::path sync_dir = "/tmp/caelan_bench_sync";
    const char* async_dir_env = "/tmp/caelan_bench_async";

    // Make sure async logger writes to known directory (your FileUtil env override)
    set_env("CAELAN_LOG_DIR", async_dir_env);

    // Warm-up (optional): reduce first-run noise
    // (You can add a small warmup run if you want.)

    // Run Sync
    auto r_sync = run_sync(threads, lines_per_thread, work_rounds, payload_bytes, sync_dir);
    print_result("SyncLogger (mutex + write)", r_sync, threads, lines_per_thread, work_rounds);

    // Run Async
    auto r_async = run_async(threads, lines_per_thread, work_rounds, payload_bytes,
        handoff_every, async_buf_size);
    print_result("AsyncLogger (your logger)", r_async, threads, lines_per_thread, work_rounds);

    std::cout << "\nLog dirs:\n";
    std::cout << "  sync:  " << sync_dir << "\n";
    std::cout << "  async: " << async_dir_env << "\n";

    return 0;
}
