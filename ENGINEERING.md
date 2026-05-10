# CaelanLogger

A high-throughput multi-threaded asynchronous logger in C++17, achieving
**~5.4M lines/sec under 8-producer contention (~13.8× a mutex-based baseline)**
across 100 benchmark runs. Designed around thread-local buffering, buffer-pool
pointer exchange, and a single backend writer thread with batched I/O.

**Status**: Stable. Core functionality complete and validated under
AddressSanitizer + ThreadSanitizer + UndefinedBehaviorSanitizer.
Future work (drop counter persistence formats, dynamic capacity) tracked
separately.

---

## Highlights

- **Lock-free fast path**: per-thread buffering + atomic-flag hint means
  most `LOG()` calls touch no shared state
- **RAII-guarded spinlock at handoff**: critical section is ~50 cycles
  (a few index updates + pointer swap)
- **Bounded buffer pool with drop-on-full**: producers never block;
  every dropped log is counted, and `logged + dropped == attempted` is
  enforced as a correctness invariant — verified across **100/100**
  benchmark runs
- **Batched disk I/O**: backend drains all pending buffers per write cycle,
  amortizing syscall cost
- **Size-based file rolling** with deterministic log directory
  (env override via `CAELAN_LOG_DIR`)
- **GoogleTest** integration tests + standalone benchmark
- **GitHub Actions CI**: GCC/Clang × Debug/Release × {ASan+UBSan, TSan}
  (sanitizers run as separate jobs since they are mutually exclusive at runtime)

---

## Benchmark

A standalone benchmark compares CaelanLogger (async) against a simple
SyncLogger baseline (global mutex + POSIX `write()` to a single file).
Both write identical content with identical compute workload per line;
checksums match across all runs.

### Configuration

