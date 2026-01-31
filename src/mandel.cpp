#include "mandel.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "thread_pool.hpp"

namespace mandel
{

// ColorScheme implementation
ColorScheme::ColorScheme(int count)
{
    const int n = (count > 0) ? count : 1;
    palette.clear();
    palette.reserve(static_cast<::std::size_t>(n));

    constexpr float two_pi = 6.2831853071795864769f;
    constexpr float phase_r = 0.0f;
    constexpr float phase_g = two_pi / 3.0f;
    constexpr float phase_b = 2.0f * two_pi / 3.0f;

    const auto to_u8 = [](const float x) -> ::std::uint8_t
    {
        const float clamped = ::std::clamp(x, 0.0f, 1.0f);
        return static_cast<::std::uint8_t>(::std::lround(clamped * 255.0f));
    };

    for (int i = 0; i < n; ++i)
    {
        const float t = (static_cast<float>(i) / static_cast<float>(n)) * two_pi;
        const float rf = 0.5f + 0.5f * ::std::sin(t + phase_r);
        const float gf = 0.5f + 0.5f * ::std::sin(t + phase_g);
        const float bf = 0.5f + 0.5f * ::std::sin(t + phase_b);
        palette.push_back(Color{to_u8(rf), to_u8(gf), to_u8(bf)});
    }
}

// CanvasMetrics implementation
CanvasMetrics::CanvasMetrics(int w, int h, FloatType xmin, FloatType xmax, FloatType ymin, FloatType ymax)
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

::std::complex<FloatType> CanvasMetrics::canvas_to_complex(int32_t x_pos, int32_t y_pos) const
{
    FloatType cx = x_min + pixel_to_x * x_pos;
    FloatType cy = y_min + pixel_to_y * y_pos;
    return ::std::complex<FloatType>(cx, cy);
}

// MandelbrotRenderer implementation
MandelbrotRenderer::MandelbrotRenderer(int width, int height)
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

void MandelbrotRenderer::init(int width, int height, ThreadPool* thread_pool)
{
    width_ = width;
    height_ = height;
    metrics_ = CanvasMetrics(width, height, x_min_, x_max_, y_min_, y_max_);
    regenerate(thread_pool);
}

void MandelbrotRenderer::set_render_callback(RenderCallback* callback)
{
    render_callback_ = callback;
}

int MandelbrotRenderer::compute_mandelbrot(::std::complex<FloatType> c, int max_iter) const
{
    // Optimized: use squared magnitude to avoid sqrt, and expand complex arithmetic manually
    FloatType zr = static_cast<FloatType>(0.0);
    FloatType zi = static_cast<FloatType>(0.0);
    FloatType cr = c.real();
    FloatType ci = c.imag();
    constexpr FloatType escape_radius_sq = static_cast<FloatType>(4.0);  // 2.0^2

    for (int i = 0; i < max_iter; ++i)
    {
        // Check squared magnitude: |z|^2 > 4 instead of |z| > 2 (avoids sqrt)
        FloatType magnitude_sq = zr * zr + zi * zi;
        if (magnitude_sq > escape_radius_sq)
        {
            return i;
        }

        // Manual complex multiplication: z = z * z + c
        // z^2 = (zr + i*zi)^2 = zr^2 - zi^2 + i*2*zr*zi
        FloatType zr_new = zr * zr - zi * zi + cr;
        FloatType zi_new = static_cast<FloatType>(2.0) * zr * zi + ci;
        zr = zr_new;
        zi = zi_new;
    }
    return max_iter;
}

inline void MandelbrotRenderer::paint_pixel(int x_pos, int y_pos, const ColorScheme::Color& color)
{
    // Optimized: precomputed width_*4 can be used in loops, but for single pixel access this is fine
    // Alpha channel already set to 255 during initialization, only need to set RGB
    int idx = (y_pos * width_ + x_pos) * 4;
    pixels_[idx + 0] = color[0];
    pixels_[idx + 1] = color[1];
    pixels_[idx + 2] = color[2];
    // Alpha channel (idx + 3) already 255, no need to set
}

inline int MandelbrotRenderer::process_pixel(int32_t x_pos, int32_t y_pos)
{
    // Inline canvas_to_complex to avoid function call overhead
    FloatType cx = metrics_.x_min + metrics_.pixel_to_x * x_pos;
    FloatType cy = metrics_.y_min + metrics_.pixel_to_y * y_pos;
    ::std::complex<FloatType> c(cx, cy);

    int iter = compute_mandelbrot(c, max_iterations_);

    // Cache palette size and optimize color lookup
    size_t palette_size = palette_.palette.size();
    auto const& color = iter == max_iterations_ ? ColorScheme::black : palette_.palette[static_cast<size_t>(iter) % palette_size];

    // Inline paint_pixel to avoid function call overhead
    // Alpha channel already set to 255 during initialization, only need to set RGB
    int idx = (y_pos * width_ + x_pos) * 4;
    pixels_[idx + 0] = color[0];
    pixels_[idx + 1] = color[1];
    pixels_[idx + 2] = color[2];
    // Alpha channel (idx + 3) already 255, no need to set

    return iter;
}

void MandelbrotRenderer::generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max)
{
    // Optimize: precompute frequently used values outside loops
    const FloatType pixel_to_x = metrics_.pixel_to_x;
    const FloatType pixel_to_y = metrics_.pixel_to_y;
    const FloatType x_min_coord = metrics_.x_min;
    const FloatType y_min_coord = metrics_.y_min;
    const int max_iter = max_iterations_;
    const size_t palette_size = palette_.palette.size();
    const int width_4 = width_ * 4;  // Precompute width * 4 for pixel indexing
    const FloatType escape_radius_sq = static_cast<FloatType>(4.0);

    // Precompute row base offsets to avoid repeated multiplication
    for (int32_t y_pos = y_min; y_pos <= y_max; ++y_pos)
    {
        const int row_base = y_pos * width_4;
        const FloatType cy = y_min_coord + pixel_to_y * y_pos;

        for (int32_t x_pos = x_min; x_pos <= x_max; ++x_pos)
        {
            // Fully inline all operations for maximum performance
            FloatType cx = x_min_coord + pixel_to_x * x_pos;

            // Inlined compute_mandelbrot
            FloatType zr = static_cast<FloatType>(0.0);
            FloatType zi = static_cast<FloatType>(0.0);
            int iter = max_iter;

            for (int i = 0; i < max_iter; ++i)
            {
                FloatType magnitude_sq = zr * zr + zi * zi;
                if (magnitude_sq > escape_radius_sq)
                {
                    iter = i;
                    break;
                }

                FloatType zr_new = zr * zr - zi * zi + cx;
                FloatType zi_new = static_cast<FloatType>(2.0) * zr * zi + cy;
                zr = zr_new;
                zi = zi_new;
            }

            // Color lookup and pixel painting
            // Alpha channel already set to 255 during initialization, only need to set RGB
            const ColorScheme::Color& color = iter == max_iter ? ColorScheme::black : palette_.palette[static_cast<size_t>(iter) % palette_size];
            int idx = row_base + x_pos * 4;
            pixels_[idx + 0] = color[0];
            pixels_[idx + 1] = color[1];
            pixels_[idx + 2] = color[2];
            // Alpha channel (idx + 3) already 255, no need to set
        }
    }
}

