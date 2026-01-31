# CaelanLogger — C++17 Asynchronous Logger

A high-throughput **multi-threaded async logger** built in **C++17**, optimized for low contention on the hot path.  
Designed around **thread-local buffering**, **buffer-pool pointer exchange**, and a **single backend writer thread**.

- **Fast producer path** (thread-local buffering + **RAII-guarded spinlock** at buffer handoff; no per-log-line mutex)
- **Batched disk I/O** (backend drains buffers and writes sequentially)
- **File rolling** support
- **Deterministic log directory** (env override via `CAELAN_LOG_DIR`)
- **GoogleTest integration tests**
- **GitHub Actions CI** with GCC/Clang + Debug/Release and a **ThreadSanitizer (TSan)** job

---

## Benchmark

A standalone benchmark compares **CaelanLogger (async)** against a simple **SyncLogger** baseline
(global mutex + POSIX `write()` to a single file), logging the **same number of lines**, with the **same payload size**,
and running the **same deterministic compute workload per log line**.

### Results (8 threads, 50,000 lines/thread)

| Logger | Time (ms) | Lines/sec | Work(rounds)/sec |
|---|---:|---:|---:|
| SyncLogger (mutex + write) | 1454.44 | 275,021 | 8.80e+06 |
| CaelanLogger (async) | 126.761 | 3,155,550 | 1.01e+08 |

**Speedup:** ~11.5× higher throughput end-to-end (3.16M vs 0.275M lines/sec).  
Both runs produced the same checksum (compute workload executed identically).

> Note: `Work(rounds)/sec` is a **repeatable compute proxy** (fixed rounds of a small hash per log line),
> used to quantify “how much real work remains possible while logging”.

### Benchmark parameters
- Threads: `8`
- Lines per thread: `50,000`
- Payload per line: `256 bytes`
- Compute workload: `32 rounds` of deterministic mixing per line
- Async buffer size: `64 KB`
- Handoff frequency: every `256` log lines

---

## Concurrency design

Logging can destroy performance if every `LOG()` call touches shared state. This project keeps the hot path local and only synchronizes when a buffer is handed off.

**Key idea:** exchange **buffer pointers**, not messages.

### Producer path (hot path)
- Each thread owns a **thread-local buffer** (TLS) and appends formatted log lines locally
- Most `LOG()` calls do **not** contend on a global lock
- When the TLS buffer reaches a threshold, the thread performs a **handoff**:
  - enqueue the full buffer pointer to the backend
  - acquire a fresh buffer pointer from a free pool
  - continue logging immediately

### Backend path (cold path)
- A single backend thread **drains buffers** and writes sequentially to disk
- This batches I/O, reduces syscall frequency, and minimizes cache-line bouncing from multi-producer contention

### What is synchronized
Synchronization is concentrated at the **buffer exchange** step:
- moving buffer pointers between producer and backend
- updating ring indices / availability flags

Everything else is thread-local:
- no per-message global queue push
- no per-message allocation
- no disk I/O under lock on the producer side



