#pragma once

namespace mandel
{

// Abstraction for viewport with overscan support
// 1:1 coordinate aspect (square region in complex plane), but renders to full window (may stretch when non-square)
class OverscanViewport
{
public:
    OverscanViewport(int viewport_width, int viewport_height);
    
    void set_viewport_size(int width, int height);
    
    void set_overscan_enabled(bool enabled);
    bool get_overscan_enabled() const { return overscan_enabled_; }
    
    int viewport_width() const { return viewport_width_; }
    int viewport_height() const { return viewport_height_; }
    int canvas_width() const { return canvas_width_; }
    int canvas_height() const { return canvas_height_; }
    int margin_x() const { return margin_x_; }
    int margin_y() const { return margin_y_; }
    
    // Texture coordinate calculation
    // Returns UV coordinates for the viewport area within the overscanned canvas
    // display_offset_x/y: screen pixel offset from dragging (positive = dragged right/down)
    // display_scale: zoom scale factor
    struct TextureCoords
    {
        float uv_min_x;
        float uv_min_y;
        float uv_max_x;
        float uv_max_y;
        bool clamped_left;
        bool clamped_right;
        bool clamped_top;
        bool clamped_bottom;
    };
    
    TextureCoords calculate_texture_coords(float display_offset_x, float display_offset_y, float display_scale) const;
    
    // Screen position calculation for drawing
    struct DrawInfo
    {
        float draw_x;
        float draw_y;
        float texture_width;
        float texture_height;
        float texture_offset_x;
        float texture_offset_y;
        float uv_min_x;
        float uv_min_y;
        float uv_max_x;
        float uv_max_y;
    };
    
    DrawInfo calculate_draw_info(float viewport_pos_x, float viewport_pos_y,
                                 float display_offset_x, float display_offset_y,
                                 float display_scale,
                                 float zoom_center_x, float zoom_center_y) const;

    // Overload using explicit texture dimensions (for when displayed texture differs from current viewport, e.g. during resize)
    DrawInfo calculate_draw_info(float viewport_pos_x, float viewport_pos_y,
                                 float display_offset_x, float display_offset_y,
                                 float display_scale,
                                 float zoom_center_x, float zoom_center_y,
                                 int texture_canvas_width, int texture_canvas_height) const;

private:
    void calculate_margins();

    int viewport_width_;
    int viewport_height_;
    int margin_x_;
    int margin_y_;
    int canvas_width_;
    int canvas_height_;
    bool overscan_enabled_;
};

} // namespace mandel
