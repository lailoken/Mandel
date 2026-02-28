#include "mandel.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _DEBUG
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

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

// Singleton implementation - thread-safe initialization using static local variable
const ColorScheme& ColorScheme::get_instance()
{
    static const ColorScheme instance(256);  // Default palette size
    return instance;
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
    // Invert Y axis: Screen Y goes down, Complex Y goes up.
    // So pixel 0 (top) corresponds to y_max. pixel height (bottom) corresponds to y_min.
    pixel_to_y = (y_min - y_max) / height;
}

::std::complex<FloatType> CanvasMetrics::canvas_to_complex(int32_t x_pos, int32_t y_pos) const
{
    FloatType cx = x_min + pixel_to_x * x_pos;
    // Start from y_max (Top)
    FloatType cy = y_max + pixel_to_y * y_pos;
    return ::std::complex<FloatType>(cx, cy);
}

// MandelbrotRenderer implementation
MandelbrotRenderer::MandelbrotRenderer(int width, int height, FloatType x_min, FloatType x_max, FloatType y_min, FloatType y_max, int max_iterations,
                                       std::atomic<unsigned int>& current_generation, unsigned int start_generation, ThreadPool& thread_pool)
    : max_iterations_(max_iterations),
      start_generation_(start_generation),
      pixels_(static_cast<size_t>(width) * static_cast<size_t>(height) * 4),  // Allocate internal buffer (RGBA)
      metrics_(width, height, x_min, x_max, y_min, y_max),
      thread_pool_(thread_pool),
      current_generation_ref_(&current_generation)
{
    // Initialize buffer to black with alpha=255
    size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::memset(pixels_.data(), 0, pixel_count);
    for (size_t i = 3; i < pixel_count; i += 4)
    {
        pixels_[i] = 255;  // Alpha
    }

    DEBUG_PRINTF("[RENDERER] Constructor: this=%p, start_gen=%u\n", static_cast<void*>(this), start_generation);
}

void MandelbrotRenderer::start()
{
    // Start rendering immediately
    DEBUG_PRINTF("[RENDERER] start: this=%p, start_gen=%u\n", static_cast<void*>(this), start_generation_);

    // Resume thread pool
    thread_pool_.resume();

    // Use thread pool for parallel generation
    // Worker ensures renderer outlives all tasks by waiting for completion
    // But since we use shared_from_this, tasks hold a reference to us, keeping us alive
    std::shared_ptr<MandelbrotRenderer> self = shared_from_this();
    std::atomic<unsigned int>* gen_ref = current_generation_ref_;
    ThreadPool* thread_pool_ptr = &thread_pool_;  // Capture pointer to thread pool
    unsigned int start_gen = start_generation_;
    int width = metrics_.width;
    int height = metrics_.height;

    DEBUG_PRINTF("[RENDERER] Adding initial task: renderer=%p, start_gen=%u\n", static_cast<void*>(self.get()), start_gen);
    thread_pool_ptr->add_task(
        [self, gen_ref, start_gen, width, height]()
        {
            DEBUG_PRINTF("[TASK] Initial task started: renderer=%p, start_gen=%u\n", static_cast<void*>(self.get()), start_gen);
            unsigned int current = gen_ref->load();
            if (start_gen == current)
            {
                self->generate_mandelbrot_recurse(0, width - 1, 0, height - 1, start_gen, current);
            }
            DEBUG_PRINTF("[TASK] Initial task completed: renderer=%p\n", static_cast<void*>(self.get()));
        });
}

MandelbrotRenderer::~MandelbrotRenderer() { DEBUG_PRINTF("[RENDERER] Destructor: this=%p\n", static_cast<void*>(this)); }

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


