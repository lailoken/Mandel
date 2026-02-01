#include "overscan_viewport.hpp"

#include <algorithm>
#include <cmath>

namespace mandel
{

OverscanViewport::OverscanViewport(int viewport_width, int viewport_height)
    : viewport_width_(viewport_width)
    , viewport_height_(viewport_height)
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
    // Calculate margins (~1/6 viewport, rounded up)
    margin_x_ = (viewport_width_ + 5) / 6;
    margin_y_ = (viewport_height_ + 5) / 6;
    
    // Canvas dimensions
    canvas_width_ = viewport_width_ + 2 * margin_x_;
    canvas_height_ = viewport_height_ + 2 * margin_y_;
}

OverscanViewport::TextureCoords OverscanViewport::calculate_texture_coords(
    float display_offset_x, float display_offset_y, float /*display_scale*/) const
{
    TextureCoords coords;
    
    // Convert display offset from screen pixels to UV space (normalized to canvas size)
    // When display_offset is positive (dragged right), we show content from the left (decrease UV)
    float uv_offset_x = -display_offset_x / static_cast<float>(canvas_width_);
    float uv_offset_y = -display_offset_y / static_cast<float>(canvas_height_);
    
    // Calculate base UV coordinates for viewport area (center of overscanned canvas)
    float base_uv_min_x = static_cast<float>(margin_x_) / static_cast<float>(canvas_width_);
    float base_uv_min_y = static_cast<float>(margin_y_) / static_cast<float>(canvas_height_);
    float base_uv_max_x = static_cast<float>(viewport_width_ + margin_x_) / static_cast<float>(canvas_width_);
    float base_uv_max_y = static_cast<float>(viewport_height_ + margin_y_) / static_cast<float>(canvas_height_);
    
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
    
    // Scaled viewport dimensions
    float scaled_width = static_cast<float>(viewport_width_) * display_scale;
    float scaled_height = static_cast<float>(viewport_height_) * display_scale;
    
    // Get texture coordinates
    TextureCoords coords = calculate_texture_coords(display_offset_x, display_offset_y, display_scale);
    
    // Calculate UV width/height (how much of the texture we're actually showing)
    float uv_width = coords.uv_max_x - coords.uv_min_x;
    float uv_height = coords.uv_max_y - coords.uv_min_y;
    
    // Calculate unclamped UV coordinates to determine clamping
    float uv_offset_x = -display_offset_x / static_cast<float>(canvas_width_);
    float uv_offset_y = -display_offset_y / static_cast<float>(canvas_height_);
    float base_uv_min_x = static_cast<float>(margin_x_) / static_cast<float>(canvas_width_);
    float base_uv_min_y = static_cast<float>(margin_y_) / static_cast<float>(canvas_height_);
    float base_uv_max_x = static_cast<float>(viewport_width_ + margin_x_) / static_cast<float>(canvas_width_);
    float base_uv_max_y = static_cast<float>(viewport_height_ + margin_y_) / static_cast<float>(canvas_height_);
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
    
    // Calculate texture size: scale by the ratio of actual UV width to base UV width
    // This ensures the texture is drawn at the correct size relative to what we're showing
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
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_width;  // Fraction of viewport
        float clamped_amount_screen = clamped_amount_viewport_uv * scaled_width;
        info.texture_offset_x = clamped_amount_screen;
    }
    
    if (coords.clamped_top)
    {
        // We're showing beyond the top edge: offset texture down to show grey on top
        float clamped_amount_uv = -uv_min_y_unclamped;  // Positive value
        float clamped_amount_viewport_uv = clamped_amount_uv / base_uv_height;  // Fraction of viewport
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

} // namespace mandel
