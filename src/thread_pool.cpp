#include "thread_pool.hpp"
#include <mutex>
#include <condition_variable>
#include <functional>


ThreadPool::ThreadPool(size_t num_threads) : _stop(false), _destroying(false), _tasks_executing(0), _num_threads(num_threads)
{
    create_threads();
}

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
                    // Wait until not stopped and there are tasks, or we're being destroyed
                    _condition.wait(lock, [this] { 
                        return _destroying.load() || (!_stop.load() && !_tasks.empty());
                    });
                    
                    // Exit if being destroyed
                    if (_destroying.load())
                    {
                        return;
                    }
                    
                    // If stopped, continue waiting (don't process tasks)
                    if (_stop.load())
                    {
                        continue;
                    }
                    
                    _tasks_executing++;
                    task = std::move(_tasks.front());
                    _tasks.pop();
                }
                task();
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _tasks_executing--;
                    if (_tasks_executing.load() == 0)
                    {
                        _stopped_condition.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop.store(true);
        _destroying.store(true);
    }
    _condition.notify_all();
    
    for (auto& thread : _threads)
    {
        thread.join();
    }
}

bool ThreadPool::add_task(std::function<void()>&& task)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_stop.load())
        {
            return false;  // Don't add tasks after shutdown
        }
        _tasks.push(task);
    }
    _condition.notify_one();
    return true;
}

void ThreadPool::reset()
{
    // Set stop flag - threads will stop taking new tasks from queue
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop.store(true);
    }
    
    // Wait for all currently executing tasks to complete (threads naturally finish)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stopped_condition.wait(lock, [this] { return _tasks_executing.load() == 0; });
        
        // Now safe to clear the queue - no tasks executing and threads won't take new ones
        while (!_tasks.empty())
        {
            _tasks.pop();
        }
    }
}

void ThreadPool::start()
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_stop.load())
        {
            return;  // Already started
        }
        _stop.store(false);
    }
    _condition.notify_all();  // Wake threads to start processing
}

bool ThreadPool::is_idle() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _tasks.empty() && _tasks_executing.load() == 0;
}

bool ThreadPool::is_running() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return !_stop.load() && !_destroying.load();
}