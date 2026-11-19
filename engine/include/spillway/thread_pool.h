#pragma once

// A small persistent thread pool for the simulation's per-particle passes.
//
// parallelFor(n, fn) splits [0, n) into fixed contiguous chunks and calls
// fn(begin, end) once per chunk; the calling thread works too, and the call
// returns when every chunk is done. Chunk boundaries depend only on n and the
// pool's chunk count, never on timing. Which thread runs a chunk is decided
// at run time (an atomic counter hands them out, so fast cores take more and
// slow cores fewer), but every pass writes only its own particles' outputs,
// so the results are bit-identical for any number of threads.
//
// Workers sleep on a condition variable between calls; they are created once
// and live as long as the pool.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace spillway {

class ThreadPool {
public:
    // `threads` counts the calling thread too: 1 = run everything inline.
    explicit ThreadPool(int threads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    int threads() const { return static_cast<int>(workers_.size()) + 1; }

    void parallelFor(size_t n, const std::function<void(size_t, size_t)>& fn);

    // Hardware threads, at least 1.
    static int hardwareThreads();

private:
    struct Job {
        const std::function<void(size_t, size_t)>* fn = nullptr;
        size_t n = 0, chunk = 0, chunks = 0;
    };
    void workerLoop();
    void runChunks(const Job& job);

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    uint64_t generation_ = 0;
    bool stop_ = false;

    Job job_; // the current job (fn == nullptr: none); guarded by mutex_
    std::atomic<size_t> nextChunk_{0};
    std::atomic<size_t> chunksLeft_{0};
    int busyWorkers_ = 0;
};

} // namespace spillway
