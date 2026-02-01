#include "mandel.hpp"
#include "thread_pool.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

// Simple unit test for MandelbrotRenderer
int test_renderer_basic()
{
    printf("[TEST] Testing MandelbrotRenderer basic functionality...\n");
    
    const int width = 100;
    const int height = 100;
    const mandel::FloatType x_min = -2.0L;
    const mandel::FloatType x_max = 1.0L;
    const mandel::FloatType y_min = -1.5L;
    const mandel::FloatType y_max = 1.5L;
    const int max_iterations = 100;
    
    std::atomic<unsigned int> current_generation(0);
    ThreadPool thread_pool(4);
    
    printf("[TEST] Creating renderer...\n");
    mandel::MandelbrotRenderer renderer(
        width, height,
        x_min, x_max, y_min, y_max,
        max_iterations,
        current_generation,
        0,  // start_generation
        thread_pool
    );
    
    printf("[TEST] Waiting for render to complete...\n");
    // Wait for thread pool to be idle
    int wait_count = 0;
    while (!thread_pool.is_idle())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
        if (wait_count > 1000)  // 10 second timeout
        {
            printf("[TEST] ERROR: Timeout waiting for render to complete\n");
            return 1;
        }
    }
    printf("[TEST] Render completed after %d ms\n", wait_count * 10);
    
    // Check that pixels were written
    const std::vector<unsigned char>& pixels = renderer.get_pixels();
    printf("[TEST] Pixel buffer size: %zu (expected: %d)\n", pixels.size(), width * height * 4);
    
    if (pixels.size() != static_cast<size_t>(width * height * 4))
    {
        printf("[TEST] ERROR: Pixel buffer size mismatch\n");
        return 1;
    }
    
    // Check that at least some pixels are non-black (Mandelbrot set should have some colored pixels)
    size_t non_black_count = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        if (pixels[i] != 0 || pixels[i+1] != 0 || pixels[i+2] != 0)
        {
            non_black_count++;
        }
    }
    
    printf("[TEST] Non-black pixels: %zu out of %zu total pixels\n", non_black_count, pixels.size() / 4);
    
    if (non_black_count == 0)
    {
        printf("[TEST] ERROR: No non-black pixels found - rendering may have failed\n");
        return 1;
    }
    
    // Check center pixel (should be in the set, so black)
    size_t center_idx = (width / 2 + (height / 2) * width) * 4;
    printf("[TEST] Center pixel (%d, %d): RGB=(%u, %u, %u)\n",
           width/2, height/2,
           pixels[center_idx], pixels[center_idx+1], pixels[center_idx+2]);
    
    // Check corner pixel (should be outside set, so colored)
    size_t corner_idx = (0 + 0 * width) * 4;
    printf("[TEST] Corner pixel (0, 0): RGB=(%u, %u, %u)\n",
           pixels[corner_idx], pixels[corner_idx+1], pixels[corner_idx+2]);
    
    printf("[TEST] MandelbrotRenderer test PASSED\n");
    return 0;
}

int main()
{
    printf("=== MandelbrotRenderer Unit Tests ===\n\n");
    
    int result = test_renderer_basic();
    
    if (result == 0)
    {
        printf("\n=== All tests PASSED ===\n");
    }
    else
    {
        printf("\n=== Tests FAILED ===\n");
    }
    
    return result;
}
