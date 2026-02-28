#include "mandel.hpp"
#include "thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main()
{
    const int width = 100;
    const int height = 100;
    const mandel::FloatType x_min = -2.0L;
    const mandel::FloatType x_max = 1.0L;
    const mandel::FloatType y_min = -1.5L;
    const mandel::FloatType y_max = 1.5L;
    const int max_iterations = 100;

    std::atomic<unsigned int> current_generation(0);
    ThreadPool thread_pool(4);

    std::cout << "[UNIT_TEST] Creating renderer..." << std::endl;
    auto renderer = std::make_shared<mandel::MandelbrotRenderer>(
        width, height,
        x_min, x_max, y_min, y_max,
        max_iterations,
        current_generation,
        0,  // start_generation
        thread_pool
    );

    std::cout << "[UNIT_TEST] Starting render..." << std::endl;
    renderer->start();

    std::cout << "[UNIT_TEST] Waiting for render to complete..." << std::endl;
    // Wait for thread pool to be idle
    int wait_count = 0;
    while (!thread_pool.is_idle())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
        if (wait_count > 1000)  // 10 second timeout
        {
            std::cerr << "[UNIT_TEST] ERROR: Timeout waiting for render to complete" << std::endl;
            return 1;
        }
    }
    std::cout << "[UNIT_TEST] Render completed" << std::endl;

    // Verify coverage by checking that at least some pixels are non-black
    const std::vector<unsigned char>& pixels = renderer->get_pixels();
    size_t expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    if (pixels.size() != expected_size)
    {
        std::cerr << "[UNIT_TEST] ERROR: Pixel buffer size mismatch. Expected " << expected_size << ", got " << pixels.size() << std::endl;
        return 1;
    }

    size_t non_black_count = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        if (pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0)
        {
            non_black_count++;
        }
    }

    if (non_black_count == 0)
    {
        std::cerr << "[UNIT_TEST] FAILURE: No non-black pixels found." << std::endl;
        return 1;
    }

    std::cout << "[UNIT_TEST] SUCCESS: Found " << non_black_count << " non-black pixels." << std::endl;
    return 0;
}
