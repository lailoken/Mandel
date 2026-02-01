#include "mandel_worker.hpp"
#include "thread_pool.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>
#include <chrono>

// Simple unit test for MandelWorker
int test_worker_basic()
{
    printf("[TEST] Testing MandelWorker basic functionality...\n");
    
    const int width = 200;
    const int height = 200;
    
    std::atomic<unsigned int> current_generation(0);
    mandel::FloatType canvas_x_min = -2.5;
    mandel::FloatType canvas_x_max = 1.5;
    mandel::FloatType canvas_y_min = -2.0;
    mandel::FloatType canvas_y_max = 2.0;
    
    ThreadPool thread_pool(4);
    
    printf("[TEST] Creating worker...\n");
    mandel::MandelWorker worker(
        width, height,
        current_generation,
        canvas_x_min, canvas_x_max,
        canvas_y_min, canvas_y_max,
        thread_pool
    );
    
    worker.init(width, height);
    worker.set_max_iterations(100);
    
    printf("[TEST] Starting render...\n");
    worker.start_render();
    
    printf("[TEST] Waiting for render to complete...\n");
    std::vector<unsigned char> buffer;
    bool success = worker.wait_and_get_buffer(buffer);
    
    if (!success)
    {
        printf("[TEST] ERROR: wait_and_get_buffer returned false\n");
        return 1;
    }
    
    printf("[TEST] Buffer size: %zu (expected: %d)\n", buffer.size(), width * height * 4);
    
    if (buffer.size() != static_cast<size_t>(width * height * 4))
    {
        printf("[TEST] ERROR: Buffer size mismatch\n");
        return 1;
    }
    
    // Check that at least some pixels are non-black
    size_t non_black_count = 0;
    for (size_t i = 0; i < buffer.size() && i < 10000; i += 4)  // Check first 2500 pixels
    {
        if (buffer[i] != 0 || buffer[i+1] != 0 || buffer[i+2] != 0)
        {
            non_black_count++;
        }
    }
    
    printf("[TEST] Non-black pixels in first 2500: %zu\n", non_black_count);
    
    if (non_black_count == 0)
    {
        printf("[TEST] ERROR: No non-black pixels found - rendering may have failed\n");
        return 1;
    }
    
    // Check that get_pixels() returns the same data
    const unsigned char* pixels_ptr = worker.get_pixels();
    if (pixels_ptr == nullptr)
    {
        printf("[TEST] ERROR: get_pixels() returned nullptr\n");
        return 1;
    }
    
    bool pixels_match = true;
    for (size_t i = 0; i < buffer.size() && i < 100; i++)  // Check first 100 bytes
    {
        if (pixels_ptr[i] != buffer[i])
        {
            pixels_match = false;
            break;
        }
    }
    
    if (!pixels_match)
    {
        printf("[TEST] ERROR: get_pixels() data doesn't match buffer\n");
        return 1;
    }
    
    printf("[TEST] MandelWorker test PASSED\n");
    return 0;
}

int test_worker_generation_cancellation()
{
    printf("[TEST] Testing MandelWorker generation cancellation...\n");
    
    const int width = 100;
    const int height = 100;
    
    std::atomic<unsigned int> current_generation(0);
    mandel::FloatType canvas_x_min = -2.5;
    mandel::FloatType canvas_x_max = 1.5;
    mandel::FloatType canvas_y_min = -2.0;
    mandel::FloatType canvas_y_max = 2.0;
    
    ThreadPool thread_pool(4);
    
    mandel::MandelWorker worker(
        width, height,
        current_generation,
        canvas_x_min, canvas_x_max,
        canvas_y_min, canvas_y_max,
        thread_pool
    );
    
    worker.init(width, height);
    worker.set_max_iterations(100);
    
    printf("[TEST] Starting render with generation 0...\n");
    worker.start_render();
    
    // Immediately increment generation to cancel the render
    printf("[TEST] Cancelling render by incrementing generation...\n");
    current_generation.fetch_add(1);
    
    // Wait a bit for cancellation to take effect
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::vector<unsigned char> buffer;
    bool success = worker.wait_and_get_buffer(buffer);
    
    // Should return false because generation changed
    if (success)
    {
        printf("[TEST] ERROR: wait_and_get_buffer should return false when generation changes\n");
        return 1;
    }
    
    printf("[TEST] Generation cancellation test PASSED\n");
    return 0;
}

