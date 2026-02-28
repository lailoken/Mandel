#include "mandel_worker.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _DEBUG
#define DEBUG_PRINTF(...) ((void)0)
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

namespace mandel
{

MandelWorker::MandelWorker(int canvas_width, int canvas_height,
                           std::atomic<unsigned int>& current_generation,
                           FloatType canvas_x_min,
                           FloatType canvas_x_max,
                           FloatType canvas_y_min,
                           FloatType canvas_y_max,
                           ThreadPool& thread_pool)
    : WorkerBase(canvas_width, canvas_height, current_generation, canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max)
    , thread_pool_(thread_pool)
    , max_iterations_(512)
    , start_generation_(0)
{
    // Canvas buffer is initialized in WorkerBase constructor
    // Renderer will be created in start_render() with all parameters
}

void MandelWorker::start_render()
{
    // Capture start generation for this render
    start_generation_ = current_generation_.load();
    
    // Get current canvas bounds (capture snapshot - no lock needed)
    // UI can update bounds freely; generation check will bail out stale renders
    FloatType x_min = canvas_x_min_;
    FloatType x_max = canvas_x_max_;
    FloatType y_min = canvas_y_min_;
    FloatType y_max = canvas_y_max_;
    
    // Clear canvas when bounds change significantly (e.g., during panning)
    // This ensures the canvas buffer is aligned with the new bounds
    // We clear it here so that when the renderer completes, we have a clean slate
    // The renderer will fill the entire canvas with new data at the new bounds
    {
        std::lock_guard<std::mutex> lock(canvas_mutex_);
        // Clear canvas to black (with alpha=255) to ensure clean merge
        std::memset(canvas_.data(), 0, canvas_.size());
        for (size_t i = 3; i < canvas_.size(); i += 4)
        {
            canvas_[i] = 255; // Alpha
        }
    }
    
    // Create renderer with all parameters - it starts rendering immediately
    // Renderer creates its own internal buffer - no canvas pointer needed
    DEBUG_PRINTF("[WORKER] Creating renderer: start_gen=%u\n", start_generation_);
    renderer_ = std::make_unique<MandelbrotRenderer>(
        canvas_width_,
        canvas_height_,
        x_min,
        x_max,
        y_min,
        y_max,
        max_iterations_,
        current_generation_,
        start_generation_,
        thread_pool_);
    DEBUG_PRINTF("[WORKER] Renderer created: renderer=%p\n", static_cast<void*>(renderer_.get()));
    
    DEBUG_PRINTF("[WORKER] start_render: width=%d, height=%d, bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), max_iter=%d\n",
           canvas_width_, canvas_height_, x_min, x_max, y_min, y_max, max_iterations_);
}

bool MandelWorker::try_complete_render()
{
    if (!renderer_)
        return false;
    if (start_generation_ != current_generation_.load())
        return false;
    if (!thread_pool_.is_idle())
        return false;

    // Merge renderer buffer into canvas (same as wait_and_get_buffer but non-blocking)
    std::lock_guard<std::mutex> lock(canvas_mutex_);
    const std::vector<unsigned char>& renderer_pixels = renderer_->get_pixels();
    if (renderer_pixels.size() != canvas_.size())
        return false;
    std::memcpy(canvas_.data(), renderer_pixels.data(), canvas_.size());
    return true;
}

bool MandelWorker::wait_and_get_buffer(std::vector<unsigned char>& target_buffer)
{
    if (!renderer_)
    {
        DEBUG_PRINTF("[WORKER] wait_and_get_buffer: no renderer\n");
        return false;
    }
    
    // CRITICAL: Check generation BEFORE waiting - if already stale, bail out immediately
    if (start_generation_ != current_generation_.load())
    {
        DEBUG_PRINTF("[WORKER] wait_and_get_buffer: generation already changed (start=%u, current=%u), renderer=%p\n", 
               start_generation_, current_generation_.load(), static_cast<void*>(renderer_.get()));
        return false; // Render was superseded before we even started waiting
    }
    
    DEBUG_PRINTF("[WORKER] wait_and_get_buffer: waiting for idle, renderer=%p, start_gen=%u\n", 
           static_cast<void*>(renderer_.get()), start_generation_);
    // Wait for thread pool to be idle (all renderers completed)
    // Check generation periodically during wait to bail out early if cancelled
    int wait_count = 0;
    while (!thread_pool_.is_idle())
    {
        // Check generation every 10ms during wait - if cancelled, bail out immediately
        if (wait_count % 10 == 0 && start_generation_ != current_generation_.load())
        {
            DEBUG_PRINTF("[WORKER] wait_and_get_buffer: generation changed during wait (start=%u, current=%u), renderer=%p\n", 
                   start_generation_, current_generation_.load(), static_cast<void*>(renderer_.get()));
            return false; // Render was cancelled during wait
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        wait_count++;
        if (wait_count % 100 == 0)
        {
            DEBUG_PRINTF("[WORKER] Still waiting... renderer=%p\n", static_cast<void*>(renderer_.get()));
        }
    }
    DEBUG_PRINTF("[WORKER] wait_and_get_buffer: thread pool is idle after %d ms, renderer=%p\n", wait_count, static_cast<void*>(renderer_.get()));
    
    // Final check after waiting - generation might have changed while we were waiting
    if (start_generation_ != current_generation_.load())
    {
        DEBUG_PRINTF("[WORKER] wait_and_get_buffer: generation changed after wait (start=%u, current=%u), renderer=%p\n", 
               start_generation_, current_generation_.load(), static_cast<void*>(renderer_.get()));
        return false; // Render was superseded
    }
    
    // Extra safety: wait a bit more to ensure all tasks really completed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // One more check after the extra wait
    if (start_generation_ != current_generation_.load())
    {
        DEBUG_PRINTF("[WORKER] wait_and_get_buffer: generation changed after extra wait (start=%u, current=%u), renderer=%p\n", 
               start_generation_, current_generation_.load(), static_cast<void*>(renderer_.get()));
        return false; // Render was superseded
    }
    
    DEBUG_PRINTF("[WORKER] wait_and_get_buffer: extra wait complete, renderer=%p\n", static_cast<void*>(renderer_.get()));
    
    // Get the completed buffer from renderer and merge into canvas
    {
        std::lock_guard<std::mutex> lock(canvas_mutex_);
        
        const std::vector<unsigned char>& renderer_pixels = renderer_->get_pixels();
        
        // Validate buffer sizes match
        if (renderer_pixels.size() != canvas_.size())
        {
            return false; // Size mismatch - should not happen
        }
        
        // Merge renderer buffer into canvas (fast memcpy)
        DEBUG_PRINTF("[WORKER] Merging buffer: renderer_pixels.size()=%zu, canvas_.size()=%zu\n", 
               renderer_pixels.size(), canvas_.size());
        
#ifdef _DEBUG
        // Check if renderer actually wrote any non-black pixels
        size_t non_black_count = 0;
        for (size_t i = 0; i < renderer_pixels.size() && i < 1000; i += 4)
        {
            if (renderer_pixels[i] != 0 || renderer_pixels[i+1] != 0 || renderer_pixels[i+2] != 0)
            {
                non_black_count++;
            }
        }
        DEBUG_PRINTF("[WORKER] Non-black pixels in first 250 pixels: %zu\n", non_black_count);
#endif
        
        std::memcpy(canvas_.data(), renderer_pixels.data(), canvas_.size());
        DEBUG_PRINTF("[WORKER] Buffer merged successfully\n");
    }
    
    // Copy merged canvas to target buffer
    target_buffer = canvas_;
    DEBUG_PRINTF("[WORKER] wait_and_get_buffer: returning buffer of size %zu\n", target_buffer.size());
    
    return true;
}

MandelWorker::~MandelWorker()
{
    DEBUG_PRINTF("[WORKER] Destructor called: renderer=%p\n", static_cast<void*>(renderer_.get()));
    // Wait for thread pool to be idle before destroying renderer
    DEBUG_PRINTF("[WORKER] Waiting for thread pool idle before destruction...\n");
    while (!thread_pool_.is_idle())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    DEBUG_PRINTF("[WORKER] Thread pool idle, destroying renderer=%p\n", static_cast<void*>(renderer_.get()));
    // Extra safety wait
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    DEBUG_PRINTF("[WORKER] Destructor complete\n");
}

}  // namespace mandel
