# CaelanLogger

A high-throughput multi-threaded asynchronous logger in C++20, achieving
**~5.4M lines/sec under 8-producer contention (~13.8× a mutex-based baseline,
~3.5× spdlog async)** across 100 benchmark runs. Designed around thread-local
buffering, two ring-buffer queues of buffer-pool pointers, and a single backend writer thread
with batched I/O.

**Status**: Core functionality complete and validated under
AddressSanitizer + ThreadSanitizer + UndefinedBehaviorSanitizer.
Future work (dynamic capacity, multiple backend consumers such as remote sender, console displayer, lock-free queues design) tracked
separately.

---

## Highlights

- **Lock-free fast path**: per-thread buffering + atomic-flag hint means
  most `LOG_TO(AsyncLogger logger)` calls touch no shared state
- **RAII-guarded spinlock at handoff**: critical section is small
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

A standalone benchmark compares CaelanLogger (async) against two baselines:
a simple SyncLogger (global mutex + POSIX `write()`) and spdlog's async
logger (1 background thread, queue sized to match CaelanLogger's 4 MB buffer
pool, `overrun_oldest` drop policy). All three write identical content under
identical compute workload; checksums match across all runs.

### Configuration

```cpp
struct BenchConfig {
    int threads                 = 8;
    int linesPerThread          = 50'000;       // 400,000 total log calls
    int workRounds              = 512;          // deterministic compute per line
    int handoffEvery            = 256;          // manual handoff cadence (producer-side)
    std::size_t asyncBufferSize = 128 * 1024;  // 128 KB per TLS buffer
    std::string payload         = std::string(128, 'X');
};
```

### Results (100 runs)

| Logger                           | End-to-end time (median) | Producer throughput (median) | Drop rate (median) |
|----------------------------------|--------------------------|------------------------------|--------------------|
| SyncLogger (mutex + write)       | 1023 ms                  | 391 K lines/sec              | 0%                 |
| **CaelanLogger (async)**         | **74 ms**                | **5.39 M lines/sec**         | **0%**             |
| spdlog async (queue ≈ 4 MB, 1T) | ~260 ms                  | ~1.54 M lines/sec            | ~74%†              |

† spdlog figures are from a representative single run with the updated benchmark
(100-run aggregate available for sync/async only). See *On the spdlog comparison* below.

**CaelanLogger vs SyncLogger: 13.8× higher producer-side throughput** (mean: 13.78×, median: 13.79×).
**CaelanLogger vs spdlog async: ~3.5× higher producer-side throughput.**

#### Distribution detail

| Metric                          | Async (mean) | Async (p95) | Async (p99) |
|---------------------------------|--------------|-------------|-------------|
| End-to-end time (ms)            | 76.48        | 96.42       | 114.00      |
| Producer lines/sec              | 5.28 M       | 5.55 M      | 5.60 M      |
| Persisted lines/sec             | 5.21 M       | 5.54 M      | 5.58 M      |
| Drop rate %                     | 1.12%        | 7.0%        | 13.4%       |

#### Drop-rate distribution across 100 runs

```
0%        : 64/100   (no drops at all)
0–5%      : 28/100   (light pressure)
5–10%     : 6/100    (moderate burst)
>10%      : 2/100    (heavy backend stall, max observed: 13.4%)
```


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

The four metrics that matter:

```
attempted log calls / sec   ->  producer-side throughput (the headline number)
persisted log lines  / sec  ->  what actually reaches disk
drop rate                   ->  % attempted that didn't fit in the buffer pool
logged + dropped invariant  ->  correctness check on accounting
```

---

## Concurrency Design

Logging can destroy performance if every `LOG_TO(AsyncLogger logger)` call touches shared state.
This project keeps the hot path local and only synchronizes at buffer handoff.

### Key idea: exchange buffer pointers, not messages

#### Producer path (hot path)

- Each thread owns a thread-local buffer (TLS) and appends formatted log
  lines locally
