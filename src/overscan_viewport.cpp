#include "overscan_viewport.hpp"

#include <algorithm>

namespace mandel
{

OverscanViewport::OverscanViewport(int viewport_width, int viewport_height)
    : viewport_width_(viewport_width)
    , viewport_height_(viewport_height)
    , overscan_enabled_(true)
{
    calculate_margins();
}

void OverscanViewport::set_viewport_size(int width, int height)
{
    viewport_width_ = width;
    viewport_height_ = height;
    calculate_margins();
}

void OverscanViewport::calculate_margins()
{
    if (overscan_enabled_)
    {
        margin_x_ = (viewport_width_ + 5) / 6;
        margin_y_ = (viewport_height_ + 5) / 6;
    }
    else
    {
        margin_x_ = 0;
        margin_y_ = 0;
    }
    canvas_width_ = viewport_width_ + 2 * margin_x_;
    canvas_height_ = viewport_height_ + 2 * margin_y_;
}

void OverscanViewport::set_overscan_enabled(bool enabled)
{
    if (overscan_enabled_ != enabled)
    {
        overscan_enabled_ = enabled;
        calculate_margins();
    }
}

OverscanViewport::TextureCoords OverscanViewport::calculate_texture_coords(
    float display_offset_x, float display_offset_y, float /*display_scale*/) const
{
    TextureCoords coords;
    
    float canvas_w_f = static_cast<float>(canvas_width_);
    float canvas_h_f = static_cast<float>(canvas_height_);
    float uv_offset_x = -display_offset_x / canvas_w_f;
    float uv_offset_y = -display_offset_y / canvas_h_f;

    float base_uv_min_x = static_cast<float>(margin_x_) / canvas_w_f;
    float base_uv_min_y = static_cast<float>(margin_y_) / canvas_h_f;
    float base_uv_max_x = static_cast<float>(viewport_width_ + margin_x_) / canvas_w_f;
    float base_uv_max_y = static_cast<float>(viewport_height_ + margin_y_) / canvas_h_f;
    
    // Apply offset to show overscanned area during dragging
    float uv_min_x_unclamped = base_uv_min_x + uv_offset_x;
    float uv_min_y_unclamped = base_uv_min_y + uv_offset_y;
    float uv_max_x_unclamped = base_uv_max_x + uv_offset_x;
    float uv_max_y_unclamped = base_uv_max_y + uv_offset_y;
    
    // Clamp UV coordinates to valid range [0, 1]
    coords.uv_min_x = std::max(0.0f, std::min(1.0f, uv_min_x_unclamped));
    coords.uv_min_y = std::max(0.0f, std::min(1.0f, uv_min_y_unclamped));
    coords.uv_max_x = std::max(0.0f, std::min(1.0f, uv_max_x_unclamped));
    coords.uv_max_y = std::max(0.0f, std::min(1.0f, uv_max_y_unclamped));
    
    // Check if UV was clamped
    coords.clamped_left = (uv_min_x_unclamped < 0.0f);
    coords.clamped_right = (uv_max_x_unclamped > 1.0f);
    coords.clamped_top = (uv_min_y_unclamped < 0.0f);
    coords.clamped_bottom = (uv_max_y_unclamped > 1.0f);
    
    return coords;
}

OverscanViewport::DrawInfo OverscanViewport::calculate_draw_info(
    float viewport_pos_x, float viewport_pos_y,
    float display_offset_x, float display_offset_y,
    float display_scale,
    float zoom_center_x, float zoom_center_y) const
{
    DrawInfo info;
    
    // Calculate base drawing position (viewport position + zoom offset)
    float scale_offset_x = zoom_center_x * (1.0f - display_scale);
    float scale_offset_y = zoom_center_y * (1.0f - display_scale);
    info.draw_x = viewport_pos_x + scale_offset_x;
    info.draw_y = viewport_pos_y + scale_offset_y;
    
    float scaled_width = static_cast<float>(viewport_width_) * display_scale;
    float scaled_height = static_cast<float>(viewport_height_) * display_scale;
    
    // Get texture coordinates
    TextureCoords coords = calculate_texture_coords(display_offset_x, display_offset_y, display_scale);
    
    // Calculate UV width/height (how much of the texture we're actually showing)
    float uv_width = coords.uv_max_x - coords.uv_min_x;
    float uv_height = coords.uv_max_y - coords.uv_min_y;
    
    float canvas_w_f = static_cast<float>(canvas_width_);
    float canvas_h_f = static_cast<float>(canvas_height_);
    float uv_offset_x = -display_offset_x / canvas_w_f;
    float uv_offset_y = -display_offset_y / canvas_h_f;
    float base_uv_min_x = static_cast<float>(margin_x_) / canvas_w_f;
    float base_uv_min_y = static_cast<float>(margin_y_) / canvas_h_f;
    float base_uv_max_x = static_cast<float>(viewport_width_ + margin_x_) / canvas_w_f;
    float base_uv_max_y = static_cast<float>(viewport_height_ + margin_y_) / canvas_h_f;
    float uv_min_x_unclamped = base_uv_min_x + uv_offset_x;
    float uv_min_y_unclamped = base_uv_min_y + uv_offset_y;
    float uv_max_x_unclamped = base_uv_max_x + uv_offset_x;
    float uv_max_y_unclamped = base_uv_max_y + uv_offset_y;
    (void)uv_max_x_unclamped;  // Used only for clamping check below
    (void)uv_max_y_unclamped;  // Used only for clamping check below
    
    // Calculate texture screen size
    // The texture should always be drawn at the size that corresponds to the viewport area in UV space
    // When not clamped: full viewport size
    // When clamped: size based on the actual UV range we're showing, but scaled to maintain aspect ratio
    // The key insight: uv_width/uv_height represent the fraction of the viewport area we're showing
    // So texture_width = viewport_width * (uv_width / base_uv_width) where base_uv_width is the viewport width in UV space
    
    float base_uv_width = base_uv_max_x - base_uv_min_x;  // Viewport width in UV space
    float base_uv_height = base_uv_max_y - base_uv_min_y;  // Viewport height in UV space
    
    info.texture_width = scaled_width * (uv_width / base_uv_width);
    info.texture_height = scaled_height * (uv_height / base_uv_height);
    
    // Calculate offset for grey areas when clamped
    info.texture_offset_x = 0.0f;
    info.texture_offset_y = 0.0f;
    
    if (coords.clamped_left)
    {
        // We're showing beyond the left edge: offset texture to the right to show grey on left
        // The amount we went past the edge in UV space, converted to screen pixels
        float clamped_amount_uv = -uv_min_x_unclamped;  // Positive value
        // Convert to screen pixels: this is how much of the viewport is outside the texture
        // clamped_amount_uv is in UV space (normalized to canvas), we need to convert to viewport space
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_width;
        float clamped_amount_screen = clamped_amount_viewport_uv * scaled_width;
        info.texture_offset_x = clamped_amount_screen;
    }
    
    if (coords.clamped_top)
    {
        // We're showing beyond the top edge: offset texture down to show grey on top
        float clamped_amount_uv = -uv_min_y_unclamped;  // Positive value
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_height;
        float clamped_amount_screen = clamped_amount_viewport_uv * scaled_height;
        info.texture_offset_y = clamped_amount_screen;
    }
    
    // Store UV coordinates
    info.uv_min_x = coords.uv_min_x;
    info.uv_min_y = coords.uv_min_y;
    info.uv_max_x = coords.uv_max_x;
    info.uv_max_y = coords.uv_max_y;
    
    return info;
}

OverscanViewport::DrawInfo OverscanViewport::calculate_draw_info(
    float viewport_pos_x, float viewport_pos_y,
    float display_offset_x, float display_offset_y,
    float display_scale,
    float zoom_center_x, float zoom_center_y,
    int texture_canvas_width, int texture_canvas_height) const
{
    // Use texture's actual dimensions and derived margin for correct center/UV mapping.
    // margin = (canvas + 5) / 8 (inverse of canvas = viewport + 2*margin with margin = (viewport+5)/6)
    int tex_margin_x = (texture_canvas_width + 5) / 8;
    int tex_margin_y = (texture_canvas_height + 5) / 8;
    int tex_viewport_w = texture_canvas_width - 2 * tex_margin_x;
    int tex_viewport_h = texture_canvas_height - 2 * tex_margin_y;
    if (tex_viewport_w <= 0 || tex_viewport_h <= 0)
    {
        // Fallback to default when dimensions are invalid
        return calculate_draw_info(viewport_pos_x, viewport_pos_y,
                                  display_offset_x, display_offset_y,
                                  display_scale, zoom_center_x, zoom_center_y);
    }

    DrawInfo info;

    float scale_offset_x = zoom_center_x * (1.0f - display_scale);
    float scale_offset_y = zoom_center_y * (1.0f - display_scale);
    info.draw_x = viewport_pos_x + scale_offset_x;
    info.draw_y = viewport_pos_y + scale_offset_y;

    // On-screen size: fill current viewport
    float scaled_width = static_cast<float>(viewport_width_) * display_scale;
    float scaled_height = static_cast<float>(viewport_height_) * display_scale;

    float tex_canvas_w_f = static_cast<float>(texture_canvas_width);
    float tex_canvas_h_f = static_cast<float>(texture_canvas_height);
    float uv_offset_x = -display_offset_x / tex_canvas_w_f;
    float uv_offset_y = -display_offset_y / tex_canvas_h_f;

    float base_uv_min_x = static_cast<float>(tex_margin_x) / tex_canvas_w_f;
    float base_uv_min_y = static_cast<float>(tex_margin_y) / tex_canvas_h_f;
    float base_uv_max_x = static_cast<float>(tex_margin_x + tex_viewport_w) / tex_canvas_w_f;
    float base_uv_max_y = static_cast<float>(tex_margin_y + tex_viewport_h) / tex_canvas_h_f;

    float uv_min_x_unclamped = base_uv_min_x + uv_offset_x;
    float uv_min_y_unclamped = base_uv_min_y + uv_offset_y;
    float uv_max_x_unclamped = base_uv_max_x + uv_offset_x;
    float uv_max_y_unclamped = base_uv_max_y + uv_offset_y;

    info.uv_min_x = std::max(0.0f, std::min(1.0f, uv_min_x_unclamped));
    info.uv_min_y = std::max(0.0f, std::min(1.0f, uv_min_y_unclamped));
    info.uv_max_x = std::max(0.0f, std::min(1.0f, uv_max_x_unclamped));
    info.uv_max_y = std::max(0.0f, std::min(1.0f, uv_max_y_unclamped));

    bool clamped_left = (uv_min_x_unclamped < 0.0f);
    bool clamped_top = (uv_min_y_unclamped < 0.0f);

    float base_uv_width = base_uv_max_x - base_uv_min_x;
    float base_uv_height = base_uv_max_y - base_uv_min_y;
    float uv_width = info.uv_max_x - info.uv_min_x;
    float uv_height = info.uv_max_y - info.uv_min_y;

    info.texture_width = scaled_width * (uv_width / base_uv_width);
    info.texture_height = scaled_height * (uv_height / base_uv_height);

    info.texture_offset_x = 0.0f;
    info.texture_offset_y = 0.0f;
    if (clamped_left)
    {
        float clamped_amount_uv = -uv_min_x_unclamped;
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_width;
        info.texture_offset_x = clamped_amount_viewport_uv * scaled_width;
    }
    if (clamped_top)
    {
        float clamped_amount_uv = -uv_min_y_unclamped;
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_height;
        info.texture_offset_y = clamped_amount_viewport_uv * scaled_height;
    }

    return info;
}

} // namespace mandel
