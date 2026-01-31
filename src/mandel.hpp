#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <complex>
#include <array>

#include "thread_pool.hpp"

namespace mandel
{

// Use long double as the underlying floating point type
using FloatType = long double;

// Render callback interface (defined here to avoid circular dependencies)
struct RenderCallback
{
    virtual ~RenderCallback() = default;
    // Called when pixels need to be rendered/updated
    virtual void on_pixels_updated(const unsigned char* pixels, int width, int height) = 0;
};

// Maximum zoom level for long double precision
inline constexpr FloatType max_zoom_v = 1000000000000000.0L;  // Higher precision for long double (1e15)

// Internal structures
struct CanvasMetrics
{
    int width;
    int height;
    FloatType x_min;
    FloatType x_max;
    FloatType y_min;
    FloatType y_max;
    FloatType pixel_to_x;
    FloatType pixel_to_y;

    CanvasMetrics(int w, int h, FloatType xmin, FloatType xmax, FloatType ymin, FloatType ymax);
    ::std::complex<FloatType> canvas_to_complex(int32_t x_pos, int32_t y_pos) const;
};

struct ColorScheme
{
    using Color = ::std::array<::std::uint8_t, 3>;
    ::std::vector<Color> palette;

    explicit ColorScheme(int count = 256);
    static constexpr Color black{0, 0, 0};
};

class MandelbrotRenderer
{
public:
    MandelbrotRenderer(int width = 800, int height = 600);
    ~MandelbrotRenderer() = default;

    // Initialize with dimensions
    // If thread_pool is nullptr, generates synchronously (single-threaded)
    // If thread_pool is provided, uses that thread pool for parallel generation
    void init(int width, int height, ThreadPool* thread_pool = nullptr);

    // Set render callback (called when pixels are updated)
    void set_render_callback(RenderCallback* callback);

    // Regenerate the Mandelbrot set (call when parameters change)
    // If thread_pool is nullptr, generates synchronously (single-threaded)
    // If thread_pool is provided, uses that thread pool for parallel generation
    // If pan_dx and pan_dy are provided, shifts existing pixels and only regenerates exposed regions
    void regenerate(ThreadPool* thread_pool = nullptr, int pan_dx = 0, int pan_dy = 0);

    // Update parameters
    void set_max_iterations(int max_iterations) { max_iterations_ = max_iterations; }
    void set_bounds(FloatType x_min, FloatType x_max, FloatType y_min, FloatType y_max) { x_min_ = x_min; x_max_ = x_max; y_min_ = y_min; y_max_ = y_max; }
    void set_zoom(FloatType zoom) { zoom_ = zoom; }

    int get_max_iterations() const { return max_iterations_; }
    FloatType get_x_min() const { return x_min_; }
    FloatType get_x_max() const { return x_max_; }
    FloatType get_y_min() const { return y_min_; }
    FloatType get_y_max() const { return y_max_; }
    FloatType get_zoom() const { return zoom_; }

    // Get pixel buffer
    const unsigned char* get_pixels() const { return pixels_.data(); }
    int get_width() const { return width_; }
    int get_height() const { return height_; }

    // Get thread pool (for checking completion status)
    ThreadPool* get_thread_pool() { return &thread_pool_; }
    const ThreadPool* get_thread_pool() const { return &thread_pool_; }

    static constexpr FloatType default_x_min = static_cast<FloatType>(-2.5);
    static constexpr FloatType default_x_max = static_cast<FloatType>(1.5);
    static constexpr FloatType default_y_min = static_cast<FloatType>(-2.0);
    static constexpr FloatType default_y_max = static_cast<FloatType>(2.0);
    
    // Maximum zoom level based on floating point type precision
    static constexpr FloatType max_zoom = max_zoom_v;

 private:
    // Internal methods
    int compute_mandelbrot(::std::complex<FloatType> c, int max_iter) const;
    void paint_pixel(int x_pos, int y_pos, const ColorScheme::Color& color);
    int process_pixel(int32_t x_pos, int32_t y_pos);
    void generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max);
    void generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, ThreadPool* thread_pool, unsigned int generation);
    void generate_mandelbrot(ThreadPool* thread_pool = nullptr);

    // Member variables
    int width_;
    int height_;
    FloatType x_min_;
    FloatType x_max_;
    FloatType y_min_;
    FloatType y_max_;
    FloatType zoom_;
    int max_iterations_;

    ::std::vector<unsigned char> pixels_;
    ColorScheme palette_;
    CanvasMetrics metrics_;

    ThreadPool thread_pool_;
    RenderCallback* render_callback_;
    
    // Generation counter to track render versions and ignore stale callbacks
    std::atomic<unsigned int> render_generation_{0};
};

}  // namespace mandel
