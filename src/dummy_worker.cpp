#include "dummy_worker.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace mandel
{

DummyWorker::DummyWorker(int canvas_width, int canvas_height,
                         std::atomic<unsigned int>& current_generation,
                         long double canvas_x_min,
                         long double canvas_x_max,
                         long double canvas_y_min,
                         long double canvas_y_max)
    : WorkerBase(canvas_width, canvas_height, current_generation, canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max)
{
}

void DummyWorker::start_render()
{
    // Generate dummy image (synchronous) - no sleep so main thread stays responsive
    generate_dummy_image();
}

bool DummyWorker::try_complete_render()
{
    return true;  // Sync worker: canvas is ready immediately after start_render()
}

void DummyWorker::generate_dummy_image()
{
    // Resize canvas if needed (in case init was called)
    size_t pixel_count = static_cast<size_t>(canvas_width_) * static_cast<size_t>(canvas_height_) * 4;
    if (canvas_.size() != pixel_count)
    {
        canvas_.resize(pixel_count);
        // Initialize new pixels to black with alpha=255
        std::memset(canvas_.data(), 0, pixel_count);
        for (size_t i = 3; i < pixel_count; i += 4)
        {
            canvas_[i] = 255; // Alpha
        }
    }
    else
    {
        // Clear to black
        std::memset(canvas_.data(), 0, pixel_count);
        for (size_t i = 3; i < pixel_count; i += 4)
        {
            canvas_[i] = 255; // Alpha
        }
    }
    
    // Draw 5pt thick border (white)
    const int border_thickness = 5;
    unsigned char border_color[4] = {255, 255, 255, 255}; // White
    
    // Top and bottom borders
    for (int y = 0; y < border_thickness; y++)
    {
        for (int x = 0; x < canvas_width_; x++)
        {
            size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(canvas_width_) + static_cast<size_t>(x)) * 4;
            std::memcpy(canvas_.data() + idx, border_color, 4);
            
            idx = ((static_cast<size_t>(canvas_height_ - 1 - y) * static_cast<size_t>(canvas_width_) + static_cast<size_t>(x)) * 4);
            std::memcpy(canvas_.data() + idx, border_color, 4);
        }
    }
    
    // Left and right borders
    for (int x = 0; x < border_thickness; x++)
    {
        for (int y = 0; y < canvas_height_; y++)
        {
            size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(canvas_width_) + static_cast<size_t>(x)) * 4;
            std::memcpy(canvas_.data() + idx, border_color, 4);
            
            idx = (static_cast<size_t>(y) * static_cast<size_t>(canvas_width_) + static_cast<size_t>(canvas_width_ - 1 - x)) * 4;
            std::memcpy(canvas_.data() + idx, border_color, 4);
        }
    }
    
}

}  // namespace mandel
