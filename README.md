# CaelanLogger

A high-throughput multi-threaded asynchronous logger in C++17, achieving
**~5.5M lines/sec under 8-producer contention** while preserving 99.6%
of messages — outperforming both a mutex baseline (13×) and spdlog's
async logger at the same memory budget (3.5× producer throughput,
13.8× persisted throughput).

**Status**: Stable. Validated under AddressSanitizer +
ThreadSanitizer + UndefinedBehaviorSanitizer.

For implementation details, lifecycle bug post-mortems, full benchmark
methodology, and design notes, see
[`docs/ENGINEERING.md`](docs/ENGINEERING.md).

---

## Highlights

- **Lock-free fast path**: per-thread buffering + atomic-flag hint means
  most `LOG()` calls touch no shared state
- **RAII-guarded spinlock at handoff**: critical section is ~50 cycles
  (a few index updates + pointer swap)
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
| SyncLogger (mutex + write)   | 420 K lines/sec     | 420 K lines/sec      | 0%         |
| spdlog async (overrun_oldest)| 1.55 M lines/sec    | 391 K lines/sec      | 74.6%      |
| **CaelanLogger (async)**     | **5.48 M lines/sec**| **5.38 M lines/sec** | **0.4%**   |

### Speedups (median)

| Comparison       | Producer-side | Persisted (actual disk output) |
|------------------|----------------|--------------------------------|
| Caelan vs Sync   | 13.0×          | 12.8×                          |
| Caelan vs spdlog | 3.5×           | **13.8×**                      |
| spdlog vs Sync   | 3.7×           | 0.93× (slightly slower)        |

The spdlog comparison is the meaningful one. Both async loggers are
given the same 4 MB memory budget, but Caelan keeps 99.6% of messages
while spdlog (in its default `overrun_oldest` policy) silently drops
~75% of them — making spdlog's headline producer throughput misleading
when the goal is to actually persist data.

### Drop-rate distribution

| Logger         | 0% | 0-5% | 5-25% | 25-75% | >75% |
|----------------|----|------|--------|---------|------|
| Caelan async   | 44 | 41   | 14     | 1       | 0    |
| spdlog async   | 0  | 0    | 0      | 62      | 38   |

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

    std::string payload = std::string(128, 'X');
};

// Caelan: 32 buffers × 128 KB = 4 MB pool
// spdlog: 8192-message power-of-2 ring buffer × ~400 B/msg ≈ 3.2 MB
```

> spdlog supports a `block` policy which would prevent drops but stall
> producers; this benchmark uses the default `overrun_oldest` to compare
> apples-to-apples with Caelan's non-blocking semantics.
>
> See [`docs/ENGINEERING.md`](docs/ENGINEERING.md)
> for full methodology.

---

## Concurrency Design

Logging can destroy performance if every `LOG()` call touches shared state.
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

A fully lock-free queue was considered for the buffer-exchange step but
rejected — see [`docs/ENGINEERING.md`](docs/ENGINEERING.md).

---

## API

```cpp
#include "AsyncLogger.h"

int main() {
    AsyncLogger::init(/*bufSize=*/128 * 1024,
                      /*logDir=*/"./log");

    LOG(INFO)    << "starting up, version=" << version;
    LOG(WARNING) << "queue depth high: " << depth;
    LOG(ERROR)   << "failed to open "
                 << path << ": "
                 << strerror(errno);

    // shutdown automatic at process exit
}
```

Levels: `INFO`, `DEBUG`, `WARNING`, `ERROR`, `FATAL`.

---

## Build & Run

### Requirements

- CMake 3.14+
- C++17 compiler (GCC 9+ / Clang 10+)
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

- Backend uses 100 µs polling rather than condition-variable signaling
- Buffer pool size is fixed at compile time
  (drop-on-full when exceeded; drop counter makes this observable)
- File rolling is size-based only
  (no time-based rotation)
- Linux-only
  (`POSIX write()`, `clock_gettime`, `O_APPEND`)
- No structured logging — plain text only

---

## Documentation

- [`ENGINEERING.md`](ENGINEERING.md)
  — implementation details, lifecycle bug post-mortems,
  benchmark methodology, and design notes