```cppstruct BenchConfig {
int threads          = 8;
int linesPerThread   = 50'000;        // 400,000 total log calls
int workRounds       = 512;           // deterministic compute per line
int handoffEvery     = 256;           // manual handoff cadence (producer-side)
std::size_t asyncBufferSize = 128 * 1024;  // 128 KB per TLS buffer
std::string payload  = std::string(128, 'X');
};

### Results (100 runs)

| Logger                           | End-to-end time (median) | Producer throughput (median) | Drop rate (median) |
|----------------------------------|--------------------------|------------------------------|--------------------|
| SyncLogger (mutex + write)       | 1023 ms                  | 391 K lines/sec              | 0%                 |
| **CaelanLogger (async)**         | **74 ms**                | **5.39 M lines/sec**         | **0%**             |

**Speedup: 13.8× higher producer-side throughput** (mean: 13.78×, median: 13.79×).

#### Distribution detail

| Metric                          | Async (mean) | Async (p95) | Async (p99) |
|---------------------------------|--------------|-------------|-------------|
| End-to-end time (ms)            | 76.48        | 96.42       | 114.00      |
| Producer lines/sec              | 5.28 M       | 5.55 M      | 5.60 M      |
| Persisted lines/sec             | 5.21 M       | 5.54 M      | 5.58 M      |
| Drop rate %                     | 1.12%        | 7.0%        | 13.4%       |

#### Drop-rate distribution across 100 runs0%        : 64/100   (no drops at all)
0-5%      : 28/100   (light pressure)
5-10%     : 6/100    (moderate burst)

10%      : 2/100    (heavy backend stall, max observed: 13.4%)


**Invariant verification**: `logged + dropped == attempted` holds for
**100/100 runs**. The bounded-buffer drop policy is correctly accounted
for — no logs are silently lost.

### Benchmark validity notes

- **Both loggers wrote identical content** under identical compute workload;
  output checksums match across all 100 runs.
- **SyncLogger is the worst-case baseline** (single global mutex around
  `write()`). Optimized synchronous loggers like spdlog-sync would narrow
  the gap considerably.
- **Throughput is producer-side, not persisted-throughput.** Under the
  drop-on-full policy, persisted lines/sec is reported separately (median
  5.33 M vs producer 5.39 M — drops account for the gap).
- **Speedup reflects 8-producer contention.** Single-threaded gap is much
  smaller — the design specifically targets multi-producer scenarios where
  a single mutex becomes the bottleneck.
- **Outlier behavior**: 8 of 100 runs showed drop rates above 5%, likely
  due to backend-thread scheduling jitter on a contended machine. Even
  in the worst case (13.4% drop), the invariant held and persisted output
  was complete with respect to what was logged.
- **Latency per call not measured.** Benchmark targets aggregate throughput.

### What the benchmark measures

> Maximum producer-side throughput under bounded-buffer drop policy,
> with `logged + dropped == attempted` verified per run.

The four metrics that matter:attempted log calls / sec    -> producer-side throughput (the headline number)
persisted log lines  / sec   -> what actually reaches disk
drop rate                    -> % attempted that didn't fit in the buffer pool
logged + dropped invariant   -> correctness check on accounting

---

## Concurrency Design

Logging can destroy performance if every `LOG()` call touches shared state.
This project keeps the hot path local and only synchronizes at buffer handoff.

### Key idea: exchange buffer pointers, not messages

#### Producer path (hot path)

- Each thread owns a thread-local buffer (TLS) and appends formatted log
  lines locally
- Most `LOG()` calls do not contend on a global lock
- When the TLS buffer reaches a threshold, the thread performs a handoff:
  - enqueue the full buffer pointer into the backend's pending queue
  - acquire a fresh buffer pointer from a free pool
  - continue logging immediately

#### Backend path (cold path)

- A single backend thread drains all pending buffers per cycle and writes
  sequentially to disk
- This batches I/O, reduces syscall frequency, and minimizes cache-line
  bouncing from multi-producer contention

### What is synchronized

Synchronization is concentrated at the buffer exchange step:

- moving buffer pointers between producer and backend
- updating ring indices / availability flags

The critical section is ~50 cycles (a few index updates and a pointer swap).
A spinlock outperforms a mutex here because contention is rare (every Nth
log line, not every line) and the section is too short to justify a syscall.
A fully lock-free queue was considered but rejected: ABA-safe variants add
complexity that's not justified given how short and uncontended the
critical section is.

### `freeAvailable_` is a hint, not a gate

`freeAvailable_` is an atomic flag exposed for a fast-path check before
acquiring the spinlock. It indicates "free pool is likely non-empty" but
authoritative checks always happen under the lock.

A subtle but important rule:freeAvailable_ can optimize acquiring a replacement buffer.
freeAvailable_ should not prevent submitting an existing buffer
during forced flush.

Mixing these two roles caused a real bug — submission could be skipped
during shutdown because the flag was false, even though pendingQue still
had room. Fixed by checking the flag only on the acquire path, not the
submit path.

### What happens when the free pool is empty

When the bounded buffer pool is exhausted, a producer's `cur_buffer` is set
to `nullptr` after submission. Because the architecture is single-direction
polling (backend cannot notify producers), recovery is producer-driven:

- on the next `handoff()`, if `cur_buffer == nullptr`, take the recovery
  path (call `get_free_buffer()` directly — no submit needed)
- if recovery succeeds, normal logging resumes
- if recovery fails (pool still empty), the next `LOG()` calls increment
  the drop counter and return without writing

Recovery must store the new buffer back into `ThreadLogger::curBuffer_`,
not into a `LogStream` local — `LogStream` is temporary and would otherwise
leave the recovered buffer outside the free/pending/TLS state machine.

---

## Engineering Notes

The buffer-pool design went through several iterations to reach correctness.
**AddressSanitizer caught four classes of lifecycle bugs that ThreadSanitizer
alone missed** — the original CI had only TSan, so these slipped through
until ASan was added and a dedicated lifecycle test was written.

### 1. Destructor leak

`BackendLogger` allocated buffers in the constructor but never released
them in the destructor — the original dtor only joined the writer thread.

This was hidden by the singleton lifetime in production tests
(`AsyncLogger` exits with the process, OS reclaims memory). Surfaced only
by an explicit `{ BackendLogger bl(...); bl.start(); bl.stop(); }` lifecycle
test. **Lesson: singletons hide dtor paths from standard tests.**

### 2. Ring-buffer cross-array double-free

The first dtor fix iterated `[0, QUEUE_SIZE)`, but stale slots in
`pendingQue` could still hold pointers to buffers that had since been
re-acquired in `freeQue`. Iterating the full array caused cross-array
double-delete and `heap-use-after-free` under ASan.

Fixed by iterating only the active range `[front, front + size)` for each
ring buffer.

### 3. Incomplete `restart()` reset

Only `freeQueBack` and `freeQueSize` were reset; `freeQueFront`, plus all
`pendingQue` indices, were not. After restart, ownership tracking was
corrupted — the same buffer could appear in two slots, or a TLS-held
buffer could be silently leaked when `restart()` rebuilt the free queue
as if all buffers were available.

Fixed by:
- resetting all six index/size fields
- compacting the live free buffers to `[0, size)` and nulling the rest
- only running `restart()` after `stop()` and after all TLS buffers have
  been accounted for

### 4. Buffer-pool exhaustion liveness bug

When the free pool was empty, `cur_buffer` was set to `nullptr` with no
recovery path. Because the architecture is single-direction polling, the
backend cannot notify producers when buffers free up — the producer would
silently drop all subsequent logs.

Fixed by adding a `get_free_buffer()` interface and a recovery branch in
`handoff()` that runs when `cur_buffer == nullptr`. This preserves the
single-direction ownership flow while letting producers recover.

### Common root cause

> Ownership state is distributed across `front` / `back` / `size` indices.
> Every entry point — dtor, restart, acquire, release — must maintain the
> full invariant. Any partial update breaks ownership tracking, which then
> manifests as **leak**, **UAF**, or **liveness loss** depending on which
> path is taken next.

### Drop accounting bug (separate class)

A subtle C++ shadowing bug in `LogStream::~LogStream()`: assigning
`target = nullptr` modified the *constructor parameter* rather than the
*member variable*, so the destructor could count the same dropped log
twice. Fixed by using `this->target_ = nullptr`.

This was caught by the integration tests, which now verify
`logged + dropped == attempted` directly by scanning the log directory
and summing `dropped: N` delta records.

---

## Known Limitations

- **Backend uses 100 µs polling** rather than condition-variable signaling.
  Trades up to ~100 µs worst-case latency for simpler shutdown semantics
  and bounded scheduler wakeup behavior.
- **Buffer pool size is fixed at compile time** (`QUEUE_SIZE`). Under
  sustained burst load exceeding pool capacity, logs are dropped
  (drop-on-full). The drop counter makes this observable.
- **File rolling is size-based only** (no time-based rotation).
- **Linux-only**: uses `clock_gettime`, POSIX `write()`, and `O_APPEND`
  in the file path.
- **No structured logging**: lines are plain text with level + timestamp +
  user payload. Adding structured fields (JSON, key-value) would require
  changes to `LogStream`.

---

## Build & Run

### Requirements

- CMake 3.14+
- C++17 compiler (GCC 9+ / Clang 10+)
- Linux

### Build

```bashcmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

