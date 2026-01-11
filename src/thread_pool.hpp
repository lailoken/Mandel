#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

class ThreadPool
{
public:
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    bool add_task(std::function<void()>&& task);
    void reset();      // Stop processing, clear queue, wait for current tasks to complete
    void start();      // Start/resume the thread pool
    bool is_idle() const;  // Check if all tasks have completed (queue empty and no tasks executing)
    bool is_running() const;  // Check if thread pool is running (not stopped)
private:
    void create_threads();
    std::vector<std::thread> _threads;
    std::queue<std::function<void()>> _tasks;
    mutable std::mutex _mutex;  // Mutable to allow locking in const methods like is_idle()
    std::condition_variable _condition;
    std::condition_variable _stopped_condition;
    std::atomic<bool> _stop;
    std::atomic<bool> _destroying;
    std::atomic<size_t> _tasks_executing;
    size_t _num_threads;
};