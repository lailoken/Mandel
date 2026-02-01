#pragma once

#include "mandel.hpp"
#include "thread_pool.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace mandel
{

// Worker class that manages a MandelbrotRenderer with a canvas
// Keeps reference to atomic generation and canvas limits
// Allows buffer swapping (waits for renderers to complete before transferring data)
class MandelWorker
{
public:
    MandelWorker(int canvas_width, int canvas_height, 
                 std::atomic<unsigned int>& current_generation,
                 FloatType& canvas_x_min,
                 FloatType& canvas_x_max,
                 FloatType& canvas_y_min,
                 FloatType& canvas_y_max,
                 ThreadPool& thread_pool);
    ~MandelWorker();

    // Initialize/resize the canvas
    void init(int canvas_width, int canvas_height);

    // Start rendering with current atomic values
    void start_render();

    // Wait for renderers to complete and get buffer copy (for UI to call)
    // Returns true if copy was successful, buffer is copied to target_buffer
    bool wait_and_get_buffer(std::vector<unsigned char>& target_buffer);

    // Update parameters
    void set_max_iterations(int max_iterations) { max_iterations_ = max_iterations; }

    // Get parameters (needed for UI)
    int get_max_iterations() const { return max_iterations_; }
    const unsigned char* get_pixels() const { return canvas_.data(); }
    unsigned int get_start_generation() const { return start_generation_; }

private:
    std::unique_ptr<MandelbrotRenderer> renderer_;  // Created fresh for each render
    int canvas_width_;
    int canvas_height_;
    unsigned int start_generation_;
    std::atomic<unsigned int>& current_generation_;
    FloatType& canvas_x_min_;
    FloatType& canvas_x_max_;
    FloatType& canvas_y_min_;
    FloatType& canvas_y_max_;
    ThreadPool& thread_pool_;  // Reference to thread pool (always set)
    std::vector<unsigned char> canvas_;  // Own canvas buffer
    int max_iterations_;  // Stored for renderer creation
    std::mutex canvas_mutex_;  // Protects canvas during merge operations
};

}  // namespace mandel
