# CaelanLogger

A high-throughput multi-threaded asynchronous logger in C++17, achieving
**~5.4M lines/sec under 8-producer contention (~13.8× a mutex-based baseline)**
across 100 benchmark runs. Designed around thread-local buffering, buffer-pool
pointer exchange, and a single backend writer thread with batched I/O.

**Status**: Stable. Validated under AddressSanitizer + ThreadSanitizer +
UndefinedBehaviorSanitizer.

For implementation details, lifecycle bug post-mortems, full benchmark
methodology, and design notes, see [`docs/ENGINEERING.md`](docs/ENGINEERING.md).

---

## Highlights

- **Lock-free fast path**: per-thread buffering + atomic-flag hint means
  most `LOG()` calls touch no shared state
- **RAII-guarded spinlock at handoff**: critical section is ~50 cycles
  (a few index updates + pointer swap)
- **Bounded buffer pool with drop-on-full**: producers never block; every
  dropped log is counted, and `logged + dropped == attempted` is enforced
  as a correctness invariant — verified across **100/100** benchmark runs
- **Batched disk I/O**: backend drains all pending buffers per write cycle
- **Size-based file rolling** with deterministic log directory
  (env override via `CAELAN_LOG_DIR`)
- **GitHub Actions CI**: GCC/Clang × Debug/Release × {ASan+UBSan, TSan}
  (sanitizers run as separate jobs since they are mutually exclusive at runtime)

---

## Benchmark

100 runs comparing CaelanLogger (async) against a SyncLogger baseline
(global mutex + POSIX `write()`). Both write identical content with
identical compute workload per line; checksums match across all runs.

| Logger                        | End-to-end (median) | Producer throughput (median) | Drop rate (median) |
|-------------------------------|---------------------|------------------------------|--------------------|
| SyncLogger (mutex + write)    | 1023 ms             | 391 K lines/sec              | 0%                 |
| **CaelanLogger (async)**      | **74 ms**           | **5.39 M lines/sec**         | **0%**             |

**Speedup: 13.8× higher producer-side throughput** (mean 13.78×, median 13.79×).

`logged + dropped == attempted` holds for **100/100 runs** — the
bounded-buffer drop policy is correctly accounted for; no logs are
silently lost.

> SyncLogger is the worst-case mutex baseline, not an optimized sync
> logger like spdlog-sync. See [`docs/ENGINEERING.md`](docs/ENGINEERING.md)
> for full methodology, distributions, and validity notes.

### Configuration

```cpp
struct BenchConfig {
    int threads          = 8;
    int linesPerThread   = 50'000;        // 400,000 total log calls
    int workRounds       = 512;           // deterministic compute per line
    int handoffEvery     = 256;           // manual handoff cadence
    std::size_t asyncBufferSize = 128 * 1024;  // 128 KB per TLS buffer
    std::string payload  = std::string(128, 'X');
};
```

---

## Concurrency Design

Logging can destroy performance if every `LOG()` call touches shared state.
This project keeps the hot path local and only synchronizes at buffer handoff.

**Key idea: exchange buffer pointers, not messages.**

### Producer path (hot path)

- Each thread owns a thread-local buffer (TLS) and appends formatted log
  lines locally — no global lock contention on most calls
- When the TLS buffer fills, the producer performs a handoff:
  - enqueue the full buffer pointer into the backend's pending queue
  - acquire a fresh buffer pointer from the free pool
  - continue logging immediately

### Backend path (cold path)

- A single backend thread drains all pending buffers per cycle and writes
  sequentially to disk — batching I/O, reducing syscall frequency, and
  minimizing cache-line bouncing from multi-producer contention

### What is synchronized

Synchronization is concentrated at the buffer exchange step (moving
buffer pointers between producer and backend, updating ring indices).
The critical section is short enough that a spinlock outperforms a mutex.
A fully lock-free queue was considered but rejected — see
[`docs/ENGINEERING.md`](docs/ENGINEERING.md) for the trade-off analysis.

---

## API

```cpp
#include "AsyncLogger.h"

int main() {
    AsyncLogger::init(/*bufSize=*/128 * 1024, /*logDir=*/"./log");

    LOG(INFO)    << "starting up, version=" << version;
    LOG(WARNING) << "queue depth high: " << depth;
    LOG(ERROR)   << "failed to open " << path << ": " << strerror(errno);
    // shutdown automatic at process exit
}
```

Levels: `INFO`, `DEBUG`, `WARNING`, `ERROR`, `FATAL`.

---

## Build & Run

### Requirements

- CMake 3.14+, C++17 compiler (GCC 9+ / Clang 10+), Linux

### Build & test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

### Sanitizer builds

```bash
# ASan + UBSan (memory errors, undefined behavior)
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1"
cmake --build build-asan -j

# TSan (data races; mutually exclusive with ASan at runtime)
cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
cmake --build build-tsan -j
```

### Run benchmark

```bash
./build/caelogger_bench
```

### Configure log directory

```bash
export CAELAN_LOG_DIR=/var/log/myapp
```

---

## Known Limitations

- Backend uses 100 µs polling rather than condition-variable signaling
- Buffer pool size is fixed at compile time (drop-on-full when exceeded;
  drop counter makes this observable)
- File rolling is size-based only (no time-based rotation)
- Linux-only (POSIX `write()`, `clock_gettime`, `O_APPEND`)
- No structured logging — plain text only

---

## Documentation

- [`docs/ENGINEERING.md`](docs/ENGINEERING.md) — implementation details,
  lifecycle bug post-mortems, benchmark methodology, design notes