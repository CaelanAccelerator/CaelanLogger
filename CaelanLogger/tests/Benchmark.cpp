#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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

static BenchResult run_async(const BenchConfig &cfg, const fs::path &dir, const std::string &token,
                             bool verbose = true)
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

            logger.tls()->handoff();

            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto producersDone = std::chrono::steady_clock::now();

    if (verbose)
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

    logger.shutdownAll();

    auto end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto x : checksums)
        checksum ^= x;

    const std::string logs = read_all_files(dir);

    // CAELAN_LOG_DIR overrides every FileUtil-derived logger's dir_ regardless
    // of what's passed to its constructor (see pick_log_dir()). Left set, it
    // would silently hijack every logger constructed later in this process
    // (e.g. any logger constructed later in main()) into writing here instead.
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

static BenchResult run_spdlog_async(const BenchConfig &cfg, const fs::path &dir,
                                    const std::string &token, std::size_t queueSize,
                                    bool verbose = true)
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

    if (verbose)
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

struct RunStats
{
    double mean = 0.0, stddev = 0.0, min = 0.0, max = 0.0;
};

static RunStats summarize(const std::vector<double> &v)
{
    RunStats s;
    if (v.empty())
        return s;

    s.min = *std::min_element(v.begin(), v.end());
    s.max = *std::max_element(v.begin(), v.end());

    double sum = 0.0;
    for (double x : v)
        sum += x;
    s.mean = sum / v.size();

    double sq = 0.0;
    for (double x : v)
        sq += (x - s.mean) * (x - s.mean);
    s.stddev = v.size() > 1 ? std::sqrt(sq / (v.size() - 1)) : 0.0;

    return s;
}

static void print_stats(const std::string &label, const RunStats &s, const std::string &unit)
{
    std::cout << "  " << std::left << std::setw(22) << label << std::right
              << "mean=" << std::fixed << std::setprecision(2) << std::setw(12) << s.mean << unit
              << "  stddev=" << std::setw(10) << s.stddev << unit
              << "  min=" << std::setw(12) << s.min << unit
              << "  max=" << std::setw(12) << s.max << unit << "\n";
}

// Runs `fn` `runs` times, resetting its own log dir each time, and prints
// mean/stddev/min/max across the runs instead of one noisy line per run.
static void run_many(const std::string &name, int runs, const std::function<BenchResult()> &fn)
{
    std::vector<double> producerMs, endToEndMs, dropPct, producerLps, endToEndLps;
    std::size_t attempted = 0;

    for (int i = 0; i < runs; ++i)
    {
        BenchResult r = fn();
        attempted = r.attempted;

        if (r.logged + r.dropped != r.attempted)
        {
            std::cout << "  [warn] " << name << " run " << i << ": logged+dropped("
                      << (r.logged + r.dropped) << ") != attempted(" << r.attempted << ")\n";
        }

        producerMs.push_back(r.producerMs);
        endToEndMs.push_back(r.endToEndMs);
        dropPct.push_back(100.0 * static_cast<double>(r.dropped) / static_cast<double>(r.attempted));
        producerLps.push_back(r.attempted / (r.producerMs / 1000.0));
        endToEndLps.push_back(r.attempted / (r.endToEndMs / 1000.0));
    }

    std::cout << "\n[" << name << "]  (n=" << runs << ", attempted/run=" << attempted << ")\n";
    print_stats("producer time", summarize(producerMs), " ms");
    print_stats("end-to-end time", summarize(endToEndMs), " ms");
    print_stats("dropped", summarize(dropPct), " %");
    print_stats("producer lines/sec", summarize(producerLps), "");
    print_stats("end-to-end lines/sec", summarize(endToEndLps), "");
}

int main()
{
    const BenchConfig cfg;
    constexpr int kRuns = 20;

    const fs::path syncDir = "/tmp/caelan_bench_sync";
    const fs::path asyncDir = "/tmp/caelan_bench_async";
    const fs::path spdlogDir = "/tmp/caelan_bench_spdlog";

    const std::string syncToken = "<<SYNC_BENCH_TOKEN>>";
    const std::string asyncToken = "<<ASYNC_BENCH_TOKEN>>";
    const std::string spdlogToken = "<<SPDLOG_BENCH_TOKEN>>";

    std::cout << "Starting logger benchmark (n=" << kRuns << " runs per scenario)...\n";
    std::cout << "threads=" << cfg.threads
              << " lines/thread=" << cfg.linesPerThread
              << " payload=" << cfg.payload.size()
              << " workRounds=" << cfg.workRounds
              << "\n";

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
    std::cout << "Note: spdlog pattern is \"%v\" (no timestamp/level added); CaelanLogger always\n"
                 "adds both on the producer thread, so it does strictly more work per call.\n";

    run_many("SyncLogger (mutex + write)", kRuns,
             [&] { return run_sync(cfg, syncDir, syncToken); });
    run_many("AsyncLogger (CaelanLogger, SharedBackend)", kRuns,
             [&] { return run_async(cfg, asyncDir, asyncToken, /*verbose=*/false); });
    run_many("spdlog async (1 thread, overrun_oldest)", kRuns,
             [&] { return run_spdlog_async(cfg, spdlogDir, spdlogToken, spdlogQueue, /*verbose=*/false); });

    std::cout << "\nLog dirs (last run's output only):\n";
    std::cout << "  sync:  " << syncDir << "\n";
    std::cout << "  async: " << asyncDir << "\n";
    std::cout << "  spdlog:" << spdlogDir << "\n";

    return 0;
}