### Sanitizer builds

```bashASan + UBSan (catches leaks, UAF, double-free, undefined behavior)
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1"
cmake --build build-asan -jTSan (catches data races; mutually exclusive with ASan)
cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
cmake --build build-tsan -j

### Run tests

```bashcd build && ctest --output-on-failure

### Run benchmark

```bash./build/caelogger_bench

### Configure log directory

```bashexport CAELAN_LOG_DIR=/var/log/myapp
./your_app

---

## API

```cpp#include "AsyncLogger.h"int main() {
AsyncLogger::init(/bufSize=/128 * 1024, /logDir=/"./log");LOG(INFO) << "starting up, version=" << version;
LOG(WARNING) << "queue depth high: " << depth;
LOG(ERROR) << "failed to open " << path << ": " << strerror(errno);// shutdown automatic at process exit
}

Levels: `INFO`, `DEBUG`, `WARNING`, `ERROR`, `FATAL`.

---

## CI

GitHub Actions runs the following matrix on every push:

- **Build matrix**: GCC × {Debug, Release}, Clang × {Debug, Release}
- **Sanitizer jobs** (separate builds, mutually exclusive at runtime):
  - ASan + UBSan: catches memory errors and undefined behavior
  - TSan: catches data races and improper synchronization

Both sanitizer jobs run the full test suite and benchmark to validate
correctness under heavy concurrency.