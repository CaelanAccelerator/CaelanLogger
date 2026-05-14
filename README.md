# CaelanLogger

A high-throughput multi-threaded asynchronous logger in C++20, achieving
**~5.3M lines/sec under 8-producer contention** while preserving 98.2%
of messages — outperforming both a mutex baseline (14.1×) and spdlog's
async logger at the same memory budget (3.7× producer throughput,
15.0× persisted throughput).

**Status**: Stable. Validated under AddressSanitizer +
ThreadSanitizer + UndefinedBehaviorSanitizer.

For implementation details, lifecycle bug post-mortems, full benchmark
methodology, and design notes, see
[`ENGINEERING.md`](ENGINEERING.md).

---

## Highlights

- **Lock-free fast path**: per-thread buffering + atomic-flag hint means
  most `LOG_TO()` calls touch no shared state
- **Two independent spinlocks**: pending queue and free queue each have
  their own lock — writers returning buffers no longer block producers
  submitting new ones
- **Event-driven writer**: condition variable replaces the 100 µs polling
  loop; latency from submission to disk is now sub-millisecond
- **Bounded buffer pool with drop-on-full**: producers never block;
  every dropped log is counted, and
  `logged + dropped == attempted` is enforced as a correctness invariant
  — verified across **100/100** benchmark runs
- **Batched disk I/O**: backend drains all pending buffers per write cycle
- **Size-based file rolling** with deterministic log directory
  (env override via `CAELAN_LOG_DIR`)
- **GitHub Actions CI**:
  GCC/Clang × Debug/Release × {ASan+UBSan, TSan}

---

## Benchmark

100 runs, three loggers compared at a matched 4 MB memory budget:

- **SyncLogger**:
  global mutex + POSIX `write()` (worst-case baseline)
- **spdlog async**:
  `overrun_oldest` (industry-standard async policy)
- **CaelanLogger**:
  drop-on-full + counter

All three write identical content with identical compute workload per
line; checksums match across all runs.

### Results (median of 100 runs)

| Logger                        | Producer throughput | Persisted throughput | Drop rate |
|------------------------------|---------------------|----------------------|------------|
| SyncLogger (mutex + write)   | 379 K lines/sec     | 379 K lines/sec      | 0%         |
| spdlog async (overrun_oldest)| 1.46 M lines/sec    | 350 K lines/sec      | 75.8%      |
| **CaelanLogger (async)**     | **5.33 M lines/sec**| **5.25 M lines/sec** | **0%**     |

### Speedups (median)

| Comparison       | Producer-side | Persisted (actual disk output) |
|------------------|----------------|--------------------------------|
| Caelan vs Sync   | 14.1×          | 13.9×                          |
| Caelan vs spdlog | 3.7×           | **15.0×**                      |
| spdlog vs Sync   | 3.8×           | 0.92× (slightly slower)        |

The spdlog comparison is the meaningful one. Both async loggers are
given the same 4 MB memory budget, but Caelan keeps 98.2% of messages
(median: 100%) while spdlog (in its default `overrun_oldest` policy)
silently drops ~76% of them — making spdlog's headline producer
throughput misleading when the goal is to actually persist data.

### Drop-rate distribution (100 runs)

| Logger         | 0%  | 0–5% | 5–25% | 25–75% | >75% |
|----------------|-----|------|-------|--------|------|
| Caelan async   | 52  | 37   | 10    | 1      | 0    |
| spdlog async   | 0   | 0    | 0     | 100    | 0    |

`logged + dropped == attempted` holds for
**100/100 Caelan runs** — the bounded-buffer drop policy is correctly
accounted for.

### Configuration

