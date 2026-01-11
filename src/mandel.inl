#pragma once

#include "mandel.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

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
template<typename FloatType>
MandelbrotRenderer<FloatType>::MandelbrotRenderer(int width, int height)
    : width_(width)
    , height_(height)
    , x_min_(static_cast<FloatType>(-2.5))
    , x_max_(static_cast<FloatType>(1.5))
    , y_min_(static_cast<FloatType>(-2.0))
    , y_max_(static_cast<FloatType>(2.0))
    , zoom_(static_cast<FloatType>(1.0))
    , max_iterations_(100)
    , palette_(256)
    , metrics_(width, height, x_min_, x_max_, y_min_, y_max_)
    , render_callback_(nullptr)
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

template<typename FloatType>
void MandelbrotRenderer<FloatType>::regenerate(ThreadPool* thread_pool)
{
    generate_mandelbrot(thread_pool);
}

}  // namespace mandel

