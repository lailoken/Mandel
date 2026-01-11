#pragma once

#include <cstdint>
#include <vector>
#include <complex>
#include <array>
#include <type_traits>

#include "thread_pool.hpp"

namespace mandel
{

// Render callback interface (defined here to avoid circular dependencies)
struct RenderCallback
{
    virtual ~RenderCallback() = default;
    // Called when pixels need to be rendered/updated
    virtual void on_pixels_updated(const unsigned char* pixels, int width, int height) = 0;
};

// Type trait to determine max zoom based on floating point type
template<typename FloatType>
struct max_zoom_trait;

template<>
struct max_zoom_trait<float>
{
    static constexpr float value = 1000000.0f;  // Lower precision for float (1e6)
};

template<>
struct max_zoom_trait<double>
{
    static constexpr double value = 70000000000000.0;  // Tested maximum before visible errors: 70113537556480, rounded to 7e13
};

template<>
struct max_zoom_trait<long double>
{
    static constexpr long double value = 100000000000000000.0L;  // Higher precision for long double (7e18)
};

template <typename FloatType>
inline constexpr FloatType max_zoom_v = max_zoom_trait<FloatType>::value;

// Internal structures
template<typename FloatType>
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

template<typename FloatType>
class MandelbrotRenderer
{
public:
    static_assert(std::is_floating_point_v<FloatType>, "FloatType must be a floating point type (float, double, or long double)");

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
    static constexpr FloatType max_zoom = max_zoom_v<FloatType>;

 private:
    // Internal methods
    int compute_mandelbrot(::std::complex<FloatType> c, int max_iter) const;
    void paint_pixel(int x_pos, int y_pos, const ColorScheme::Color& color);
    int process_pixel(int32_t x_pos, int32_t y_pos);
    void generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max);
    void generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, ThreadPool* thread_pool = nullptr);
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
    CanvasMetrics<FloatType> metrics_;

    ThreadPool thread_pool_;
    RenderCallback* render_callback_;
};

// Common type aliases
using MandelbrotRendererFloat = MandelbrotRenderer<float>;
using MandelbrotRendererDouble = MandelbrotRenderer<double>;
using MandelbrotRendererLongDouble = MandelbrotRenderer<long double>;

}  // namespace mandel

// Include template implementation
#include "mandel.inl"
