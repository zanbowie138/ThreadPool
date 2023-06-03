#pragma once
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>

// Simple thread pool for "embarrassingly parallel" problems
// Adapted from this implementation: https://stackoverflow.com/a/32593825/21568848
class ThreadPool
{
private:
    // Vector storing the threads
    std::vector<std::thread> mThreads;

    // Ensures queue is only read and modified by one thread at a time
    std::mutex queueMutex;
    // Queue of jobs
    std::queue<std::function<void()>> mJobs;

    // Stores how many threads are active
    std::atomic<int> mThreadsActive;

    // Controls thread sleeping
    std::condition_variable activateCondition;

    // Tells threads whether to terminate themselves
    bool shouldTerminate = false;

    // Either waits or runs one of the jobs added onto the queue
    void ThreadLoop();
public:
    ThreadPool() = default;

    // Runs the threads
    void Start();
    void Start(uint8_t threadCount);

    // Add job to queue
    void QueueJob(const std::function<void()>& job);

    // Returns whether the thread pool is busy or not
    bool Busy();

    // Stops threads
    // Note: Won't stop any currently running jobs
    void Clear();
};

inline void ThreadPool::ThreadLoop()
{
    while (true) {
        std::function<void()> job;
        {
            // Only one thread will be executing this scope at a time
            std::unique_lock<std::mutex> lock(queueMutex);
            // Waits until there is a job or it should terminate itself
            activateCondition.wait(lock, [this] {return !mJobs.empty() || shouldTerminate;});
            if (shouldTerminate)
                return;

            // Pop a job off the queue
            job = mJobs.front();
            mJobs.pop();

            ++mThreadsActive;
        }
        job();
        --mThreadsActive;
    }
}

inline void ThreadPool::Start()
{
    // Query for amount of CPU threads
    auto THREADS = static_cast<uint8_t>(std::thread::hardware_concurrency());
    if (THREADS == 0) // Returns 0 if the value is not well defined or not computable
    {
        // Add better handling here
        THREADS = 8;
    }

    mThreads.resize(THREADS);

    // Run threads
    for (uint8_t i = 0; i < THREADS; i++)
        mThreads.at(i) = std::thread(&ThreadPool::ThreadLoop, this);
}

inline void ThreadPool::Start(uint8_t threadCount)
{
    mThreads.resize(threadCount);

    // Run threads
    for (uint8_t i = 0; i < threadCount; i++)
        mThreads.at(i) = std::thread(&ThreadPool::ThreadLoop, this);
}

inline void ThreadPool::QueueJob(const std::function<void()>& job)
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        mJobs.push(job);
    }
    // Wake up a thread
    activateCondition.notify_one();
}

inline bool ThreadPool::Busy()
{
    bool busy;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        busy = !mJobs.empty() || mThreadsActive!=0;
    }
    return busy;
}

inline void ThreadPool::Clear()
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        shouldTerminate = true;
    }
    activateCondition.notify_all();

    for (std::thread& thread : mThreads) {
        thread.join();
    }
    mThreads.clear();
}