void MandelbrotRenderer::generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, ThreadPool* thread_pool, unsigned int generation)
{
    // Check if this render has been superseded by a newer one
    if (generation != render_generation_.load())
    {
        return;  // Stale render, exit early
    }

    // Precompute frequently used values to avoid repeated member access
    const FloatType pixel_to_x = metrics_.pixel_to_x;
    const FloatType pixel_to_y = metrics_.pixel_to_y;
    const FloatType x_min_coord = metrics_.x_min;
    const FloatType y_min_coord = metrics_.y_min;
    const int max_iter = max_iterations_;
    const size_t palette_size = palette_.palette.size();
    const int width_4 = width_ * 4;
    const FloatType escape_radius_sq = static_cast<FloatType>(4.0);

    // Helper lambda for fast pixel processing (inlined)
    auto fast_process_pixel = [&](int32_t x_pos, int32_t y_pos) -> int
    {
        FloatType cx = x_min_coord + pixel_to_x * x_pos;
        FloatType cy = y_min_coord + pixel_to_y * y_pos;

        FloatType zr = static_cast<FloatType>(0.0);
        FloatType zi = static_cast<FloatType>(0.0);
        int iter = max_iter;

        for (int i = 0; i < max_iter; ++i)
        {
            FloatType magnitude_sq = zr * zr + zi * zi;
            if (magnitude_sq > escape_radius_sq)
            {
                iter = i;
                break;
            }

            FloatType zr_new = zr * zr - zi * zi + cx;
            FloatType zi_new = static_cast<FloatType>(2.0) * zr * zi + cy;
            zr = zr_new;
            zi = zi_new;
        }

        const ColorScheme::Color& color = iter == max_iter ? ColorScheme::black : palette_.palette[static_cast<size_t>(iter) % palette_size];
        int idx = y_pos * width_4 + x_pos * 4;  // Use precomputed width_4
        pixels_[idx + 0] = color[0];
        pixels_[idx + 1] = color[1];
        pixels_[idx + 2] = color[2];
        // Alpha channel (idx + 3) already 255, no need to set

        return iter;
    };

    bool all_same = true;
    int const first_iter = fast_process_pixel(x_min, y_min);

    // Rest of top horizontal line
    for (int32_t x_pos = x_min + 1; x_pos <= x_max; ++x_pos)
    {
        if (first_iter != fast_process_pixel(x_pos, y_min))
        {
            all_same = false;
            // Early exit not beneficial here as we need to process all boundary pixels anyway
        }
    }

    // Bottom horizontal line
    for (int32_t x_pos = x_min; x_pos <= x_max; ++x_pos)
    {
        if (first_iter != fast_process_pixel(x_pos, y_max))
        {
            all_same = false;
        }
    }

    // Left vertical
    for (int32_t y_pos = y_min + 1; y_pos <= y_max - 1; ++y_pos)
    {
        if (first_iter != fast_process_pixel(x_min, y_pos))
        {
            all_same = false;
        }
    }

    // Right vertical
    for (int32_t y_pos = y_min + 1; y_pos <= y_max - 1; ++y_pos)
    {
        if (first_iter != fast_process_pixel(x_max, y_pos))
        {
            all_same = false;
        }
    }

    // Notify render callback after drawing boundaries (for progressive visualization)
    // Only if this render is still current (not superseded)
    if (generation == render_generation_.load() && render_callback_ && !pixels_.empty())
    {
        render_callback_->on_pixels_updated(pixels_.data(), width_, height_);
    }

    int inner_d_x = x_max - x_min - 1;
    int inner_d_y = y_max - y_min - 1;

    if (inner_d_x > 0 && inner_d_y > 0)
    {
        int new_x_min = x_min + 1;
        int new_x_max = x_max - 1;
        int new_y_min = y_min + 1;
        int new_y_max = y_max - 1;

        if (all_same)
        {
            // make sure we are not zoomed out so far that the entire mandelbrot is inside the bounds:
            auto c_min = metrics_.canvas_to_complex(x_min, y_min);
            if (c_min.real() <= -1.95 && c_min.imag() <= -1.95)
            {
                // if the entire mandelbrot is inside the default bounds, we cannot rely on the flood fill to be correct
                auto c_max = metrics_.canvas_to_complex(x_max, y_max);
                if (c_max.real() >= 1.95 || c_max.imag() >= 1.95)
                {
                    // if the entire mandelbrot is outside the default bounds, we cannot rely on the flood fill to be correct
                    all_same = false;
                }
            }
        }

        if (!all_same && thread_pool && thread_pool->is_paused())
        {
           all_same = true; // we are terminating the recursion, flood fill.
        }

        if (all_same)
        {
            // Flood fill entire interior - optimized using first row as pattern
            const ColorScheme::Color& color = first_iter == max_iter ? ColorScheme::black : palette_.palette[static_cast<size_t>(first_iter) % palette_size];
            const size_t fill_width = static_cast<size_t>(new_x_max - new_x_min + 1);
            const size_t fill_bytes = fill_width * 4;

            // Fill first row efficiently - construct pattern once, then duplicate
            // Alpha channel already 255 from buffer initialization, but we'll include it for memcpy
            unsigned char* first_row_start = &pixels_[new_y_min * width_4 + new_x_min * 4];

            // Write first pixel completely (RGB + alpha) as the pattern
            first_row_start[0] = color[0];
            first_row_start[1] = color[1];
            first_row_start[2] = color[2];
            first_row_start[3] = 255;  // Alpha (already 255, but needed for memcpy pattern)

            // Copy the first pixel pattern to all remaining pixels in the row using memcpy
            // This is much faster than a loop for larger rows
            // Use size_t to match fill_width - eliminates conversion overhead and matches pointer arithmetic type
            for (size_t x = 1; x < fill_width; ++x)
            {
                std::memcpy(first_row_start + x * 4, first_row_start, 4);
            }

            // Copy first row to all subsequent rows using memcpy (very fast)
            for (int32_t y_pos = new_y_min + 1; y_pos <= new_y_max; ++y_pos)
            {
                unsigned char* row_start = &pixels_[y_pos * width_4 + new_x_min * 4];
                std::memcpy(row_start, first_row_start, fill_bytes);
            }

            // Notify render callback after flood fill (for progressive visualization)
            if (generation == render_generation_.load() && render_callback_ && !pixels_.empty())
            {
                render_callback_->on_pixels_updated(pixels_.data(), width_, height_);
            }
        }
        else
        {
            constexpr int recurse_size_limit = 4;
            if (inner_d_x <= recurse_size_limit || inner_d_y <= recurse_size_limit)
            {
                generate_mandelbrot_direct(new_x_min, new_x_max, new_y_min, new_y_max);

                // Notify render callback after direct generation (for progressive visualization)
                if (generation == render_generation_.load() && render_callback_ && !pixels_.empty())
                {
                    render_callback_->on_pixels_updated(pixels_.data(), width_, height_);
                }
            }
            else
            {
                int x_mid = (new_x_min + new_x_max) / 2;
                int y_mid = (new_y_min + new_y_max) / 2;
                
                if (thread_pool == nullptr)
                {
                    // Direct recursive calls (synchronous)
                    generate_mandelbrot_recurse(new_x_min, x_mid, new_y_min, y_mid, nullptr, generation);
                    generate_mandelbrot_recurse(x_mid + 1, new_x_max, new_y_min, y_mid, nullptr, generation);
                    generate_mandelbrot_recurse(new_x_min, x_mid, y_mid + 1, new_y_max, nullptr, generation);
                    generate_mandelbrot_recurse(x_mid + 1, new_x_max, y_mid + 1, new_y_max, nullptr, generation);
                }
                else
                {
                    // Submit using provided thread pool
                    MandelbrotRenderer* self = this;
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(new_x_min, x_mid, new_y_min, y_mid, thread_pool, generation);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, new_y_min, y_mid, thread_pool, generation);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(new_x_min, x_mid, y_mid + 1, new_y_max, thread_pool, generation);
                    });
                    thread_pool->add_task([=]() {
                        self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, y_mid + 1, new_y_max, thread_pool, generation);
                    });
                }
            }
        }
    }
    
    // Notify render callback that pixels have been updated (only if still current generation)
    if (generation == render_generation_.load() && render_callback_ && !pixels_.empty())
    {
        render_callback_->on_pixels_updated(pixels_.data(), width_, height_);
    }
}

