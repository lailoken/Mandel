#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mandel.hpp"
#include "thread_pool.hpp"

namespace mandel
{

// Template implementation of CanvasMetrics
template<typename FloatType>
CanvasMetrics<FloatType>::CanvasMetrics(int w, int h, FloatType xmin, FloatType xmax, FloatType ymin, FloatType ymax)
    : width(w)
    , height(h)
    , x_min(xmin)
    , x_max(xmax)
    , y_min(ymin)
    , y_max(ymax)
{
    pixel_to_x = (x_max - x_min) / width;
    pixel_to_y = (y_max - y_min) / height;
}

template<typename FloatType>
::std::complex<FloatType> CanvasMetrics<FloatType>::canvas_to_complex(int32_t x_pos, int32_t y_pos) const
{
    FloatType cx = x_min + pixel_to_x * x_pos;
    FloatType cy = y_min + pixel_to_y * y_pos;
    return ::std::complex<FloatType>(cx, cy);
}

// Template implementation of MandelbrotRenderer
template <typename FloatType>
MandelbrotRenderer<FloatType>::MandelbrotRenderer(int width, int height)
    : width_(width),
      height_(height),
      x_min_(static_cast<FloatType>(-2.5)),
      x_max_(static_cast<FloatType>(1.5)),
      y_min_(static_cast<FloatType>(-2.0)),
      y_max_(static_cast<FloatType>(2.0)),
      zoom_(1ULL),
      max_iterations_(512),
      palette_(32),
      metrics_(width, height, x_min_, x_max_, y_min_, y_max_),
      render_callback_(nullptr)
{
    init(width, height);
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::init(int width, int height, ThreadPool* thread_pool)
{
    width_ = width;
    height_ = height;
    metrics_ = CanvasMetrics<FloatType>(width, height, x_min_, x_max_, y_min_, y_max_);
    regenerate(thread_pool);
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::set_render_callback(RenderCallback* callback)
{
    render_callback_ = callback;
}

template<typename FloatType>
int MandelbrotRenderer<FloatType>::compute_mandelbrot(::std::complex<FloatType> c, int max_iter) const
{
    ::std::complex<FloatType> z(static_cast<FloatType>(0.0), static_cast<FloatType>(0.0));

    for (int i = 0; i < max_iter; ++i)
    {
        if (::std::abs(z) > static_cast<FloatType>(2.0))
        {
            return i;
        }
        z = z * z + c;
    }
    return max_iter;
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::paint_pixel(int x_pos, int y_pos, const ColorScheme::Color& color)
{
    int idx = (y_pos * width_ + x_pos) * 4;
    pixels_[idx + 0] = color[0];
    pixels_[idx + 1] = color[1];
    pixels_[idx + 2] = color[2];
    pixels_[idx + 3] = 255;
}

template<typename FloatType>
int MandelbrotRenderer<FloatType>::process_pixel(int32_t x_pos, int32_t y_pos)
{
    int iter = compute_mandelbrot(metrics_.canvas_to_complex(x_pos, y_pos), max_iterations_);

    auto const& color = iter == max_iterations_ ? ColorScheme::black : palette_.palette[iter % palette_.palette.size()];
    paint_pixel(x_pos, y_pos, color);
    return iter;
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max)
{
    for (int32_t x_pos = x_min; x_pos <= x_max; ++x_pos)
    {
        for (int32_t y_pos = y_min; y_pos <= y_max; ++y_pos)
        {
            process_pixel(x_pos, y_pos);
        }
    }
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, ThreadPool* thread_pool)
{
    bool all_same = true;
    int const first_iter = process_pixel(x_min, y_min);

    // Rest of top horizontal line
    for (int32_t x_pos = x_min + 1; x_pos <= x_max; ++x_pos)
    {
        if (first_iter != process_pixel(x_pos, y_min))
        {
            all_same = false;
        }
    }

    // Bottom horizontal line
    for (int32_t x_pos = x_min; x_pos <= x_max; ++x_pos)
    {
        if (first_iter != process_pixel(x_pos, y_max))
        {
            all_same = false;
        }
    }

    // Left vertical
    for (int32_t y_pos = y_min + 1; y_pos <= y_max - 1; ++y_pos)
    {
        if (first_iter != process_pixel(x_min, y_pos))
        {
            all_same = false;
        }
    }

    // Right vertical
    for (int32_t y_pos = y_min + 1; y_pos <= y_max - 1; ++y_pos)
    {
        if (first_iter != process_pixel(x_max, y_pos))
        {
            all_same = false;
        }
    }

    int inner_d_x = x_max - x_min - 1;
    int inner_d_y = y_max - y_min - 1;

    if (inner_d_x > 0 || inner_d_y > 0)
    {
        int new_x_min = x_min + 1;
        int new_x_max = x_max - 1;
        int new_y_min = y_min + 1;
        int new_y_max = y_max - 1;

        if (all_same)
        {
            auto c_min = metrics_.canvas_to_complex(x_min, y_min);
            if (c_min.real() <= default_x_min && c_min.imag() <= default_y_min)
            {
                // if the entire mandelbrot is inside the default bounds, we cannot rely on the flood fill to be correct
                auto c_max = metrics_.canvas_to_complex(x_max, y_max);
                if (c_max.real() >= default_x_max || c_max.imag() >= default_y_max)
                {
                    // if the entire mandelbrot is outside the default bounds, we cannot rely on the flood fill to be correct
                    all_same = false;
                }
            }
        }

        if (!all_same && thread_pool && !thread_pool->is_running())
        {
           all_same = true; // we are terminating the recursion, flood fill.
        }

        if (all_same)
        {
            // Flood fill entire interior
            auto const& color = first_iter == max_iterations_ ? ColorScheme::black : palette_.palette[first_iter % palette_.palette.size()];
            for (int32_t x_pos = new_x_min; x_pos <= new_x_max; ++x_pos)
            {
                for (int32_t y_pos = new_y_min; y_pos <= new_y_max; ++y_pos)
                {
                    paint_pixel(x_pos, y_pos, color);
                }
            }
        }
        else
        {
            if (inner_d_x <= 4 || inner_d_y <= 4)
            {
                generate_mandelbrot_direct(new_x_min, new_x_max, new_y_min, new_y_max);
            }
            else
            {
                int x_mid = (new_x_min + new_x_max) / 2;
                int y_mid = (new_y_min + new_y_max) / 2;
                
                if (thread_pool == nullptr)
                {
                    // Direct recursive calls (synchronous)
                    generate_mandelbrot_recurse(new_x_min, x_mid, new_y_min, y_mid, nullptr);
                    generate_mandelbrot_recurse(x_mid + 1, new_x_max, new_y_min, y_mid, nullptr);
                    generate_mandelbrot_recurse(new_x_min, x_mid, y_mid + 1, new_y_max, nullptr);
                    generate_mandelbrot_recurse(x_mid + 1, new_x_max, y_mid + 1, new_y_max, nullptr);
                }
                else
                {
                    // Submit using provided thread pool
                    MandelbrotRenderer<FloatType>* self = this;
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(new_x_min, x_mid, new_y_min, y_mid, thread_pool);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, new_y_min, y_mid, thread_pool);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(new_x_min, x_mid, y_mid + 1, new_y_max, thread_pool);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, y_mid + 1, new_y_max, thread_pool);
                    });
                }
            }
        }
    }
    