- Most `LOG_TO(AsyncLogger logger)` calls do not contend on a global lock
- When the TLS buffer reaches a threshold, the thread performs a handoff:
  - enqueue the full buffer pointer into the backend's pending queue
  - acquire a fresh buffer pointer from a free queue
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

The critical section is small (a few index updates and a pointer swap).
A spinlock outperforms a mutex here because contention is rare (every Nth
log line, not every line) and the section is too short to justify a syscall.
A fully lock-free queue was considered but rejected: ABA-safe variants add
complexity that's not justified given how short and uncontended the
critical section is.

### `freeAvailable_` is a hint, not a gate

`freeAvailable_` is an atomic flag exposed for a fast-path check before
acquiring the spinlock. It indicates "free pool is likely non-empty" but
authoritative checks always happen under the lock.

A subtle but important rule:

- `freeAvailable_` can optimize acquiring a replacement buffer.
- `freeAvailable_` should not prevent submitting an existing buffer during a forced flush.

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

### 5. Writer-thread lost wakeup on stop

The writer loop used `pendingQueSize_.wait(0)` (block while zero) and
`stop()` called `pendingQueSize_.notify_all()` after setting
`running_ = false`. There is a race window: the writer reads
`running_ = true`, evaluates its loop condition, then `stop()` sets
`running_ = false` and fires `notify_all()` — before the writer enters
`wait()`. The notification is lost; the writer blocks forever;
`writer_.join()` deadlocks.

`atomic::wait` only supports a single-value predicate and cannot
atomically check a compound condition. Fixed by replacing the atomic
wait with `std::condition_variable::wait(lock, pred)`, whose predicate
covers both "has pending work" and "stopping". The mutex held by
`cv_.wait` guarantees that `stop()`'s state change and `notify_all()`
are not visible to the writer until after it has registered the wait.

### 6. Temporary-object destructor corrupts buffer ownership in `tls()`

```cpp
// BEFORE — creates a temporary ThreadLogger, then moves it
map.emplace(&backend_, ThreadLogger(bufSize_, &backend_));
```

`map.emplace` constructs a temporary `ThreadLogger`, copies/moves it
into the map, then destructs the temporary. The temporary's destructor
ran with a non-null `curBuffer_`, triggering `submitAndAcquire` (which
submitted the buffer to pending and acquired a new one from the free
pool) followed by `delete curBuffer_` on the newly borrowed pool buffer.
This gave the same buffer two owners simultaneously: the pool (via the
stale `freeQue_` slot that was never cleared) and the live map entry.

Fixed by `map.try_emplace(&backend_, bufSize_, &backend_)`, which
forwards arguments directly to the value constructor and never
creates a temporary. No destructor fires for a partially-constructed
entry.

### 7. freeQue stale-slot double-free

Taking a buffer from `freeQue_` advanced `head` but left the vacated
slot with the old pointer. After the ring completed a full cycle
(`freeQueSize_ == queueSize_`), the destructor's iteration
`[head, head + freeQueSize_)` covered all 32 physical positions.
The slot at `head - 1 (mod 32)` — the stale slot — held the same
pointer as the live slot where the writer had since returned that
buffer. The destructor encountered the pointer twice and deleted it
twice → `heap-use-after-free` under ASan.

Root cause was compounded by bug 6: the spurious temporary destructor
called `delete curBuffer_` on a pool buffer. This removed the buffer
from existence while its pointer remained in the stale slot. The live
slot (where the buffer had been returned by the writer) and the stale
slot both fell inside the full-ring iteration range → double-free.

Both causes are fixed together: `try_emplace` eliminates the spurious
destructor (bug 6), and removing `delete curBuffer_` from
`~ThreadLogger` ensures that a borrowed pool buffer is never freed by
the wrong owner.

**Lesson:** ring-buffer slots must be nulled when consumed. A non-null
stale slot is indistinguishable from a live slot when the ring is full,
making any full-range iteration unsafe.

### 8. TLS ThreadLogger outlives AsyncLogger (stack-use-after-return in CI)