void MandelbrotRenderer::generate_mandelbrot(ThreadPool* thread_pool)
{
    // Increment generation to mark this as a new render
    unsigned int generation = ++render_generation_;

    // Update metrics
    metrics_ = CanvasMetrics(width_, height_, x_min_, x_max_, y_min_, y_max_);

    // Resize pixel buffer if needed, but DON'T clear existing pixels
    // Per RENDERING.md: zoom should render OVER existing image, not clear first
    // This provides visual continuity - old image stays visible while new one renders
    size_t pixel_count = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4;
    size_t old_size = pixels_.size();
    pixels_.resize(pixel_count);
    
    // Only initialize NEW pixels (if buffer grew) - set alpha to 255
    if (pixel_count > old_size)
    {
        // Initialize new portion to black with alpha=255
        for (size_t i = old_size; i < pixel_count; i += 4)
        {
            pixels_[i] = 0;      // R
            pixels_[i + 1] = 0;  // G
            pixels_[i + 2] = 0;  // B
            pixels_[i + 3] = 255; // A
        }
    }

    if (thread_pool == nullptr)
    {
        // Direct recursive calls (synchronous)
        generate_mandelbrot_recurse(0, width_ - 1, 0, height_ - 1, nullptr, generation);
    }
    else
    {
        // Use thread pool (start if it was reset above, or if it's not running)
        MandelbrotRenderer* self = this;
        thread_pool->add_task([=]() { self->generate_mandelbrot_recurse(0, width_ - 1, 0, height_ - 1, thread_pool, generation); });
    }
}

