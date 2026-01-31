#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    bool add_task(std::function<void()>&& task);
    void pause();
    void resume();
    void reset();          // Stop processing, clear queue, wait for current tasks to complete
    bool is_idle() const;  // Check if all tasks have completed (queue empty and no tasks executing)
    bool is_paused() const;  // Check if the thread pool is paused
 private:
    void create_threads();
    std::vector<std::thread> _threads;
    std::queue<std::function<void()>> _tasks;
    mutable std::mutex _mutex;  // Mutable to allow locking in const methods like is_idle()
    std::condition_variable _cv;  // For efficient waiting instead of busy-spinning
    std::atomic<bool> _pause;
    std::atomic<bool> _terminate;
    std::atomic<size_t> _tasks_executing;
    size_t _num_threads;
};