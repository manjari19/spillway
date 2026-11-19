#include "spillway/thread_pool.h"

#include <algorithm>

namespace spillway {

namespace {
// Chunks per thread: enough that a slow core (e.g. an efficiency core) only
// holds up a small piece of the pass. Chunks never get smaller than this many
// particles, so tiny passes don't drown in scheduling overhead.
constexpr size_t kChunksPerThread = 6;
constexpr size_t kMinChunk = 256;
} // namespace

int ThreadPool::hardwareThreads() {
    const unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? static_cast<int>(n) : 1;
}

ThreadPool::ThreadPool(int threads) {
    for (int i = 1; i < std::max(1, threads); ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : workers_) t.join();
}

void ThreadPool::runChunks(const Job& job) {
    // Claim chunks until none are left.
    for (;;) {
        const size_t c = nextChunk_.fetch_add(1, std::memory_order_relaxed);
        if (c >= job.chunks) return;
        const size_t begin = c * job.chunk;
        const size_t end = std::min(job.n, begin + job.chunk);
        (*job.fn)(begin, end);
        chunksLeft_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void ThreadPool::workerLoop() {
    uint64_t seen = 0;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
            // Woke too late: the job already finished and was cleared.
            if (!job_.fn) continue;
            job = job_; // a consistent snapshot, taken under the lock
            ++busyWorkers_;
        }
        runChunks(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --busyWorkers_;
        }
        done_.notify_one();
    }
}

void ThreadPool::parallelFor(size_t n, const std::function<void(size_t, size_t)>& fn) {
    if (n == 0) return;
    const size_t want = static_cast<size_t>(threads()) * kChunksPerThread;
    const size_t chunk = std::max(kMinChunk, (n + want - 1) / want);
    const size_t chunks = (n + chunk - 1) / chunk;
    if (workers_.empty() || chunks == 1) {
        for (size_t c = 0; c < chunks; ++c) fn(c * chunk, std::min(n, (c + 1) * chunk));
        return;
    }
    const Job job{&fn, n, chunk, chunks};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        nextChunk_.store(0, std::memory_order_relaxed);
        chunksLeft_.store(chunks, std::memory_order_relaxed);
        ++generation_;
    }
    wake_.notify_all();
    runChunks(job);
    // Wait until every chunk is finished and every worker is back to sleep
    // (so the next job can't race with a worker still leaving this one).
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return chunksLeft_.load(std::memory_order_acquire) == 0 && busyWorkers_ == 0; });
    job_ = {};
}

} // namespace spillway
