#include "thread_pool.hpp"

#include <functional>
#include <mutex>

ThreadPool::ThreadPool(size_t num_threads) : _pause(false), _terminate(false), _tasks_executing(0), _num_threads(num_threads) { create_threads(); }

void ThreadPool::create_threads()
{
    for (size_t i = 0; i < _num_threads; ++i)
    {
        _threads.emplace_back([this] {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    // Wait until: terminated, or (not paused and have tasks)
                    _cv.wait(lock, [this] {
                        return _terminate.load() || (!_pause.load() && !_tasks.empty());
                    });

                    if (_terminate.load())
                    {
                        return;
                    }

                    // Double-check we have tasks (might have been woken for pause/resume)
                    if (_tasks.empty() || _pause.load())
                    {
                        continue;
                    }

                    task = std::move(_tasks.front());
                    _tasks.pop();
                    _tasks_executing.fetch_add(1);
                }
                task();
                _tasks_executing.fetch_sub(1);
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _terminate.store(true);
    }
    _cv.notify_all();

    for (auto& thread : _threads)
    {
        thread.join();
    }
}

bool ThreadPool::add_task(std::function<void()>&& task)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_terminate.load() || _pause.load())
        {
            return false;  // Don't add tasks when paused or terminated
        }
        _tasks.push(task);
    }
    _cv.notify_one();
    return true;
}

void ThreadPool::reset()
{
    pause();
    {
        std::unique_lock<std::mutex> lock(_mutex);
        while (!_tasks.empty())
        {
            _tasks.pop();
        }
    }
    resume();
}

void ThreadPool::pause()
{
    // Wait for all queued and executing tasks to complete BEFORE setting pause flag
    // This prevents the race where tasks are stuck in queue because workers see _pause=true
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_tasks.empty() && _tasks_executing.load() == 0)
            {
                _pause.store(true);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ThreadPool::resume()
{
    _pause.store(false);
    _cv.notify_all();  // Wake up workers to check for tasks
}

bool ThreadPool::is_idle() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _tasks.empty() && _tasks_executing.load() == 0;
}

bool ThreadPool::is_paused() const { return _pause.load(); }