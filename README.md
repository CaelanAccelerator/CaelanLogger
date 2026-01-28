# CaelanLogger — C++17 Asynchronous Logger

A high-throughput **multi-threaded async logger** built in **C++17**, optimized for low contention on the hot path.
Designed around **thread-local buffering**, **buffer-pool pointer exchange**, and a **single backend writer thread**.

- **Fast producer path** (mostly thread-local, minimal locking)
- **Batched disk I/O** (backend drains buffers and writes sequentially)
- **File rolling** support
- **GoogleTest integration tests**
- **GitHub Actions CI** with GCC/Clang + Debug/Release and a **ThreadSanitizer (TSan)** job

---

## Why this design

Logging can destroy performance if every `LOG()` call touches shared state.
This project keeps log writes local and only synchronizes when a buffer is handed off.

**Key idea:** exchange **buffer pointers**, not messages.

- Producers append to a **TLS buffer** → no global lock per log line
- When full, producers **handoff** the entire buffer to the backend and immediately acquire a new empty buffer from a pool
- Backend performs I/O in batches and returns buffers to the free pool

This turns “many log lines” into “few synchronized buffer exchanges”.

---

## Architecture

### Components
- **AsyncLogger**: singleton coordinating the system
- **ThreadLogger (TLS)**: per-thread logger holding the current writable buffer
- **BackendLogger**: dedicated writer thread + buffer pools
- **Buffer**: fixed-size byte buffer used to store formatted log lines
- **FileUtil**: POSIX `open/write` + log rolling

---

## Performance & Concurrency Notes

### Reduced lock usage
The only shared synchronization is the **buffer exchange** step (pointer moves + ring indices updates).
Everything else stays thread-local:

- **No per-message global queue push**
- **No per-message allocation**
- **No disk I/O under lock**

This design:
- amortizes synchronization cost over many log lines per buffer
- reduces cache-line bouncing compared to per-message locking
- improves I/O efficiency by batching writes

### Backpressure (current behavior)
If the backend runs out of free buffers, the system can apply a policy (e.g., drop/reset or retry).
(Upgradeable to blocking / priority channel if needed.)

## Build & Test

### Requirements
- CMake >= 3.20
- C++17 compiler (GCC/Clang)
- Linux/WSL recommended (POSIX file APIs)

### Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