    // Notify render callback that pixels have been updated
    if (render_callback_ && !pixels_.empty())
    {
        render_callback_->on_pixels_updated(pixels_.data(), width_, height_);
    }
}

template<typename FloatType>
void MandelbrotRenderer<FloatType>::generate_mandelbrot(ThreadPool* thread_pool)
{
    // Update metrics
    metrics_ = CanvasMetrics<FloatType>(width_, height_, x_min_, x_max_, y_min_, y_max_);
    
    // Resize pixel buffer
    size_t pixel_count = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4;
    pixels_.resize(pixel_count);
    
    if (thread_pool == nullptr)
    {
        // Direct recursive calls (synchronous)
        generate_mandelbrot_recurse(0, width_ - 1, 0, height_ - 1, nullptr);
    }
    else
    {
        // Use thread pool
        thread_pool->reset();
        thread_pool->start();
        MandelbrotRenderer<FloatType>* self = this;
        thread_pool->add_task([=]() { self->generate_mandelbrot_recurse(0, width_ - 1, 0, height_ - 1, thread_pool); });
    }
}

template <typename FloatType>
void MandelbrotRenderer<FloatType>::regenerate(ThreadPool* thread_pool, int pan_dx, int pan_dy)
{
    // Check if we're panning (non-zero pixel offsets)
    bool is_panning = (pan_dx != 0 || pan_dy != 0);

    if (is_panning && !pixels_.empty() && width_ > 0 && height_ > 0)
    {
        // Panning: shift existing pixels and regenerate only exposed regions
        // Clamp pan offsets to valid range
        pan_dx = std::clamp(pan_dx, -width_ + 1, width_ - 1);
        pan_dy = std::clamp(pan_dy, -height_ + 1, height_ - 1);

        if (pan_dx != 0 || pan_dy != 0)
        {
            // Update metrics first (bounds have changed)
            metrics_ = CanvasMetrics<FloatType>(width_, height_, x_min_, x_max_, y_min_, y_max_);

            // Shift pixels using memmove
            // For positive pan_dx: shift right (new data on left)
            // For negative pan_dx: shift left (new data on right)
            // Similar for pan_dy

            if (pan_dx != 0 && pan_dy != 0)
            {
                // Both X and Y panning: use full copy to avoid overwriting issues
                // Copy from source region to destination region with offset
                std::vector<unsigned char> temp_buffer(pixels_);

                // Calculate source and destination regions
                int src_x_min, dst_x_min, copy_width;
                int src_y_min, dst_y_min, copy_height;

                if (pan_dx > 0)
                {
                    // Shift right: source is left part, destination is right part
                    src_x_min = 0;
                    dst_x_min = pan_dx;
                    copy_width = width_ - pan_dx;
                }
                else
                {
                    // Shift left: source is right part, destination is left part
                    int abs_dx = -pan_dx;
                    src_x_min = abs_dx;
                    dst_x_min = 0;
                    copy_width = width_ - abs_dx;
                }

                if (pan_dy > 0)
                {
                    // Shift down: source is top part, destination is bottom part
                    src_y_min = 0;
                    dst_y_min = pan_dy;
                    copy_height = height_ - pan_dy;
                }
                else
                {
                    // Shift up: source is bottom part, destination is top part
                    int abs_dy = -pan_dy;
                    src_y_min = abs_dy;
                    dst_y_min = 0;
                    copy_height = height_ - abs_dy;
                }

                // Copy overlapping region from source to destination
                for (int y = 0; y < copy_height; ++y)
                {
                    int src_y = src_y_min + y;
                    int dst_y = dst_y_min + y;
                    if (dst_y >= 0 && dst_y < height_ && src_y >= 0 && src_y < height_)
                    {
                        std::memcpy(&pixels_[dst_y * width_ * 4 + dst_x_min * 4], &temp_buffer[src_y * width_ * 4 + src_x_min * 4], static_cast<size_t>(copy_width) * 4);
                    }
                }
            }
            else if (pan_dy != 0)
            {
                // Only vertical panning
                if (pan_dy > 0)
                {
                    // Shift rows down (new data on top)
                    for (int y = height_ - 1; y >= pan_dy; --y)
                    {
                        std::memmove(&pixels_[y * width_ * 4], &pixels_[(y - pan_dy) * width_ * 4], static_cast<size_t>(width_) * 4);
                    }
                }
                else
                {
                    // Shift rows up (new data on bottom)
                    int abs_dy = -pan_dy;
                    for (int y = 0; y < height_ - abs_dy; ++y)
                    {
                        std::memmove(&pixels_[y * width_ * 4], &pixels_[(y + abs_dy) * width_ * 4], static_cast<size_t>(width_) * 4);
                    }
                }
            }
            else if (pan_dx != 0)
            {
                // Only horizontal panning
                for (int y = 0; y < height_; ++y)
                {
                    unsigned char* row_start = &pixels_[y * width_ * 4];
                    if (pan_dx > 0)
                    {
                        // Shift right (new data on left)
                        std::memmove(row_start + static_cast<size_t>(pan_dx) * 4, row_start, static_cast<size_t>(width_ - pan_dx) * 4);
                    }
                    else
                    {
                        // Shift left (new data on right)
                        int abs_dx = -pan_dx;
                        std::memmove(row_start, row_start + static_cast<size_t>(abs_dx) * 4, static_cast<size_t>(width_ - abs_dx) * 4);
                    }
                }
            }

            // Determine which regions need to be regenerated
            int32_t x_min_new = 0, x_max_new = width_ - 1;
            int32_t y_min_new = 0, y_max_new = height_ - 1;

            if (pan_dx > 0)
            {
                // Shifted right, regenerate left edge
                x_min_new = 0;
                x_max_new = pan_dx - 1;
            }
            else if (pan_dx < 0)
            {
                // Shifted left, regenerate right edge
                x_min_new = width_ + pan_dx;
                x_max_new = width_ - 1;
            }

            if (pan_dy > 0)
            {
                // Shifted down, regenerate top edge
                y_min_new = 0;
                y_max_new = pan_dy - 1;
            }
            else if (pan_dy < 0)
            {
                // Shifted up, regenerate bottom edge
                y_min_new = height_ + pan_dy;
                y_max_new = height_ - 1;
            }

            // Regenerate the exposed regions
            if (pan_dx != 0 && pan_dy != 0)
            {
                // Two regions to regenerate (L-shaped or cross-shaped)
                // Top/bottom strip + left/right strip
                if (pan_dy > 0)
                {
                    // Top strip
                    generate_mandelbrot_recurse(0, width_ - 1, 0, pan_dy - 1, thread_pool);
                }
                else
                {
                    // Bottom strip
                    int abs_dy = -pan_dy;
                    generate_mandelbrot_recurse(0, width_ - 1, height_ - abs_dy, height_ - 1, thread_pool);
                }

                if (pan_dx > 0)
                {
                    // Left strip (excluding the part already regenerated)
                    generate_mandelbrot_recurse(0, pan_dx - 1, pan_dy > 0 ? pan_dy : 0, pan_dy < 0 ? height_ + pan_dy - 1 : height_ - 1, thread_pool);
                }
                else
                {
                    // Right strip (excluding the part already regenerated)
                    int abs_dx = -pan_dx;
                    generate_mandelbrot_recurse(width_ - abs_dx, width_ - 1, pan_dy > 0 ? pan_dy : 0, pan_dy < 0 ? height_ + pan_dy - 1 : height_ - 1, thread_pool);
                }
            }
            else if (pan_dy != 0)
            {
                // Single vertical strip
                generate_mandelbrot_recurse(0, width_ - 1, y_min_new, y_max_new, thread_pool);
            }
            else
            {
                // Single horizontal strip
                generate_mandelbrot_recurse(x_min_new, x_max_new, 0, height_ - 1, thread_pool);
            }
        }
        else
        {
            // No actual panning, regenerate everything
            generate_mandelbrot(thread_pool);
        }
    }
    else
    {
        // Not panning, regenerate everything
        generate_mandelbrot(thread_pool);
    }
}

}  // namespace mandel

