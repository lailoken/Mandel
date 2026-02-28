#pragma once

#include "mandel.hpp"
#include "thread_pool.hpp"
#include "worker_base.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace mandel
{

// Worker class that manages a MandelbrotRenderer with a canvas
// Keeps reference to atomic generation and canvas limits
// Allows buffer swapping (waits for renderers to complete before transferring data)
class MandelWorker : public WorkerBase
{
public:
    MandelWorker(int canvas_width, int canvas_height, 
                 std::atomic<unsigned int>& current_generation,
                 FloatType canvas_x_min,
                 FloatType canvas_x_max,
                 FloatType canvas_y_min,
                 FloatType canvas_y_max,
                 ThreadPool& thread_pool);
    ~MandelWorker();

    // WorkerBase interface
    void start_render() override;
    bool try_complete_render() override;

    // MandelWorker-specific methods
    // Wait for renderers to complete and get buffer copy (for UI to call)
    // Returns true if copy was successful, buffer is copied to target_buffer
    bool wait_and_get_buffer(std::vector<unsigned char>& target_buffer);

    // Update parameters (WorkerBase override)
    void set_max_iterations(int max_iterations) override { max_iterations_ = max_iterations; }

    // Get parameters (needed for UI)
    int get_max_iterations() const { return max_iterations_; }

private:
    std::unique_ptr<MandelbrotRenderer> renderer_;  // Created fresh for each render
    ThreadPool& thread_pool_;  // Reference to thread pool (always set)
    int max_iterations_;  // Stored for renderer creation
    std::mutex canvas_mutex_;  // Protects canvas during merge operations
    unsigned int start_generation_;  // Generation when current render started (for stale check)
};

}  // namespace mandel