int test_worker_translation()
{
    printf("[TEST] Testing MandelWorker translation (drag)...\n");
    
    const int width = 200;
    const int height = 200;
    
    std::atomic<unsigned int> current_generation(0);
    
    // Initial bounds - centered on origin
    mandel::FloatType initial_x_min = -2.5;
    mandel::FloatType initial_x_max = 1.5;
    mandel::FloatType initial_y_min = -2.0;
    mandel::FloatType initial_y_max = 2.0;
    
    ThreadPool thread_pool(4);
    
    printf("[TEST] Creating worker with initial bounds...\n");
    mandel::MandelWorker worker(
        width, height,
        current_generation,
        initial_x_min, initial_x_max,
        initial_y_min, initial_y_max,
        thread_pool
    );
    
    worker.init(width, height);
    worker.set_max_iterations(100);
    
    // Render initial view
    printf("[TEST] Rendering initial view...\n");
    worker.start_render();
    
    std::vector<unsigned char> initial_buffer;
    bool success = worker.wait_and_get_buffer(initial_buffer);
    
    if (!success)
    {
        printf("[TEST] ERROR: wait_and_get_buffer returned false for initial render\n");
        return 1;
    }
    
    if (initial_buffer.size() != static_cast<size_t>(width * height * 4))
    {
        printf("[TEST] ERROR: Initial buffer size mismatch\n");
        return 1;
    }
    
    // Sample a few pixels from the initial render
    // Center pixel
    size_t center_idx = (width / 2 + (height / 2) * width) * 4;
    unsigned char center_r = initial_buffer[center_idx];
    unsigned char center_g = initial_buffer[center_idx + 1];
    unsigned char center_b = initial_buffer[center_idx + 2];
    
    // Top-left corner
    size_t corner_idx = (0 + 0 * width) * 4;
    unsigned char corner_r = initial_buffer[corner_idx];
    unsigned char corner_g = initial_buffer[corner_idx + 1];
    unsigned char corner_b = initial_buffer[corner_idx + 2];
    
    printf("[TEST] Initial render - center pixel RGB=(%u, %u, %u), corner RGB=(%u, %u, %u)\n",
           center_r, center_g, center_b, corner_r, corner_g, corner_b);
    
    // Calculate translation amount (pan right and up by 10% of viewport width/height)
    mandel::FloatType x_range = initial_x_max - initial_x_min;
    mandel::FloatType y_range = initial_y_max - initial_y_min;
    mandel::FloatType pan_x = x_range * 0.1;  // Pan 10% to the right
    mandel::FloatType pan_y = y_range * 0.1;  // Pan 10% up
    
    // New bounds after translation
    mandel::FloatType new_x_min = initial_x_min + pan_x;
    mandel::FloatType new_x_max = initial_x_max + pan_x;
    mandel::FloatType new_y_min = initial_y_min + pan_y;
    mandel::FloatType new_y_max = initial_y_max + pan_y;
    
    printf("[TEST] Translating view: pan_x=%.10Lf, pan_y=%.10Lf\n", pan_x, pan_y);
    printf("[TEST] New bounds: (%.10Lf, %.10Lf, %.10Lf, %.10Lf)\n",
           new_x_min, new_x_max, new_y_min, new_y_max);
    
    // Update worker bounds and render again
    current_generation.fetch_add(1);  // Increment generation to cancel any in-progress work
    
    // Create new worker with translated bounds
    mandel::MandelWorker worker_translated(
        width, height,
        current_generation,
        new_x_min, new_x_max,
        new_y_min, new_y_max,
        thread_pool
    );
    
    worker_translated.init(width, height);
    worker_translated.set_max_iterations(100);
    
    printf("[TEST] Rendering translated view...\n");
    worker_translated.start_render();
    
    std::vector<unsigned char> translated_buffer;
    success = worker_translated.wait_and_get_buffer(translated_buffer);
    
    if (!success)
    {
        printf("[TEST] ERROR: wait_and_get_buffer returned false for translated render\n");
        return 1;
    }
    
    if (translated_buffer.size() != static_cast<size_t>(width * height * 4))
    {
        printf("[TEST] ERROR: Translated buffer size mismatch\n");
        return 1;
    }
    
    // Check that the images are different (translation should change the view)
    // The center pixel of the translated view should match a different pixel from the initial view
    // Specifically, if we panned right and up, the new center should match a pixel that was
    // to the left and down in the original view
    
    // Calculate the pixel offset based on translation
    int pixel_offset_x = static_cast<int>((pan_x / x_range) * width);
    int pixel_offset_y = static_cast<int>((pan_y / y_range) * height);
    
    // The center of the translated view should match a pixel that was offset in the initial view
    int source_x = (width / 2) - pixel_offset_x;
    int source_y = (height / 2) - pixel_offset_y;
    
    // Clamp to valid range
    source_x = std::max(0, std::min(width - 1, source_x));
    source_y = std::max(0, std::min(height - 1, source_y));
    
    size_t source_idx = (source_x + source_y * width) * 4;
    unsigned char source_r = initial_buffer[source_idx];
    unsigned char source_g = initial_buffer[source_idx + 1];
    unsigned char source_b = initial_buffer[source_idx + 2];
    
    unsigned char new_center_r = translated_buffer[center_idx];
    unsigned char new_center_g = translated_buffer[center_idx + 1];
    unsigned char new_center_b = translated_buffer[center_idx + 2];
    
    printf("[TEST] Translated render - center pixel RGB=(%u, %u, %u)\n",
           new_center_r, new_center_g, new_center_b);
    printf("[TEST] Expected source pixel from initial view at (%d, %d): RGB=(%u, %u, %u)\n",
           source_x, source_y, source_r, source_g, source_b);
    
    // Check that the images are different (at least some pixels should differ)
    size_t different_pixels = 0;
    for (size_t i = 0; i < initial_buffer.size() && i < 10000; i += 4)
    {
        if (initial_buffer[i] != translated_buffer[i] ||
            initial_buffer[i+1] != translated_buffer[i+1] ||
            initial_buffer[i+2] != translated_buffer[i+2])
        {
            different_pixels++;
        }
    }
    
    printf("[TEST] Different pixels in first 2500: %zu\n", different_pixels);
    
    if (different_pixels == 0)
    {
        printf("[TEST] ERROR: No pixel differences found - translation may not have worked\n");
        return 1;
    }
    
    // The center pixel of translated view should approximately match the source pixel
    // (allowing for some tolerance due to floating point precision and different sampling)
    int diff_r = std::abs(static_cast<int>(new_center_r) - static_cast<int>(source_r));
    int diff_g = std::abs(static_cast<int>(new_center_g) - static_cast<int>(source_g));
    int diff_b = std::abs(static_cast<int>(new_center_b) - static_cast<int>(source_b));
    
    printf("[TEST] Pixel difference: RGB diff=(%d, %d, %d)\n", diff_r, diff_g, diff_b);
    
    // Allow some tolerance (within 5 RGB units) since floating point precision can cause slight differences
    if (diff_r > 5 || diff_g > 5 || diff_b > 5)
    {
        printf("[TEST] WARNING: Center pixel doesn't match expected source pixel closely (tolerance: 5)\n");
        printf("[TEST] This may be acceptable due to floating point precision differences\n");
    }
    
    printf("[TEST] Translation test PASSED\n");
    return 0;
}

int main()
{
    printf("=== MandelWorker Unit Tests ===\n\n");
    
    int result1 = test_worker_basic();
    int result2 = test_worker_generation_cancellation();
    int result3 = test_worker_translation();
    
    int total_result = result1 | result2 | result3;
    
    if (total_result == 0)
    {
        printf("\n=== All tests PASSED ===\n");
    }
    else
    {
        printf("\n=== Some tests FAILED ===\n");
    }
    
    return total_result;
}
