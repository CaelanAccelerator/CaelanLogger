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

namespace fs = std::filesystem;

struct BenchConfig
{
    int threads = 8;
    int linesPerThread = 50'000;
    int workRounds = 512;
    int handoffEvery = 256;
    std::size_t asyncBufferSize = 128 * 1024;
    std::string payload = std::string(128, 'X');
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

// FileUtil::roll() writes dropped delta and resets it.
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

    AsyncLogger::init(cfg.asyncBufferSize);

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

                LOG(INFO) << token
                          << " T=" << t
                          << " I=" << i
                          << " " << cfg.payload;

                // if ((i + 1) % cfg.handoffEvery == 0)
                //     AsyncLogger::getInstance().tls().handoff();
            }

            // Important: final flush must force submit this thread's TLS buffer.
            AsyncLogger::getInstance().tls().handoff(true);

            checksums[t] = local; });
    }

    for (auto &th : threads)
        th.join();

    auto producersDone = std::chrono::steady_clock::now();

    AsyncLogger::getInstance().shutdown();

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
    r.dropped = count_dropped_delta(logs);
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

    const std::string syncToken = "<<SYNC_BENCH_TOKEN>>";
    const std::string asyncToken = "<<ASYNC_BENCH_TOKEN>>";

    std::cout << "Starting logger benchmark...\n";
    std::cout << "threads=" << cfg.threads
              << " lines/thread=" << cfg.linesPerThread
              << " payload=" << cfg.payload.size()
              << " workRounds=" << cfg.workRounds
              << "\n";

    const BenchResult sync = run_sync(cfg, syncDir, syncToken);
    const BenchResult async = run_async(cfg, asyncDir, asyncToken);

    print_result("SyncLogger (mutex + write)", sync);
    print_result("AsyncLogger (your logger)", async);

    std::cout << "\nValidation:\n";
    std::cout << "  sync logged + dropped = " << (sync.logged + sync.dropped)
              << " / " << sync.attempted << "\n";
    std::cout << "  async logged + dropped = " << (async.logged + async.dropped)
              << " / " << async.attempted << "\n";

    std::cout << "\nLog dirs:\n";
    std::cout << "  sync:  " << syncDir << "\n";
    std::cout << "  async: " << asyncDir << "\n";

    return 0;
}