#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

#include "thread_pool.hpp"

namespace mandel
{

// Use long double as the underlying floating point type
using FloatType = long double;

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

    // Singleton access - thread-safe initialization
    static const ColorScheme& get_instance();
};

class MandelbrotRenderer : public std::enable_shared_from_this<MandelbrotRenderer>
{
public:
   // Constructor takes all parameters. Does NOT start rendering immediately (call start() for that).
   MandelbrotRenderer(int width, int height, FloatType x_min, FloatType x_max, FloatType y_min, FloatType y_max, int max_iterations, std::atomic<unsigned int>& current_generation,
                      unsigned int start_generation, ThreadPool& thread_pool);
   ~MandelbrotRenderer();

   // Start the rendering process (must be called after constructor)
   void start();

   static constexpr FloatType default_x_min = static_cast<FloatType>(-2.0);
   static constexpr FloatType default_x_max = static_cast<FloatType>(0.5);
   static constexpr FloatType default_y_min = static_cast<FloatType>(-1.125);
   static constexpr FloatType default_y_max = static_cast<FloatType>(1.125);

   // Maximum zoom level based on floating point type precision
   static constexpr FloatType max_zoom = max_zoom_v;

   // Get the completed pixel buffer (worker calls this after renderer completes)
   const std::vector<unsigned char>& get_pixels() const { return pixels_; }

private:
   // Internal methods
   int compute_mandelbrot(::std::complex<FloatType> c, int max_iter) const;
   void generate_mandelbrot_direct(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, unsigned int start_generation, unsigned int current_generation);
   void generate_mandelbrot_recurse(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max, unsigned int start_generation, unsigned int current_generation);

   // Member variables
   const int max_iterations_;
   const unsigned int start_generation_;

   std::vector<unsigned char> pixels_;  // Own internal buffer (RGBA, size = metrics_.width * metrics_.height * 4)
   const CanvasMetrics metrics_;

   ThreadPool& thread_pool_;                            // Reference to thread pool (always set)
   std::atomic<unsigned int>* const current_generation_ref_;  // Reference to current generation
};

}  // namespace mandel
