#include "mandel.hpp"

#include <iostream>
#include <cstdint>
#include <set>
#include <utility>

int main()
{
    const int width = 800;
    const int height = 600;
    
    // Create Mandelbrot renderer with long double precision
    mandel::MandelbrotRenderer renderer(width, height);
    
    // Generate synchronously (no threading)
    renderer.regenerate(nullptr);
    
    // Verify coverage by checking that all pixels have been written to
    // We check that pixels are not all zeros (which would indicate uninitialized buffer)
    const unsigned char* pixels = renderer.get_pixels();
    size_t expected_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    
    ::std::set<::std::pair<int32_t, int32_t>> covered_pixels;
    size_t pixels_with_data = 0;
    
    // Count pixels that have been written (check if alpha channel is set)
    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            int idx = (y * width + x) * 4;
            // Check if alpha channel is set (should be 255 for all pixels)
            if (pixels[idx + 3] == 255)
            {
                covered_pixels.insert(::std::make_pair(x, y));
                pixels_with_data++;
            }
        }
    }
    
    // Verify all pixels are covered
    bool success = true;
    size_t covered_count = covered_pixels.size();
    
    if (covered_count == expected_pixels && pixels_with_data == expected_pixels)
    {
        std::cout << "[UNIT_TEST] SUCCESS: All " << expected_pixels << " pixels are covered!" << std::endl;
    }
    else
    {
        std::cerr << "[UNIT_TEST] FAILURE: Only " << covered_count << " out of " << expected_pixels 
                  << " pixels are covered. Missing " << (expected_pixels - covered_count) << " pixels." << std::endl;
        success = false;
        
        // Report missing pixels (first 20 for brevity)
        int missing_count = 0;
        for (int32_t y = 0; y < height && missing_count < 20; ++y)
        {
            for (int32_t x = 0; x < width && missing_count < 20; ++x)
            {
                if (covered_pixels.find(::std::make_pair(x, y)) == covered_pixels.end())
                {
                    std::cerr << "  Missing pixel at (" << x << ", " << y << ")" << std::endl;
                    missing_count++;
                }
            }
        }
        if ((expected_pixels - covered_count) > 20)
        {
            std::cerr << "  ... and " << (expected_pixels - covered_count - 20) << " more missing pixels" << std::endl;
        }
    }
    
    return success ? 0 : 1;
}
