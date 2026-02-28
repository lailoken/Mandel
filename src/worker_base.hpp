#pragma once

#include <atomic>
#include <cstring>
#include <vector>

namespace mandel
{

// Base class for all workers that render to a canvas
class WorkerBase
{
public:
    WorkerBase(int canvas_width, int canvas_height,
               std::atomic<unsigned int>& current_generation,
               long double canvas_x_min,
               long double canvas_x_max,
               long double canvas_y_min,
               long double canvas_y_max)
        : canvas_width_(canvas_width)
        , canvas_height_(canvas_height)
        , current_generation_(current_generation)
        , canvas_x_min_(canvas_x_min)
        , canvas_x_max_(canvas_x_max)
        , canvas_y_min_(canvas_y_min)
        , canvas_y_max_(canvas_y_max)
    {
        // Initialize canvas buffer (RGBA)
        size_t pixel_char_count = static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4;
        canvas_.resize(pixel_char_count);
        // Initialize to black with alpha=255
        std::memset(canvas_.data(), 0, pixel_char_count);
        for (size_t i = 3; i < pixel_char_count; i += 4)
        {
            canvas_[i] = 255; // Alpha
        }
    }

    virtual ~WorkerBase() = default;

    // Start rendering
    virtual void start_render() = 0;

    // Non-blocking: returns true if render completed and canvas_ is ready (merge done for async workers)
    virtual bool try_complete_render() = 0;

    // Set max iterations for next render (no-op for workers that don't use it)
    virtual void set_max_iterations(int) {}

    // Public members
    int canvas_width_;
    int canvas_height_;
    std::atomic<unsigned int>& current_generation_;
    long double canvas_x_min_;
    long double canvas_x_max_;
    long double canvas_y_min_;
    long double canvas_y_max_;
    std::vector<unsigned char> canvas_;  // RGBA buffer
};

}  // namespace mandel
