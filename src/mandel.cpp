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

// Hot Mandelbrot pixel computation, templated on the arithmetic type.
//
// T = double  at shallow/medium zoom (SSE2, 2-5× faster than x87 long double)
// T = FloatType (long double) at deep zoom for full 80-bit precision
//
// Optimisations vs the original inlined loop:
//   1. Period-2 bulb and main cardioid early-exit: pixels inside the main body
//      are classified in ~10 ns instead of max_iter × ~5 ns.
//   2. zr² and zi² are computed once per step and reused for both the escape
//      test and the next iterate — saves two multiplications per iteration.
template<typename T>
[[nodiscard]] static inline int compute_pixel_iter(T cx, T cy, int max_iter)
{
    // Period-2 bulb: centre (-1, 0), radius 0.25  →  |c+1|² < 1/16
    T bx = cx + T(1);
    if (bx * bx + cy * cy < T(0.0625)) return max_iter;

    // Main cardioid (sqrt-free form):
    //   dx = cx - 0.25,  q = dx² + cy²
    //   c is inside the cardioid iff  q·(q + dx) < ¼·cy²
    T dx = cx - T(0.25);
    T q  = dx * dx + cy * cy;
    if (q * (q + dx) < T(0.25) * cy * cy) return max_iter;

    // Escape-time loop — reuse zr², zi² to save two multiplications per step
    T zr = 0, zi = 0;
    for (int i = 0; i < max_iter; ++i)
    {
        T zr2 = zr * zr, zi2 = zi * zi;
        if (zr2 + zi2 > T(4)) return i;
        zi = T(2) * zr * zi + cy;
        zr = zr2 - zi2 + cx;
    }
    return max_iter;
}

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
    if (start_generation != current_generation_ref_->load()) return;

    const FloatType px   = metrics_.pixel_to_x;
    const FloatType py   = metrics_.pixel_to_y;
    const FloatType xc   = metrics_.x_min;
    const FloatType yb   = metrics_.y_max;
    const int       mi   = max_iterations_;
    const size_t    w4   = static_cast<size_t>(metrics_.width) * 4;
    unsigned char*  pd   = pixels_.data();

    const ColorScheme& pal  = ColorScheme::get_instance();
    const size_t  pal_mask  = pal.palette.size() - 1;  // palette size is always 256 (power-of-2)

    // Dispatch to double (SSE2) for shallow/medium zoom, long double (x87) for deep zoom.
    // At pixel_to_x > 1e-7 (zoom < ~4×10^7) double precision is more than sufficient
    // and avoids the x87 pipeline which is 2-5× slower than SSE2 on modern CPUs.
    if (px > FloatType(1e-7))
    {
        const double px_d = static_cast<double>(px);
        const double py_d = static_cast<double>(py);
        const double xc_d = static_cast<double>(xc);
        const double yb_d = static_cast<double>(yb);
        for (int32_t y = y_min; y <= y_max; ++y)
        {
            if (y % 16 == 0 && start_generation != current_generation_ref_->load()) return;
            const size_t row = static_cast<size_t>(y) * w4;
            const double cy  = yb_d + py_d * y;
            for (int32_t x = x_min; x <= x_max; ++x)
            {
                const int iter = compute_pixel_iter<double>(xc_d + px_d * x, cy, mi);
                const ColorScheme::Color& c = (iter == mi) ? ColorScheme::black : pal.palette[static_cast<size_t>(iter) & pal_mask];
                const size_t idx = row + static_cast<size_t>(x) * 4;
                pd[idx]     = c[0];
                pd[idx + 1] = c[1];
                pd[idx + 2] = c[2];
            }
        }
    }
    else
    {
        for (int32_t y = y_min; y <= y_max; ++y)
        {
            if (y % 16 == 0 && start_generation != current_generation_ref_->load()) return;
            const size_t    row = static_cast<size_t>(y) * w4;
            const FloatType cy  = yb + py * y;
            for (int32_t x = x_min; x <= x_max; ++x)
            {
                const int iter = compute_pixel_iter<FloatType>(xc + px * x, cy, mi);
                const ColorScheme::Color& c = (iter == mi) ? ColorScheme::black : pal.palette[static_cast<size_t>(iter) & pal_mask];
                const size_t idx = row + static_cast<size_t>(x) * 4;
                pd[idx]     = c[0];
                pd[idx + 1] = c[1];
                pd[idx + 2] = c[2];
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

    // Capture this pointer (shared) - keeps renderer alive for the duration of all tasks
    std::shared_ptr<MandelbrotRenderer> self = shared_from_this();
    unsigned char* pixels_data_captured = pixels_.data();
    ThreadPool* thread_pool_ptr = &thread_pool_;
    const ColorScheme& palette_captured = ColorScheme::get_instance();

    // Still needed by the flood-fill bounds check below
    const size_t pixels_size = pixels_.size();

    // Double-dispatch setup: use SSE2 double at shallow zoom, x87 long double at deep zoom
    const bool   use_double = pixel_to_x > FloatType(1e-7);
    const double ptx_d = static_cast<double>(pixel_to_x);
    const double pty_d = static_cast<double>(pixel_to_y);
    const double xc_d  = static_cast<double>(x_min_coord);
    const double yb_d  = static_cast<double>(y_base_coord);
    const size_t pal_mask = palette_captured.palette.size() - 1;  // 255, power-of-2 bitmask

    auto fast_process_pixel =
        [pixels_data_captured, pixel_to_x, pixel_to_y, x_min_coord, y_base_coord,
         max_iter, &palette_captured, pal_mask, width_4,
         start_generation, gen_ref,
         use_double, ptx_d, pty_d, xc_d, yb_d](int32_t x_pos, int32_t y_pos) -> int
    {
        // One generation check per pixel — provides responsive cancellation without
        // the 3× per-pixel atomic loads the previous implementation had.
        if (gen_ref->load(std::memory_order_relaxed) != start_generation) return max_iter;

        int iter;
        if (use_double)
            iter = compute_pixel_iter<double>(xc_d + ptx_d * x_pos, yb_d + pty_d * y_pos, max_iter);
        else
            iter = compute_pixel_iter<FloatType>(x_min_coord + pixel_to_x * x_pos,
                                                  y_base_coord + pixel_to_y * y_pos, max_iter);

        const ColorScheme::Color& color = (iter == max_iter)
            ? ColorScheme::black
            : palette_captured.palette[static_cast<size_t>(iter) & pal_mask];
        const size_t idx = static_cast<size_t>(y_pos) * width_4 + static_cast<size_t>(x_pos) * 4;
        pixels_data_captured[idx]     = color[0];
        pixels_data_captured[idx + 1] = color[1];
        pixels_data_captured[idx + 2] = color[2];
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
            // If the entire Mandelbrot set is inside the view, the interior can have different iteration
            // counts (e.g. main cardioid, period-2 bulb, interior "atoms") even when the border is
            // uniform. Do not use the flood-fill shortcut in that case.
            // Mandelbrot set is contained in ~[-2.25, 0.75] x [-1.25, 1.25]
            // c_min = top-left (min real, max imag), c_max = bottom-right (max real, min imag)
            auto c_min = metrics_.canvas_to_complex(x_min, y_min);
            auto c_max = metrics_.canvas_to_complex(x_max, y_max);
            constexpr FloatType set_left = -2.25, set_right = 0.75, set_bottom = -1.25, set_top = 1.25;
            bool entire_set_inside = (c_min.real() <= set_left && c_max.real() >= set_right &&
                                     c_min.imag() >= set_top && c_max.imag() <= set_bottom);
            if (entire_set_inside)
                all_same = false;
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