void MandelbrotRenderer::generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, unsigned int start_generation, unsigned int /*current_generation*/)
{
    // Check if this render has been superseded by a newer one
    unsigned int current_gen_check = current_generation_ref_->load();
    if (start_generation != current_gen_check)
    {
        return;  // Stale render, exit early
    }

    // Optimize: precompute frequently used values outside loops
    const FloatType pixel_to_x = metrics_.pixel_to_x;
    const FloatType pixel_to_y = metrics_.pixel_to_y;
    const FloatType x_min_coord = metrics_.x_min;
    // Use y_max as base because pixel_to_y is negative (Top-Down)
    const FloatType y_base_coord = metrics_.y_max;
    const int max_iter = max_iterations_;
    const ColorScheme& palette = ColorScheme::get_instance();
    const size_t palette_size = palette.palette.size();
    const size_t width_4 = static_cast<size_t>(metrics_.width) * 4;  // Use size_t to prevent overflow
    const FloatType escape_radius_sq = static_cast<FloatType>(4.0);
    const size_t pixels_size = static_cast<size_t>(metrics_.width) * static_cast<size_t>(metrics_.height) * 4;

    // Precompute row base offsets to avoid repeated multiplication
    for (int32_t y_pos = y_min; y_pos <= y_max; ++y_pos)
    {
        // Check generation periodically to allow early bailout
        if (y_pos % 16 == 0)  // Check every 16 rows
        {
            unsigned int current_gen_check_periodic = current_generation_ref_->load();
            if (start_generation != current_gen_check_periodic)
            {
                return;  // Stale render, exit early
            }
        }

        // Calculate row_base using size_t to prevent overflow
        const size_t row_base = static_cast<size_t>(y_pos) * width_4;
        const FloatType cy = y_base_coord + pixel_to_y * y_pos;

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
            const ColorScheme::Color& color = iter == max_iter ? ColorScheme::black : palette.palette[static_cast<size_t>(iter) % palette_size];
            // Calculate idx using size_t to prevent overflow
            size_t idx = row_base + static_cast<size_t>(x_pos) * 4;

            // Bounds check: ensure we can write 4 bytes (idx through idx+3)
            if (idx + 3 < pixels_size)
            {
                // Write pixel directly - generation check already done, no mutex needed
                // UI doesn't block on pixel writes; generation check bails out stale renders
                pixels_[idx + 0] = color[0];
                pixels_[idx + 1] = color[1];
                pixels_[idx + 2] = color[2];
                // Alpha channel (idx + 3) already 255, no need to set
            }
        }
    }
}

