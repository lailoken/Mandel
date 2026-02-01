#include "mandel_ui_test.hpp"
#include "src/mandel_ui.hpp"
#include <cmath>

namespace mandel
{

// Test helper implementation - calls actual MandelUI methods
bool MandelUITestHelper::test_swap_calculation(
    FloatType displayed_texture_canvas_x_min, FloatType displayed_texture_canvas_x_max,
    FloatType displayed_texture_canvas_y_min, FloatType displayed_texture_canvas_y_max,
    FloatType render_start_canvas_x_min, FloatType render_start_canvas_x_max,
    FloatType render_start_canvas_y_min, FloatType render_start_canvas_y_max,
    float display_offset_x, float display_offset_y,
    int viewport_width, int viewport_height,
    int /*canvas_width*/, int /*canvas_height*/,
    int /*margin_x*/, int /*margin_y*/,
    FloatType& mouse_complex_x_before, FloatType& mouse_complex_y_before,
    FloatType& mouse_complex_x_after, FloatType& mouse_complex_y_after)
{
    // Create a minimal MandelUI instance to use its conversion methods
    // We'll use dummy functions and create a thread pool
    ThreadPool thread_pool(1);
    TextureUpdateFunc update_func = [](ImTextureID* /*texture_id*/, const unsigned char* /*pixels*/, int /*width*/, int /*height*/) {};
    TextureDeleteFunc delete_func = [](ImTextureID /*texture_id*/) {};
    
    MandelUI ui(update_func, delete_func, &thread_pool);
    ui.handle_resize(viewport_width, viewport_height);
    
    // Set up the state to match the test scenario
    // We need to access private members, so we'll use the friend class access
    ui.displayed_texture_canvas_x_min_ = displayed_texture_canvas_x_min;
    ui.displayed_texture_canvas_x_max_ = displayed_texture_canvas_x_max;
    ui.displayed_texture_canvas_y_min_ = displayed_texture_canvas_y_min;
    ui.displayed_texture_canvas_y_max_ = displayed_texture_canvas_y_max;
    
    ui.render_start_canvas_x_min_ = render_start_canvas_x_min;
    ui.render_start_canvas_x_max_ = render_start_canvas_x_max;
    ui.render_start_canvas_y_min_ = render_start_canvas_y_min;
    ui.render_start_canvas_y_max_ = render_start_canvas_y_max;
    
    // Calculate mouse complex coordinate BEFORE swap using displayed texture viewport
    FloatType displayed_viewport_x_min, displayed_viewport_x_max, displayed_viewport_y_min, displayed_viewport_y_max;
    ui.convert_canvas_to_viewport_bounds(displayed_texture_canvas_x_min, displayed_texture_canvas_x_max,
                                         displayed_texture_canvas_y_min, displayed_texture_canvas_y_max,
                                         displayed_viewport_x_min, displayed_viewport_x_max,
                                         displayed_viewport_y_min, displayed_viewport_y_max);
    
    float mouse_screen_x = static_cast<float>(viewport_width) / 2.0f + display_offset_x;
    float mouse_screen_y = static_cast<float>(viewport_height) / 2.0f + display_offset_y;
    
    FloatType displayed_viewport_x_range = displayed_viewport_x_max - displayed_viewport_x_min;
    FloatType displayed_viewport_y_range = displayed_viewport_y_max - displayed_viewport_y_min;
    mouse_complex_x_before = displayed_viewport_x_min + static_cast<FloatType>(mouse_screen_x) * (displayed_viewport_x_range / static_cast<FloatType>(viewport_width));
    mouse_complex_y_before = displayed_viewport_y_min + static_cast<FloatType>(mouse_screen_y) * (displayed_viewport_y_range / static_cast<FloatType>(viewport_height));
    
    // Call the actual swap calculation method
    float adjusted_display_offset_x = display_offset_x;
    float adjusted_display_offset_y = display_offset_y;
    ui.calculate_swap_display_offset_adjustment(adjusted_display_offset_x, adjusted_display_offset_y);
    
    // Calculate mouse complex coordinate AFTER swap
    FloatType render_start_viewport_x_min, render_start_viewport_x_max, render_start_viewport_y_min, render_start_viewport_y_max;
    ui.convert_canvas_to_viewport_bounds(render_start_canvas_x_min, render_start_canvas_x_max,
                                         render_start_canvas_y_min, render_start_canvas_y_max,
                                         render_start_viewport_x_min, render_start_viewport_x_max,
                                         render_start_viewport_y_min, render_start_viewport_y_max);
    
    float mouse_screen_x_after = static_cast<float>(viewport_width) / 2.0f + adjusted_display_offset_x;
    float mouse_screen_y_after = static_cast<float>(viewport_height) / 2.0f + adjusted_display_offset_y;
    
    FloatType viewport_x_range = render_start_viewport_x_max - render_start_viewport_x_min;
    FloatType viewport_y_range = render_start_viewport_y_max - render_start_viewport_y_min;
    mouse_complex_x_after = render_start_viewport_x_min + static_cast<FloatType>(mouse_screen_x_after) * (viewport_x_range / static_cast<FloatType>(viewport_width));
    mouse_complex_y_after = render_start_viewport_y_min + static_cast<FloatType>(mouse_screen_y_after) * (viewport_y_range / static_cast<FloatType>(viewport_height));
    
    // Check if mouse coordinate is preserved
    FloatType tolerance = 0.0001L;
    FloatType diff_x = std::abs(mouse_complex_x_after - mouse_complex_x_before);
    FloatType diff_y = std::abs(mouse_complex_y_after - mouse_complex_y_before);
    
    return (diff_x <= tolerance && diff_y <= tolerance);
}

}  // namespace mandel
