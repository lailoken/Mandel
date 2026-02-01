#include "mandel_ui_test.hpp"
#include "src/overscan_viewport.hpp"

#include <cstdio>
#include <cmath>

// Minimal test that uses MandelUI methods directly
bool test_swap_preserves_mouse_coordinate()
{
    printf("=== Test: Swap Preserves Mouse Coordinate ===\n");
    
    using FloatType = long double;
    
    int viewport_width = 1600;
    int viewport_height = 1200;
    mandel::OverscanViewport viewport(viewport_width, viewport_height);
    
    // Simulate the scenario: user drags, pan happens, render completes, swap occurs
    FloatType displayed_texture_canvas_x_min = -2.0L;
    FloatType displayed_texture_canvas_x_max = 0.5L;
    FloatType displayed_texture_canvas_y_min = -1.125L;
    FloatType displayed_texture_canvas_y_max = 1.125L;
    
    FloatType render_start_canvas_x_min = -2.0585754452L;
    FloatType render_start_canvas_x_max = 0.4414245548L;
    FloatType render_start_canvas_y_min = -0.9843750000L;
    FloatType render_start_canvas_y_max = 1.2656250000L;
    
    float display_offset_x = 156.0f;
    float display_offset_y = -62.0f;
    
    FloatType mouse_complex_x_before, mouse_complex_y_before;
    FloatType mouse_complex_x_after, mouse_complex_y_after;
    
    bool result = mandel::MandelUITestHelper::test_swap_calculation(
        displayed_texture_canvas_x_min, displayed_texture_canvas_x_max,
        displayed_texture_canvas_y_min, displayed_texture_canvas_y_max,
        render_start_canvas_x_min, render_start_canvas_x_max,
        render_start_canvas_y_min, render_start_canvas_y_max,
        display_offset_x, display_offset_y,
        viewport_width, viewport_height,
        viewport.canvas_width(), viewport.canvas_height(),
        viewport.margin_x(), viewport.margin_y(),
        mouse_complex_x_before, mouse_complex_y_before,
        mouse_complex_x_after, mouse_complex_y_after);
    
    if (!result)
    {
        FloatType diff_x = std::abs(mouse_complex_x_after - mouse_complex_x_before);
        FloatType diff_y = std::abs(mouse_complex_y_after - mouse_complex_y_before);
        printf("FAILED: Mouse coordinate jump detected!\n");
        printf("  Before: (%.20Lf, %.20Lf)\n", mouse_complex_x_before, mouse_complex_y_before);
        printf("  After:  (%.20Lf, %.20Lf)\n", mouse_complex_x_after, mouse_complex_y_after);
        printf("  Diff:   (%.20Lf, %.20Lf)\n", diff_x, diff_y);
        return false;
    }
    
    printf("PASS: Mouse coordinate preserved\n");
    return true;
}

int main()
{
    printf("========================================\n");
    printf("Testing Buffer Swap Mouse Coordinate Preservation\n");
    printf("========================================\n\n");
    
    bool test_passed = test_swap_preserves_mouse_coordinate();
    
    printf("\n========================================\n");
    if (test_passed)
    {
        printf("Test PASSED\n");
        return 0;
    }
    else
    {
        printf("Test FAILED - Bug detected in swap calculation!\n");
        return 1;
    }
}