void MandelbrotRenderer::generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, unsigned int start_generation, unsigned int current_generation)
{
    // Check if this render has been superseded by a newer one
    if (start_generation != current_generation)
    {
        DEBUG_PRINTF("[RENDERER] Recurse bailed: start_gen=%u != current_gen=%u, renderer=%p\n", start_generation, current_generation, static_cast<void*>(this));
        return;  // Stale render, exit early
    }

    // CRITICAL: Capture gen_ref at the start to avoid accessing current_generation_ref_ after renderer might be destroyed
    std::atomic<unsigned int>* gen_ref = current_generation_ref_;

    // Precompute frequently used values to avoid repeated member access
    const FloatType pixel_to_x = metrics_.pixel_to_x;
    const FloatType pixel_to_y = metrics_.pixel_to_y;
    const FloatType x_min_coord = metrics_.x_min;
    // Use y_max as base because pixel_to_y is negative (Top-Down)
    const FloatType y_base_coord = metrics_.y_max;
    const int max_iter = max_iterations_;
    const size_t width_4 = static_cast<size_t>(metrics_.width) * 4;  // Use size_t to prevent overflow

    // Capture size and dimensions for bounds checking
    size_t pixels_size = pixels_.size();
    int width = metrics_.width;
    int height = metrics_.height;

    // Capture this pointer (shared) - keeps renderer alive
    std::shared_ptr<MandelbrotRenderer> self = shared_from_this();
    unsigned char* pixels_data_captured = pixels_.data();  // Capture pointer once, at creation time
    ThreadPool* thread_pool_ptr = &thread_pool_;           // Capture pointer to thread pool
    const ColorScheme& palette_captured = ColorScheme::get_instance();
    auto fast_process_pixel =
        [self, pixels_data_captured, pixels_size, pixel_to_x, pixel_to_y, x_min_coord, y_base_coord, max_iter, &palette_captured, width_4, width, height, start_generation, gen_ref](
            int32_t x_pos, int32_t y_pos) -> int
    {
        // Use captured pixels_data pointer - no need to access self->pixels_.data() again
        // This avoids the crash if self becomes invalid
        unsigned char* pixels_data = pixels_data_captured;
        if (pixels_data == nullptr || pixels_size < 4)
        {
            return max_iter;
        }

        FloatType cx = x_min_coord + pixel_to_x * x_pos;
        FloatType cy = y_base_coord + pixel_to_y * y_pos;

        FloatType zr = static_cast<FloatType>(0.0);
        FloatType zi = static_cast<FloatType>(0.0);
        int iter = max_iter;
        constexpr FloatType escape_radius_sq = static_cast<FloatType>(4.0);

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

        const ColorScheme::Color& color = iter == max_iter ? ColorScheme::black : palette_captured.palette[static_cast<size_t>(iter) % palette_captured.palette.size()];

        // Calculate index with bounds checking
        if (x_pos < 0 || y_pos < 0 || x_pos >= width || y_pos >= height)
        {
            return iter;  // Out of bounds, return without writing
        }

        // Check generation again before writing - if a new render started, bail out
        unsigned int current_gen = gen_ref->load();
        if (start_generation != current_gen)
        {
            return iter;  // Stale render, exit early
        }

        // Use the captured pixels_data - no need to re-access through self
        // This avoids the crash if self becomes invalid
        // The pixels_data was captured when the lambda was created, so it's safe to use
        if (pixels_data == nullptr || pixels_size < 4)
        {
            return iter;  // Buffer invalid or too small, cannot write
        }

        // Calculate index using size_t arithmetic to prevent overflow
        // width_4 is already size_t, so this calculation is safe
        // y_pos and x_pos are already validated to be within [0, width) and [0, height)
        size_t idx = static_cast<size_t>(y_pos) * width_4 + static_cast<size_t>(x_pos) * 4;

        // Bounds check: ensure we can write 4 bytes (idx through idx+3)
        // Calculate maximum safe index: pixels_size - 4 (to leave room for 4 bytes)
        // We already checked pixels_size >= 4 above, so this is safe
        const size_t max_safe_idx = pixels_size - 4;
        if (idx <= max_safe_idx)
        {
            // Final generation check RIGHT before write - buffer might have been invalidated
            // This is the last chance to bail out before accessing potentially invalid memory
            unsigned int final_gen_check = gen_ref->load(std::memory_order_acquire);
            if (start_generation != final_gen_check)
            {
                return iter;  // Stale render, exit immediately
            }

            // Double-check pointer validity (defensive programming)
            if (pixels_data == nullptr)
            {
                return iter;  // Buffer invalidated, exit immediately
            }

            // Final bounds check: ensure idx + 3 is within bounds
            // This is redundant but provides extra safety
            if ((idx + 3) < pixels_size)
            {
                // Write pixel directly - all checks passed, safe to write
                // UI doesn't block on pixel writes; generation check bails out stale renders
                if (pixels_data == nullptr)
                {
                    DEBUG_PRINTF("[ERROR] Writing pixel: pixels_data is null! renderer=%p, idx=%zu\n", static_cast<void*>(self.get()), idx);
                    return iter;
                }
                // Write pixel - pixels_data was captured at lambda creation time, so it's safe
                pixels_data[idx + 0] = color[0];
                pixels_data[idx + 1] = color[1];
                pixels_data[idx + 2] = color[2];
                // Alpha channel (idx + 3) already 255, no need to set
#ifdef _DEBUG
                // Debug: log first few pixels to verify writes
                static int pixel_write_count = 0;
                if (pixel_write_count < 10)
                {
                    DEBUG_PRINTF("[RENDERER] Wrote pixel at (%d, %d): RGB=(%u, %u, %u), idx=%zu\n", x_pos, y_pos, color[0], color[1], color[2], idx);
                    pixel_write_count++;
                }
#endif
            }
            else
            {
                DEBUG_PRINTF("[ERROR] Bounds check failed: idx=%zu, pixels_size=%zu, renderer=%p\n", idx, pixels_size, static_cast<void*>(self.get()));
            }
        }

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

    // No callback needed - renderer writes directly to canvas buffer

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

        if (!all_same && thread_pool_ptr->is_paused())
        {
           all_same = true; // we are terminating the recursion, flood fill.
        }

        if (all_same)
        {
            // Flood fill entire interior - optimized using first row as pattern
            // No mutex needed - generation check already done, UI doesn't block on writes
            const ColorScheme& palette = ColorScheme::get_instance();
            const ColorScheme::Color& color = first_iter == max_iter ? ColorScheme::black : palette.palette[static_cast<size_t>(first_iter) % palette.palette.size()];
            const size_t fill_width = static_cast<size_t>(new_x_max - new_x_min + 1);
            const size_t fill_bytes = fill_width * 4;

            // Fill first row efficiently - construct pattern once, then duplicate
            // Alpha channel already 255 from buffer initialization, but we'll include it for memcpy
            // Calculate offset using size_t arithmetic to prevent overflow
            // Access pixels_ directly (we're in a member function, so this is safe)
            size_t first_row_offset = static_cast<size_t>(new_y_min) * width_4 + static_cast<size_t>(new_x_min) * 4;
            if (first_row_offset + fill_bytes > pixels_size)
            {
                return;  // Bounds check failed, exit early
            }
            unsigned char* first_row_start = pixels_.data() + first_row_offset;

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
                // Calculate offset using size_t arithmetic to prevent overflow
                // Access pixels_ directly (we're in a member function, so this is safe)
                size_t row_offset = static_cast<size_t>(y_pos) * width_4 + static_cast<size_t>(new_x_min) * 4;
                if (row_offset + fill_bytes > pixels_size)
                {
                    break;  // Bounds check failed, stop copying
                }
                unsigned char* row_start = pixels_.data() + row_offset;
                std::memcpy(row_start, first_row_start, fill_bytes);
            }

            // No callback needed - renderer writes directly to canvas buffer
        }
        else
        {
            constexpr int recurse_size_limit = 4;
            if (inner_d_x <= recurse_size_limit || inner_d_y <= recurse_size_limit)
            {
                unsigned int current_gen_check_direct = current_generation_ref_->load();
                generate_mandelbrot_direct(new_x_min, new_x_max, new_y_min, new_y_max, start_generation, current_gen_check_direct);

                // No callback needed - renderer writes directly to canvas buffer
            }
            else
            {
                int x_mid = (new_x_min + new_x_max) / 2;
                int y_mid = (new_y_min + new_y_max) / 2;

                // Submit using thread pool
                std::shared_ptr<MandelbrotRenderer> self = shared_from_this();
                std::atomic<unsigned int>* gen_ref = current_generation_ref_;
                ThreadPool* thread_pool_ptr = &thread_pool_;  // Capture pointer to thread pool
                DEBUG_PRINTF("[RENDERER] Adding 4 recursive tasks: renderer=%p\n", static_cast<void*>(self.get()));
                thread_pool_ptr->add_task(
                    [self, gen_ref, new_x_min, x_mid, new_y_min, y_mid, start_generation]()
                    {
                        DEBUG_PRINTF("[TASK] Recursive task 1 started: renderer=%p\n", static_cast<void*>(self.get()));
                        unsigned int current = gen_ref->load();
                        if (start_generation == current)
                        {
                            self->generate_mandelbrot_recurse(new_x_min, x_mid, new_y_min, y_mid, start_generation, current);
                        }
                        DEBUG_PRINTF("[TASK] Recursive task 1 completed: renderer=%p\n", static_cast<void*>(self.get()));
                    });
                thread_pool_ptr->add_task(
                    [self, gen_ref, x_mid, new_x_max, new_y_min, y_mid, start_generation]()
                    {
                        DEBUG_PRINTF("[TASK] Recursive task 2 started: renderer=%p\n", static_cast<void*>(self.get()));
                        unsigned int current = gen_ref->load();
                        if (start_generation == current)
                        {
                            self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, new_y_min, y_mid, start_generation, current);
                        }
                        DEBUG_PRINTF("[TASK] Recursive task 2 completed: renderer=%p\n", static_cast<void*>(self.get()));
                    });
                thread_pool_ptr->add_task(
                    [self, gen_ref, new_x_min, x_mid, y_mid, new_y_max, start_generation]()
                    {
                        DEBUG_PRINTF("[TASK] Recursive task 3 started: renderer=%p\n", static_cast<void*>(self.get()));
                        unsigned int current = gen_ref->load();
                        if (start_generation == current)
                        {
                            self->generate_mandelbrot_recurse(new_x_min, x_mid, y_mid + 1, new_y_max, start_generation, current);
                        }
                        DEBUG_PRINTF("[TASK] Recursive task 3 completed: renderer=%p\n", static_cast<void*>(self.get()));
                    });
                thread_pool_ptr->add_task(
                    [self, gen_ref, x_mid, new_x_max, y_mid, new_y_max, start_generation]()
                    {
                        DEBUG_PRINTF("[TASK] Recursive task 4 started: renderer=%p\n", static_cast<void*>(self.get()));
                        unsigned int current = gen_ref->load();
                        if (start_generation == current)
                        {
                            self->generate_mandelbrot_recurse(x_mid + 1, new_x_max, y_mid + 1, new_y_max, start_generation, current);
                        }
                        DEBUG_PRINTF("[TASK] Recursive task 4 completed: renderer=%p\n", static_cast<void*>(self.get()));
                    });
            }
        }
    }

    // No callback needed - renderer writes directly to canvas buffer
}


}  // namespace mandel
