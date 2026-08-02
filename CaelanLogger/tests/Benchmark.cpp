#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "AsyncLogger.h"
#include "SharedBackend.h"
#include "UringBackend.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

struct BenchConfig
{
    int threads = 8;
    int linesPerThread = 50'000;
    int workRounds = 512;
    int handoffEvery = 256;
    std::size_t asyncBufferSize = 128 * 1024;
    std::string payload = std::string(256, 'X');
};

struct BenchResult
{
    double producerMs = 0.0;
    double endToEndMs = 0.0;
    std::size_t attempted = 0;
    std::size_t logged = 0;
    std::size_t dropped = 0;
    std::uint64_t checksum = 0;
};

static void reset_dir(const fs::path &dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();
    fs::create_directories(dir, ec);
}

static std::vector<fs::path> files_in_dir(const fs::path &dir)
{
    std::vector<fs::path> files;

    if (!fs::exists(dir))
        return files;

    for (const auto &e : fs::directory_iterator(dir))
    {
        if (e.is_regular_file())
            files.push_back(e.path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

static std::string read_all_files(const fs::path &dir)
{
    std::string out;

    for (const auto &p : files_in_dir(dir))
    {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        out += ss.str();
        out += '\n';
    }

    return out;
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

// NormalWriter::roll() writes dropped delta and resets it.
// Therefore, sum all dropped lines in this benchmark run.
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

static std::uint64_t do_work(int rounds, std::uint64_t seed)
{
    std::uint64_t x = seed;

    for (int i = 0; i < rounds; ++i)
    {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= 2685821657736338717ULL;
    }

    return x;
}

class SyncLogger
{
public:
    explicit SyncLogger(const fs::path &file)
    {
        fd_ = ::open(file.string().c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd_ < 0)
            throw std::runtime_error("failed to open sync log file");
    }

    ~SyncLogger()
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

    void log(const std::string &line)
    {
        std::lock_guard<std::mutex> lock(mu_);
        ::write(fd_, line.data(), line.size());
    }

private:
    int fd_ = -1;
    std::mutex mu_;
};

static BenchResult run_sync(const BenchConfig &cfg, const fs::path &dir, const std::string &token)
{
    reset_dir(dir);

    SyncLogger logger(dir / "sync.log");

    const std::size_t attempted =
        static_cast<std::size_t>(cfg.threads) * static_cast<std::size_t>(cfg.linesPerThread);

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> checksums(cfg.threads, 0);

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < cfg.threads; ++t)
    {
        threads.emplace_back([&, t]
                             {
            std::uint64_t local = 0;

            for (int i = 0; i < cfg.linesPerThread; ++i)
            {
                local ^= do_work(cfg.workRounds, static_cast<std::uint64_t>(t) << 32 | static_cast<std::uint64_t>(i));

                logger.log("INFO " + token +
                           " T=" + std::to_string(t) +
                           " I=" + std::to_string(i) +
                           " " + cfg.payload + "\n");
            }

            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto x : checksums)
        checksum ^= x;

    const std::string logs = read_all_files(dir);

    BenchResult r;
    r.producerMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.endToEndMs = r.producerMs;
    r.attempted = attempted;
    r.logged = count_occurrences(logs, token);
    r.dropped = 0;
    r.checksum = checksum;
    return r;
}

static BenchResult run_async(const BenchConfig &cfg, const fs::path &dir, const std::string &token)
{
    reset_dir(dir);

#if defined(_WIN32)
    _putenv_s("CAELAN_LOG_DIR", dir.string().c_str());
#else
    setenv("CAELAN_LOG_DIR", dir.string().c_str(), 1);
#endif

    const std::size_t attempted =
        static_cast<std::size_t>(cfg.threads) * static_cast<std::size_t>(cfg.linesPerThread);

    AsyncLogger<SharedBackend> logger(cfg.asyncBufferSize, 32, dir.string());

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> checksums(cfg.threads, 0);
    std::vector<std::vector<long long>> threadLats(cfg.threads);

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < cfg.threads; ++t)
    {
        threads.emplace_back([&, t]
                             {
            std::uint64_t local = 0;
            threadLats[t].reserve(cfg.linesPerThread);

            for (int i = 0; i < cfg.linesPerThread; ++i)
            {
                local ^= do_work(cfg.workRounds, static_cast<std::uint64_t>(t) << 32 | static_cast<std::uint64_t>(i));

                auto t0 = std::chrono::steady_clock::now();
                LOG_TO(logger, INFO) << token
                                     << " T=" << t
                                     << " I=" << i
                                     << " " << cfg.payload;
                auto t1 = std::chrono::steady_clock::now();
                threadLats[t].push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }

            logger.tls().handoff(true);

            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto producersDone = std::chrono::steady_clock::now();

    {
        std::vector<long long> allLat;
        allLat.reserve(static_cast<std::size_t>(cfg.threads) * cfg.linesPerThread);
        for (auto &v : threadLats)
            allLat.insert(allLat.end(), v.begin(), v.end());
        std::sort(allLat.begin(), allLat.end());

        auto pct = [&](double p) {
            return allLat[static_cast<std::size_t>(p / 100.0 * (allLat.size() - 1))];
        };
        std::cout << "\n[AsyncLogger per-call latency (ns)]\n"
                  << "  p50=" << pct(50) << "  p95=" << pct(95)
                  << "  p99=" << pct(99) << "  max=" << allLat.back() << "\n";
    }

    logger.shutdown();

    auto end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto x : checksums)
        checksum ^= x;

    const std::string logs = read_all_files(dir);

    // CAELAN_LOG_DIR overrides every FileUtil-derived logger's dir_ regardless
    // of what's passed to its constructor (see pick_log_dir()). Left set, it
    // would silently hijack every logger constructed later in this process
    // (e.g. run_async_uring's per-thread dirs) into writing here instead.
#if defined(_WIN32)
    _putenv_s("CAELAN_LOG_DIR", "");
#else
    unsetenv("CAELAN_LOG_DIR");
#endif

    BenchResult r;
    r.producerMs = std::chrono::duration<double, std::milli>(producersDone - start).count();
    r.endToEndMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.attempted = attempted;
    r.logged = count_occurrences(logs, token);
    r.dropped = count_dropped_delta(logs);
    r.checksum = checksum;
    return r;
}

// UringBackend is shared-nothing: unlike SharedBackend (one backend, one
// writer thread, all producer threads funnel through an MPSC queue), each
// thread here owns its own AsyncLogger<UringBackend> end to end, so we
// expect exactly cfg.threads files (one per thread's own subdirectory),
// not one shared file. Each thread also calls shutdown() itself before its
// AsyncLogger goes out of scope, so producer time already includes that
// thread's own flush - there's no separate "backend catches up" phase like
// there is for SharedBackend.
static BenchResult run_async_uring(const BenchConfig &cfg, const fs::path &dir, const std::string &token)
{
    reset_dir(dir);

    const std::size_t attempted =
        static_cast<std::size_t>(cfg.threads) * static_cast<std::size_t>(cfg.linesPerThread);

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> checksums(cfg.threads, 0);
    std::vector<std::vector<long long>> threadLats(cfg.threads);
    std::vector<fs::path> threadDirs(cfg.threads);

    // Each thread gets its own subdirectory: generateFileName()'s counter is
    // a per-Derived-type static, not per-instance, so concurrent UringBackend
    // instances racing on it could in principle pick the same filename.
    // Separate directories sidestep that instead of relying on the race
    // resolving cleanly.
    for (int t = 0; t < cfg.threads; ++t)
        threadDirs[t] = dir / ("thread_" + std::to_string(t));

    // SharedBackend gets 32 buffers TOTAL, shared and dynamically borrowed
    // across all threads. UringBackend is shared-nothing - each thread's pool
    // is its own and can't be borrowed from - so to keep the total memory
    // budget equal (not 8x it), each thread only gets 32 / cfg.threads
    // buffers of its own, with no cross-thread sharing possible.
    const int poolCapacityPerThread = 32 / cfg.threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < cfg.threads; ++t)
    {
        threads.emplace_back([&, t]
                             {
            AsyncLogger<UringBackend> logger(cfg.asyncBufferSize, poolCapacityPerThread, threadDirs[t].string());

            std::uint64_t local = 0;
            threadLats[t].reserve(cfg.linesPerThread);

            for (int i = 0; i < cfg.linesPerThread; ++i)
            {
                local ^= do_work(cfg.workRounds, static_cast<std::uint64_t>(t) << 32 | static_cast<std::uint64_t>(i));

                auto t0 = std::chrono::steady_clock::now();
                LOG_TO(logger, INFO) << token
                                     << " T=" << t
                                     << " I=" << i
                                     << " " << cfg.payload;
                auto t1 = std::chrono::steady_clock::now();
                threadLats[t].push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }

            logger.shutdown(); // flush + drain this thread's own backend before it's destroyed

            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto x : checksums)
        checksum ^= x;

    {
        std::vector<long long> allLat;
        allLat.reserve(static_cast<std::size_t>(cfg.threads) * cfg.linesPerThread);
        for (auto &v : threadLats)
            allLat.insert(allLat.end(), v.begin(), v.end());
        std::sort(allLat.begin(), allLat.end());

        auto pct = [&](double p) {
            return allLat[static_cast<std::size_t>(p / 100.0 * (allLat.size() - 1))];
        };
        std::cout << "\n[AsyncLogger<UringBackend> per-call latency (ns)]\n"
                  << "  p50=" << pct(50) << "  p95=" << pct(95)
                  << "  p99=" << pct(99) << "  max=" << allLat.back() << "\n";
    }

    // Sanity-check the shared-nothing expectation: one non-empty content file
    // per thread (roll() also opens one extra, empty file per thread at
    // shutdown - see UringBackend::stop() - so the raw file count is 2x this).
    std::size_t contentFileCount = 0;
    for (auto &d : threadDirs)
        for (auto &f : files_in_dir(d))
            if (fs::file_size(f) > 0)
                ++contentFileCount;
    if (contentFileCount != static_cast<std::size_t>(cfg.threads))
    {
        std::cout << "  [warn] expected " << cfg.threads
                  << " non-empty files (one per thread), found " << contentFileCount << "\n";
    }

    // A given thread's dropped-count message is written asynchronously via
    // io_uring and can complete (and thus land in the file) out of order
    // relative to other in-flight writes to that same file - it is not
    // guaranteed to be the last line. So count it by scanning every line in
    // the full concatenated content, never by position.
    std::string logs;
    for (auto &d : threadDirs)
        logs += read_all_files(d);

    BenchResult r;
    r.producerMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.endToEndMs = r.producerMs; // each thread already waits out its own backend via shutdown()
    r.attempted = attempted;
    r.logged = count_occurrences(logs, token);
    r.dropped = count_dropped_delta(logs);
    r.checksum = checksum;
    return r;
}

static BenchResult run_spdlog_async(const BenchConfig &cfg, const fs::path &dir,
                                    const std::string &token, std::size_t queueSize)
{
    reset_dir(dir);

    spdlog::init_thread_pool(queueSize, 1);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((dir / "spdlog.log").string());
    auto logger = std::make_shared<spdlog::async_logger>(
        "bench", sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
    logger->set_pattern("%v");
    logger->set_level(spdlog::level::info);

    const std::size_t attempted =
        static_cast<std::size_t>(cfg.threads) * static_cast<std::size_t>(cfg.linesPerThread);

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> checksums(cfg.threads, 0);
    std::vector<std::vector<long long>> threadLats(cfg.threads);

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < cfg.threads; ++t)
    {
        threads.emplace_back([&, t]
                             {
            std::uint64_t local = 0;
            threadLats[t].reserve(cfg.linesPerThread);

            for (int i = 0; i < cfg.linesPerThread; ++i)
            {
                local ^= do_work(cfg.workRounds,
                    static_cast<std::uint64_t>(t) << 32 | static_cast<std::uint64_t>(i));

                auto t0 = std::chrono::steady_clock::now();
                logger->info("{} T={} I={} {}", token, t, i, cfg.payload);
                auto t1 = std::chrono::steady_clock::now();
                threadLats[t].push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }
            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto producersDone = std::chrono::steady_clock::now();

    {
        std::vector<long long> allLat;
        allLat.reserve(static_cast<std::size_t>(cfg.threads) * cfg.linesPerThread);
        for (auto &v : threadLats)
            allLat.insert(allLat.end(), v.begin(), v.end());
        std::sort(allLat.begin(), allLat.end());

        auto pct = [&](double p) {
            return allLat[static_cast<std::size_t>(p / 100.0 * (allLat.size() - 1))];
        };
        std::cout << "\n[spdlog per-call latency (ns)]\n"
                  << "  p50=" << pct(50) << "  p95=" << pct(95)
                  << "  p99=" << pct(99) << "  max=" << allLat.back() << "\n";
    }

    logger->flush();
    spdlog::drop("bench");
    spdlog::shutdown();

    auto end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto x : checksums)
        checksum ^= x;

    const std::string logs = read_all_files(dir);

    BenchResult r;
    r.producerMs = std::chrono::duration<double, std::milli>(producersDone - start).count();
    r.endToEndMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.attempted = attempted;
    r.logged = count_occurrences(logs, token);
    r.dropped = attempted - r.logged;
    r.checksum = checksum;
    return r;
}

static void print_result(const std::string &name, const BenchResult &r)
{
    const double producerLinesPerSec = r.attempted / (r.producerMs / 1000.0);
    const double endToEndLinesPerSec = r.attempted / (r.endToEndMs / 1000.0);
    const double dropPct = 100.0 * static_cast<double>(r.dropped) / static_cast<double>(r.attempted);

    std::cout << "\n[" << name << "]\n";
    std::cout << "  producer time: " << std::fixed << std::setprecision(2) << r.producerMs << " ms\n";
    std::cout << "  end-to-end time: " << std::fixed << std::setprecision(2) << r.endToEndMs << " ms\n";
    std::cout << "  attempted: " << r.attempted << "\n";
    std::cout << "  logged: " << r.logged << "\n";
    std::cout << "  dropped: " << r.dropped << " / " << r.attempted
              << " (" << std::fixed << std::setprecision(1) << dropPct << "%)\n";
    std::cout << "  producer lines/sec: " << std::fixed << std::setprecision(0) << producerLinesPerSec << "\n";
    std::cout << "  end-to-end lines/sec: " << std::fixed << std::setprecision(0) << endToEndLinesPerSec << "\n";
    std::cout << "  checksum: 0x" << std::hex << r.checksum << std::dec << "\n";
}

int main()
{
    const BenchConfig cfg;

    const fs::path syncDir = "/tmp/caelan_bench_sync";
    const fs::path asyncDir = "/tmp/caelan_bench_async";
    const fs::path uringDir = "/tmp/caelan_bench_uring";

    const std::string syncToken = "<<SYNC_BENCH_TOKEN>>";
    const std::string asyncToken = "<<ASYNC_BENCH_TOKEN>>";
    const std::string uringToken = "<<URING_BENCH_TOKEN>>";

    std::cout << "Starting logger benchmark...\n";
    std::cout << "threads=" << cfg.threads
              << " lines/thread=" << cfg.linesPerThread
              << " payload=" << cfg.payload.size()
              << " workRounds=" << cfg.workRounds
              << "\n";

    const fs::path spdlogDir = "/tmp/caelan_bench_spdlog";
    const std::string spdlogToken = "<<SPDLOG_BENCH_TOKEN>>";

    // Match spdlog queue memory to CaelanLogger's total buffer pool.
    // CaelanLogger: queueSize_=32 buffers x asyncBufferSize bytes = total pool.
    // spdlog async_msg: fmt::memory_buffer (250B inline) + struct fields ≈ 400B/msg.
    const std::size_t caelMemBytes = 32 * cfg.asyncBufferSize;
    constexpr std::size_t spdlogMsgBytes = 400;
    // Round down to nearest power of 2 (required by spdlog ring buffer)
    std::size_t spdlogQueue = caelMemBytes / spdlogMsgBytes;
    spdlogQueue = spdlogQueue > 0 ? (std::size_t{1} << static_cast<std::size_t>(std::log2(spdlogQueue))) : 1;

    std::cout << "\nMemory budget:\n";
    std::cout << "  CaelanLogger: " << (caelMemBytes / 1024) << " KB  (32 x " << (cfg.asyncBufferSize / 1024) << " KB buffers)\n";
    std::cout << "  spdlog queue: ~" << (spdlogQueue * spdlogMsgBytes / 1024) << " KB  (" << spdlogQueue << " msgs x ~" << spdlogMsgBytes << " B)\n";
    std::cout << "Note: spdlog formats on producer thread; CaelanLogger defers to writer thread.\n";

    const BenchResult sync = run_sync(cfg, syncDir, syncToken);
    const BenchResult async = run_async(cfg, asyncDir, asyncToken);
    const BenchResult uring = run_async_uring(cfg, uringDir, uringToken);
    const BenchResult spd = run_spdlog_async(cfg, spdlogDir, spdlogToken, spdlogQueue);

    print_result("SyncLogger (mutex + write)", sync);
    print_result("AsyncLogger (CaelanLogger, SharedBackend)", async);
    print_result("AsyncLogger (CaelanLogger, UringBackend, shared-nothing)", uring);
    std::cout << "\n";
    print_result("spdlog async (1 thread, overrun_oldest)", spd);

    std::cout << "\nValidation:\n";
    std::cout << "  sync  logged+dropped = " << (sync.logged + sync.dropped)
              << " / " << sync.attempted << "\n";
    std::cout << "  async logged+dropped = " << (async.logged + async.dropped)
              << " / " << async.attempted << "\n";
    std::cout << "  uring logged+dropped = " << (uring.logged + uring.dropped)
              << " / " << uring.attempted << "\n";
    std::cout << "  spd   logged+dropped = " << (spd.logged + spd.dropped)
              << " / " << spd.attempted << "\n";

    std::cout << "\nLog dirs:\n";
    std::cout << "  sync:  " << syncDir << "\n";
    std::cout << "  async: " << asyncDir << "\n";
    std::cout << "  uring: " << uringDir << " (" << cfg.threads << " per-thread subdirs)\n";

    return 0;
}