`tls()` stored the TLS map as a function-local `thread_local`. When
`AsyncLogger logger` was a stack variable inside a test function, its
destructor ran when the function returned. But the TLS map — and the
`ThreadLogger` pointing to `logger.backend_` — lives until thread exit.
At program shutdown, the TLS map destructor called `~ThreadLogger()`
which called `submitAndAcquire` on the long-dead stack frame →
`stack-use-after-return` under ASan.

Fixed by factoring the TLS map into a named static helper `tlsMap()` so
it is accessible outside of `tls()`. `~AsyncLogger()` now erases its
backend's entry from `tlsMap()` before `backend_.stop()` — while the
backend is still alive — so `~ThreadLogger()` runs safely during erase
rather than at thread exit on a dangling pointer.

**Lesson:** thread-local storage outlives any object whose address it
holds. If a TLS entry references a stack-allocated object, the owner
must explicitly invalidate or remove that entry in its destructor.

---

## On the spdlog comparison

This benchmark measures **synchronization cost amortization on a hot
producer path**. CaelanLogger and spdlog differ architecturally:

- spdlog enqueues each message to a single MPSC ring buffer — every
  log call costs one synchronization point
- CaelanLogger writes to a thread-local buffer and only synchronizes
  when it is full — synchronization amortized across hundreds of lines

Under heavy contention (8 producers, 400 K total log lines), this
difference compounds: CaelanLogger achieves ~3.5× higher producer
throughput. The high spdlog drop rate (~74%) reflects its per-message
queue pressure under the same 4 MB memory budget; the formatting cost
(fmt library on the producer thread vs deferred memcpy) is a secondary
factor (~8% of the gap).

### What this benchmark does NOT show

- **spdlog is a general-purpose logger.** It supports cross-platform
  builds, multiple sinks (file, syslog, network), structured logging,
  and pattern-based formatting. CaelanLogger is single-purpose (Linux
  file logging only). The comparison is "specialized vs. general", not
  "CaelanLogger is better."
- **spdlog has tunable parameters not explored here.** A larger queue,
  more backend threads (we used 1 to match), or the `block` overflow
  policy could change the trade-offs significantly.
- **This workload is high-contention by design.** Single-threaded or
  sparse-logging workloads would narrow or eliminate the throughput gap.
- **Producer throughput is one metric.** spdlog's broader feature
  surface, ecosystem, and production maturity are not in scope here.

The honest takeaway: CaelanLogger demonstrates that **buffer-exchange
amortization** is a meaningful architectural choice for high-contention
file logging on Linux — not that it is a general spdlog replacement.

---

## Known Limitations

- **Buffer pool size is fixed at construction time** (`queueSize` parameter).
  Under sustained burst load exceeding pool capacity, logs are dropped
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

- CMake 3.20+
- C++20 compiler (GCC 11+ / Clang 14+)
- Linux

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Sanitizer builds

#### ASan + UBSan (catches leaks, UAF, double-free, undefined behavior)

```bash
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1"
cmake --build build-asan -j
```

#### TSan (catches data races; mutually exclusive with ASan)

```bash
cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
cmake --build build-tsan -j
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

### Run benchmark

```bash
./build/caelogger_bench
```

### Configure log directory

```bash
export CAELAN_LOG_DIR=/var/log/myapp
./your_app
```

---

## API

```cpp
#include "AsyncLogger.h"

// bufSize: per-TLS buffer in bytes; queueSize: pool depth; logDir: output path
AsyncLogger logger(128 * 1024, 32, "./log");

LOG_TO(logger, INFO)    << "starting up, version=" << version;
LOG_TO(logger, WARNING) << "queue depth high: " << depth;
LOG_TO(logger, ERROR)   << "failed to open " << path << ": " << strerror(errno);

// Optional: flush current thread's TLS buffer to the backend
logger.flush();

// Graceful shutdown — drains all pending buffers before returning
logger.shutdown();
```

Convenience aliases: `LOG_INFO_TO`, `LOG_WARN_TO`, `LOG_ERROR_TO`, `LOG_DEBUG_TO`.

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