```cpp
struct BenchConfig {
    int threads                = 8;
    int linesPerThread         = 50'000;        // 400,000 total log calls
    int workRounds             = 512;           // deterministic compute per line
    int handoffEvery           = 256;
    std::size_t asyncBufferSize = 128 * 1024;  // 128 KB per TLS buffer
    std::string payload        = std::string(256, 'X');
};

// Caelan: 32 buffers × 128 KB = 4 MB pool
// spdlog: queue size auto-matched to ~4 MB (≈ 8192 msgs × ~400 B/msg)
```

> spdlog supports a `block` policy which would prevent drops but stall
> producers; this benchmark uses the default `overrun_oldest` to compare
> apples-to-apples with Caelan's non-blocking semantics.
>
> See [`ENGINEERING.md`](ENGINEERING.md)
> for full methodology.

---

## Concurrency Design

Logging can destroy performance if every `LOG_TO()` call touches shared state.
This project keeps the hot path local and only synchronizes at buffer handoff.

### Key idea: exchange buffer pointers, not messages

#### Producer path (hot path)

- Each thread owns a thread-local buffer (TLS) and appends formatted log
  lines locally — no global lock contention on most calls
- When the TLS buffer fills, the producer performs a handoff:
  - enqueue the full buffer pointer into the backend's pending queue
  - acquire a fresh buffer pointer from the free pool
  - continue logging immediately

#### Backend path (cold path)

- A single backend thread drains all pending buffers per cycle and writes
  sequentially to disk — batching I/O, reducing syscall frequency, and
  minimizing cache-line bouncing from multi-producer contention

### Why this beats per-message MPSC queues

spdlog (and most async loggers) use a per-message MPSC ring buffer:
every log call requires synchronization on a shared queue.

Caelan amortizes synchronization cost across hundreds of log lines per
buffer handoff, which is why it achieves 3.5× higher producer throughput
with the same memory budget.

Two independent spinlocks guard the pending and free queues separately,
so the writer returning buffers to the free pool no longer contends with
producers submitting new ones. A fully lock-free queue was also
considered but rejected — see [`ENGINEERING.md`](ENGINEERING.md).

---

## API

`AsyncLogger` is a regular object — create one instance per log
destination, pass it to any thread via reference.

```cpp
#include "AsyncLogger.h"

// bufSize: per-TLS buffer in bytes; queueSize: pool depth; logDir: output path
AsyncLogger logger(128 * 1024, 32, "./log");

// Logging — use the macro, pass the logger by reference
LOG_TO(logger, INFO)    << "starting up, version=" << version;
LOG_TO(logger, WARNING) << "queue depth high: " << depth;
LOG_TO(logger, ERROR)   << "failed to open " << path;

// Optional: flush current thread's TLS buffer to the backend
// (non-blocking; data reaches disk within the next writer cycle)
logger.flush();

// Graceful shutdown — drains all pending buffers before returning
logger.shutdown();
```

Convenience aliases: `LOG_INFO_TO`, `LOG_WARN_TO`, `LOG_ERROR_TO`, `LOG_DEBUG_TO`.

Levels: `INFO`, `DEBUG`, `WARNING`, `ERROR`, `FATAL`.

---

## Build & Run

### Requirements

- CMake 3.20+
- C++20 compiler (GCC 11+ / Clang 14+)
- Linux

### Build & test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

cd build && ctest --output-on-failure
```

### Sanitizer builds

#### ASan + UBSan

```bash
cmake -B build-asan \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1"

cmake --build build-asan -j
```

#### TSan

```bash
cmake -B build-tsan \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"

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

- Buffer pool size is fixed at construction time
  (drop-on-full when exceeded; drop counter makes this observable)
- File rolling is size-based only
  (no time-based rotation)
- Linux-only
  (`POSIX write()`, `clock_gettime`, `O_APPEND`)
- No structured logging — plain text only
- `flush()` is non-blocking: guarantees submission to the backend queue
  but not persistence to disk; use `shutdown()` for a full drain

---

## Documentation

- [`ENGINEERING.md`](ENGINEERING.md)
  — implementation details, lifecycle bug post-mortems,
  benchmark methodology, and design notes