void MandelbrotRenderer::regenerate(ThreadPool* thread_pool, int pan_dx, int pan_dy)
{
    // Check if we're panning (non-zero pixel offsets)
    bool is_panning = (pan_dx != 0 || pan_dy != 0);

    if (thread_pool != nullptr)
    {
        thread_pool->resume();
    }

    if (is_panning && !pixels_.empty() && width_ > 0 && height_ > 0)
    {
        // Panning: shift existing pixels and regenerate only exposed regions
        // Clamp pan offsets to valid range
        pan_dx = std::clamp(pan_dx, -width_ + 1, width_ - 1);
        pan_dy = std::clamp(pan_dy, -height_ + 1, height_ - 1);

        if (pan_dx != 0 || pan_dy != 0)
        {
            // Update metrics first (bounds have changed)
            metrics_ = CanvasMetrics(width_, height_, x_min_, x_max_, y_min_, y_max_);

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

            // Increment generation to mark this as a new render
            unsigned int generation = ++render_generation_;

            // Regenerate the exposed regions
            if (pan_dx != 0 && pan_dy != 0)
            {
                // Two regions to regenerate (L-shaped or cross-shaped)
                // Top/bottom strip + left/right strip
                if (pan_dy > 0)
                {
                    // Top strip
                    generate_mandelbrot_recurse(0, width_ - 1, 0, pan_dy - 1, thread_pool, generation);
                }
                else
                {
                    // Bottom strip
                    int abs_dy = -pan_dy;
                    generate_mandelbrot_recurse(0, width_ - 1, height_ - abs_dy, height_ - 1, thread_pool, generation);
                }

                if (pan_dx > 0)
                {
                    // Left strip (excluding the part already regenerated)
                    generate_mandelbrot_recurse(0, pan_dx - 1, pan_dy > 0 ? pan_dy : 0, pan_dy < 0 ? height_ + pan_dy - 1 : height_ - 1, thread_pool, generation);
                }
                else
                {
                    // Right strip (excluding the part already regenerated)
                    int abs_dx = -pan_dx;
                    generate_mandelbrot_recurse(width_ - abs_dx, width_ - 1, pan_dy > 0 ? pan_dy : 0, pan_dy < 0 ? height_ + pan_dy - 1 : height_ - 1, thread_pool, generation);
                }
            }
            else if (pan_dy != 0)
            {
                // Single vertical strip
                generate_mandelbrot_recurse(0, width_ - 1, y_min_new, y_max_new, thread_pool, generation);
            }
            else
            {
                // Single horizontal strip
                generate_mandelbrot_recurse(x_min_new, x_max_new, 0, height_ - 1, thread_pool, generation);
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

    // Note: We intentionally do NOT pause the thread pool here.
    // This allows progressive rendering to work - the main loop can update
    // the display while worker threads continue processing.
    // The pool will naturally become idle when all tasks complete.
}

}  // namespace mandel
