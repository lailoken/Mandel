#include "mandel_ui_test.hpp"
#include "src/mandel_ui.hpp"

namespace mandel
{

// Test helper: call real swap conversion and verify mouse complex is preserved
bool MandelUITestHelper::test_swap_calculation(
    FloatType displayed_texture_canvas_x_min, FloatType displayed_texture_canvas_x_max,
    FloatType displayed_texture_canvas_y_min, FloatType displayed_texture_canvas_y_max,
    FloatType render_start_canvas_x_min, FloatType render_start_canvas_x_max,
    FloatType render_start_canvas_y_min, FloatType render_start_canvas_y_max,
    float display_offset_x, float display_offset_y,
    int viewport_width, int viewport_height,
    int canvas_width, int canvas_height,
    int margin_x, int margin_y,
    FloatType& mouse_complex_x_before, FloatType& mouse_complex_y_before,
    FloatType& mouse_complex_x_after, FloatType& mouse_complex_y_after)
{
    return MandelUI::compute_swap_conversion(
        displayed_texture_canvas_x_min, displayed_texture_canvas_x_max,
        displayed_texture_canvas_y_min, displayed_texture_canvas_y_max,
        render_start_canvas_x_min, render_start_canvas_x_max,
        render_start_canvas_y_min, render_start_canvas_y_max,
        display_offset_x, display_offset_y,
        viewport_width, viewport_height,
        canvas_width, canvas_height,
        margin_x, margin_y,
        mouse_complex_x_before, mouse_complex_y_before,
        mouse_complex_x_after, mouse_complex_y_after);
}

}  // namespace mandel
