#pragma once

#include "src/mandel.hpp" // For FloatType

namespace mandel
{

// Forward declaration
class MandelUI;

// Test helper class for unit testing - minimal interface, test-only code
class MandelUITestHelper
{
public:
    // Test the swap calculation by calling MandelUI methods
    static bool test_swap_calculation(
        FloatType displayed_texture_canvas_x_min, FloatType displayed_texture_canvas_x_max,
        FloatType displayed_texture_canvas_y_min, FloatType displayed_texture_canvas_y_max,
        FloatType render_start_canvas_x_min, FloatType render_start_canvas_x_max,
        FloatType render_start_canvas_y_min, FloatType render_start_canvas_y_max,
        float display_offset_x, float display_offset_y,
        int viewport_width, int viewport_height,
        int canvas_width, int canvas_height,
        int margin_x, int margin_y,
        FloatType& mouse_complex_x_before, FloatType& mouse_complex_y_before,
        FloatType& mouse_complex_x_after, FloatType& mouse_complex_y_after);
};

}  // namespace mandel
