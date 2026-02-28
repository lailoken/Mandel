#include "mandel_ui.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>

#include "config.hpp"
#include "overscan_viewport.hpp"
#include "worker_base.hpp"

#ifdef _DEBUG
#define DEBUG_PRINTF(...) std::printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) std::printf(__VA_ARGS__)
#endif

namespace mandel
{

MandelUI::MandelUI(TextureUpdateFunc update_func, TextureDeleteFunc delete_func, ThreadPool& thread_pool)
    : update_texture_func_(update_func),
      delete_texture_func_(delete_func),
      overscan_viewport_(800, 800),
      texture_front_(0),
      texture_back_(0),
      display_offset_x_(0.0f),
      display_offset_y_(0.0f),
      is_dragging_(false),
      render_generation_(0),
      canvas_x_min_(-2.0L),
      canvas_x_max_(0.5L),
      canvas_y_min_(-1.125L),
      canvas_y_max_(1.125L),
      displayed_texture_canvas_x_min_(-2.0L),
      displayed_texture_canvas_x_max_(0.5L),
      displayed_texture_canvas_y_min_(-1.125L),
      displayed_texture_canvas_y_max_(1.125L),
      render_start_canvas_x_min_(-2.0L),
      render_start_canvas_x_max_(0.5L),
      render_start_canvas_y_min_(-1.125L),
      render_start_canvas_y_max_(1.125L),
      worker_(nullptr),
      thread_pool_(thread_pool),
      render_pending_(false),
      control_(this),
      max_iterations_(512),
      has_pending_settings_(false)
{
    // Convert viewport bounds to canvas bounds (add overscan margins)
    convert_viewport_to_canvas_bounds(-2.0L, 0.5L, -1.125L, 1.125L, canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
    displayed_texture_canvas_x_min_ = canvas_x_min_;
    displayed_texture_canvas_x_max_ = canvas_x_max_;
    displayed_texture_canvas_y_min_ = canvas_y_min_;
    displayed_texture_canvas_y_max_ = canvas_y_max_;
    render_start_canvas_x_min_ = canvas_x_min_;
    render_start_canvas_x_max_ = canvas_x_max_;
    render_start_canvas_y_min_ = canvas_y_min_;
    render_start_canvas_y_max_ = canvas_y_max_;

    // Initial applied settings (viewport bounds + max iter)
    FloatType vx_min, vx_max, vy_min, vy_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_, vx_min, vx_max, vy_min, vy_max);
    applied_settings_ = ViewState(vx_min, vx_max, vy_min, vy_max, max_iterations_);

    // Create MandelWorker for real Mandelbrot rendering
    worker_ = std::make_unique<MandelWorker>(
        overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(), render_generation_,
        canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_, thread_pool_);
    worker_->set_max_iterations(max_iterations_);

    // Start initial render
    start_render();
}

MandelUI::~MandelUI()
{
    if (delete_texture_func_)
    {
        if (texture_front_ != 0)
        {
            delete_texture_func_(texture_front_);
        }
        if (texture_back_ != 0)
        {
            delete_texture_func_(texture_back_);
        }
    }
}

void MandelUI::convert_viewport_to_canvas_bounds(FloatType viewport_x_min, FloatType viewport_x_max, FloatType viewport_y_min,
                                                  FloatType viewport_y_max, FloatType& canvas_x_min, FloatType& canvas_x_max,
                                                  FloatType& canvas_y_min, FloatType& canvas_y_max) const
{
    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    FloatType pixel_to_x = viewport_x_range / static_cast<FloatType>(overscan_viewport_.viewport_width());
    FloatType pixel_to_y = viewport_y_range / static_cast<FloatType>(overscan_viewport_.viewport_height());

    canvas_x_min = viewport_x_min - pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    canvas_x_max = viewport_x_max + pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    canvas_y_min = viewport_y_min - pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    canvas_y_max = viewport_y_max + pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
}

void MandelUI::convert_canvas_to_viewport_bounds(FloatType canvas_x_min, FloatType canvas_x_max, FloatType canvas_y_min, FloatType canvas_y_max, FloatType& viewport_x_min,
                                                 FloatType& viewport_x_max, FloatType& viewport_y_min, FloatType& viewport_y_max) const
{
    FloatType canvas_x_range = canvas_x_max - canvas_x_min;
    FloatType canvas_y_range = canvas_y_max - canvas_y_min;
    FloatType pixel_to_x = canvas_x_range / static_cast<FloatType>(overscan_viewport_.canvas_width());
    FloatType pixel_to_y = canvas_y_range / static_cast<FloatType>(overscan_viewport_.canvas_height());

    viewport_x_min = canvas_x_min + pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    viewport_x_max = canvas_x_max - pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    viewport_y_min = canvas_y_min + pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    viewport_y_max = canvas_y_max - pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
}

void MandelUI::handle_resize(int viewport_width, int viewport_height)
{
    if (viewport_width == overscan_viewport_.viewport_width() && viewport_height == overscan_viewport_.viewport_height())
    {
        return;
    }

    overscan_viewport_.set_viewport_size(viewport_width, viewport_height);

    // Get current viewport bounds
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);

    // Convert back to canvas bounds with new margins
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;

    // Recreate worker with new dimensions
    worker_ = std::make_unique<MandelWorker>(
        overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(), render_generation_,
        canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_, thread_pool_);
    worker_->set_max_iterations(max_iterations_);
    start_render();
}

void MandelUI::start_render()
{
    render_generation_.fetch_add(1);
    // Record bounds this render is for (so swap conversion uses correct render_start)
    render_start_canvas_x_min_ = canvas_x_min_;
    render_start_canvas_x_max_ = canvas_x_max_;
    render_start_canvas_y_min_ = canvas_y_min_;
    render_start_canvas_y_max_ = canvas_y_max_;
    worker_->set_max_iterations(max_iterations_);
    worker_->start_render();
    if (!worker_->try_complete_render())
        render_pending_ = true;
    else
        update_textures();
}

void MandelUI::handle_input()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 viewport_pos = viewport->Pos;
    ImVec2 viewport_size = viewport->Size;
    ImVec2 mouse_pos = io.MousePos;
    bool mouse_over_viewport =
        (mouse_pos.x >= viewport_pos.x && mouse_pos.x < viewport_pos.x + viewport_size.x && mouse_pos.y >= viewport_pos.y && mouse_pos.y < viewport_pos.y + viewport_size.y);
    bool control_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    // Don't pan when ImGui is using the mouse (e.g. slider drag, combo box)
    bool imgui_wants_mouse = io.WantCaptureMouse;

    // Panning (drag): only when mouse is on background, not on any window, and not captured by a widget
    if (io.MouseDown[0] && mouse_over_viewport && !control_window_hovered && !imgui_wants_mouse)
    {
        if (!is_dragging_)
        {
            is_dragging_ = true;
        }

        display_offset_x_ += io.MouseDelta.x;
        display_offset_y_ += io.MouseDelta.y;

        // Threshold to start new render (use overscan margin; overscan is always on)
        // Trigger early (40% of margin) to give renderer time to finish before we hit the edge
        float start_threshold_x = static_cast<float>(overscan_viewport_.margin_x()) * 0.4f;
        float start_threshold_y = static_cast<float>(overscan_viewport_.margin_y()) * 0.4f;

        // Only trigger a new pan when over threshold and no render in progress (avoids runaway)
        if ((std::abs(display_offset_x_) > start_threshold_x || std::abs(display_offset_y_) > start_threshold_y) && !render_pending_)
        {
            handle_pan(display_offset_x_, display_offset_y_);
        }
    }
    else
    {
        if (is_dragging_)
        {
            is_dragging_ = false;
        }
    }
}

void MandelUI::handle_pan(float display_offset_x, float display_offset_y)
{
    DEBUG_PRINTF("handle_pan: offset=(%.2f, %.2f) render_pending=%d\n", display_offset_x, display_offset_y, render_pending_);

    // 1. Save bounds of texture currently on screen (for swap conversion later).
    //    Only when no async render is pending does canvas_ match the front texture;
    //    when render_pending_, the front texture is still the old one so keep displayed_texture_*.
    if (!render_pending_)
    {
        displayed_texture_canvas_x_min_ = canvas_x_min_;
        displayed_texture_canvas_x_max_ = canvas_x_max_;
        displayed_texture_canvas_y_min_ = canvas_y_min_;
        displayed_texture_canvas_y_max_ = canvas_y_max_;
        DEBUG_PRINTF("  Saved displayed bounds: x=[%.6Lf, %.6Lf], y=[%.6Lf, %.6Lf]\n", 
               displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
               displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_);
    }
    else
    {
        DEBUG_PRINTF("  Render pending, keeping existing displayed bounds: x=[%.6Lf, %.6Lf], y=[%.6Lf, %.6Lf]\n", 
               displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
               displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_);
    }

    // 2. Get viewport bounds for what's actually on screen (use displayed when render pending)
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    if (render_pending_)
        convert_canvas_to_viewport_bounds(displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
                                           displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_,
                                           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max);
    else
        convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                           canvas_y_min_, canvas_y_max_,
                                           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max);

    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    // display_offset is in viewport/screen pixels; convert to complex units (independent of overscan)
    int vp_w = overscan_viewport_.viewport_width();
    int vp_h = overscan_viewport_.viewport_height();
    FloatType screen_to_x = viewport_x_range / static_cast<FloatType>(vp_w);
    FloatType screen_to_y = viewport_y_range / static_cast<FloatType>(vp_h);

    // Convert display_offset (viewport pixels) to complex plane delta.
    // See DRAGGING_REDESIGN.md and plan: screen (x right, y down) vs Mandel (x right, y up).
    FloatType pan_x = -display_offset_x * screen_to_x;
    FloatType pan_y = display_offset_y * screen_to_y;

    DEBUG_PRINTF("  Pan complex: (%.6Lf, %.6Lf)\n", pan_x, pan_y);

    // Calculate new viewport bounds
    viewport_x_min += pan_x;
    viewport_x_max += pan_x;
    viewport_y_min += pan_y;
    viewport_y_max += pan_y;

    // 3–4. Convert to canvas bounds and save as render_start_bounds (what new texture will be for)
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;
    render_start_canvas_x_min_ = new_canvas_x_min;
    render_start_canvas_x_max_ = new_canvas_x_max;
    render_start_canvas_y_min_ = new_canvas_y_min;
    render_start_canvas_y_max_ = new_canvas_y_max;

    DEBUG_PRINTF("  New render bounds: x=[%.6Lf, %.6Lf], y=[%.6Lf, %.6Lf]\n", 
           render_start_canvas_x_min_, render_start_canvas_x_max_,
           render_start_canvas_y_min_, render_start_canvas_y_max_);

    // 5. Do NOT reset display_offset here: we keep drawing the old texture with the current
    //    offset until the new texture is ready, then convert_display_offset_on_swap() sets
    //    the correct offset for the new texture. Resetting to 0 would make the image jump.

    // 6. Start render
    start_render();
}

bool MandelUI::compute_swap_conversion(
    FloatType displayed_canvas_x_min, FloatType displayed_canvas_x_max,
    FloatType displayed_canvas_y_min, FloatType displayed_canvas_y_max,
    FloatType render_start_canvas_x_min, FloatType render_start_canvas_x_max,
    FloatType render_start_canvas_y_min, FloatType render_start_canvas_y_max,
    float display_offset_x, float display_offset_y,
    int viewport_width, int viewport_height,
    int canvas_width, int canvas_height,
    int margin_x, int margin_y,
    FloatType& mouse_complex_x_before, FloatType& mouse_complex_y_before,
    FloatType& mouse_complex_x_after, FloatType& mouse_complex_y_after)
{
    float viewport_center_x = static_cast<float>(viewport_width) * 0.5f;
    float viewport_center_y = static_cast<float>(viewport_height) * 0.5f;
    
    // We track the center of the viewport.
    // Pixel Index = Margin + Screen - Offset
    float screen_x = viewport_center_x;
    float screen_y = viewport_center_y;

    // Complex point under viewport center in OLD texture
    float canvas_px_old = static_cast<float>(margin_x) + screen_x - display_offset_x;
    float canvas_py_old = static_cast<float>(margin_y) + screen_y - display_offset_y;
    
    FloatType old_x_range = displayed_canvas_x_max - displayed_canvas_x_min;
    FloatType old_y_range = displayed_canvas_y_max - displayed_canvas_y_min;
    mouse_complex_x_before = displayed_canvas_x_min + static_cast<FloatType>(canvas_px_old) / static_cast<FloatType>(canvas_width) * old_x_range;
    mouse_complex_y_before = displayed_canvas_y_max - static_cast<FloatType>(canvas_py_old) / static_cast<FloatType>(canvas_height) * old_y_range;

    // New display_offset
    FloatType new_x_range = render_start_canvas_x_max - render_start_canvas_x_min;
    FloatType new_y_range = render_start_canvas_y_max - render_start_canvas_y_min;
    float canvas_px_new = static_cast<float>((mouse_complex_x_before - render_start_canvas_x_min) / new_x_range * canvas_width);
    float canvas_py_new = static_cast<float>((render_start_canvas_y_max - mouse_complex_y_before) / new_y_range * canvas_height);
    
    // Calculate new offset such that the same complex point stays at the viewport center
    // canvas_px_new = Margin + Screen - NewOffset
    // NewOffset = Margin + Screen - canvas_px_new
    float new_offset_x = static_cast<float>(margin_x) + screen_x - canvas_px_new;
    float new_offset_y = static_cast<float>(margin_y) + screen_y - canvas_py_new;

    // Verification: Calculate complex point under viewport center in NEW texture with NEW offset
    float canvas_px_new2 = static_cast<float>(margin_x) + screen_x - new_offset_x;
    float canvas_py_new2 = static_cast<float>(margin_y) + screen_y - new_offset_y;
    mouse_complex_x_after = render_start_canvas_x_min + static_cast<FloatType>(canvas_px_new2) / static_cast<FloatType>(canvas_width) * new_x_range;
    mouse_complex_y_after = render_start_canvas_y_max - static_cast<FloatType>(canvas_py_new2) / static_cast<FloatType>(canvas_height) * new_y_range;

    FloatType tol = static_cast<FloatType>(1e-4); // Relax tolerance slightly for float precision
    return std::abs(mouse_complex_x_after - mouse_complex_x_before) <= tol &&
           std::abs(mouse_complex_y_after - mouse_complex_y_before) <= tol;
}

void MandelUI::convert_display_offset_on_swap()
{
    DEBUG_PRINTF("convert_display_offset_on_swap: OLD offset=(%.2f, %.2f)\n", display_offset_x_, display_offset_y_);
    
    // Convert display_offset from old texture (displayed_*) to new (render_start_*) so the same
    // complex coordinate stays under the viewport center (per DRAGGING_REDESIGN.md).
    int vp_w = overscan_viewport_.viewport_width();
    int vp_h = overscan_viewport_.viewport_height();
    int cw = overscan_viewport_.canvas_width();
    int ch = overscan_viewport_.canvas_height();
    int mx = overscan_viewport_.margin_x();
    int my = overscan_viewport_.margin_y();

    // 1. Point of interest: Center of the viewport
    float screen_x = static_cast<float>(vp_w) * 0.5f;
    float screen_y = static_cast<float>(vp_h) * 0.5f;

    // 2. Map to canvas pixel in OLD texture (displayed bounds)
    // Texture Pixel = Margin + Screen Pixel - Display Offset
    // (Dragging Right -> Offset > 0 -> See Left -> Smaller Index -> Margin + Screen - Offset)
    float canvas_px_old = static_cast<float>(mx) + screen_x - display_offset_x_;
    float canvas_py_old = static_cast<float>(my) + screen_y - display_offset_y_;

    // 3. Convert to complex coordinate using old texture bounds
    FloatType old_x_range = displayed_texture_canvas_x_max_ - displayed_texture_canvas_x_min_;
    FloatType old_y_range = displayed_texture_canvas_y_max_ - displayed_texture_canvas_y_min_;
    FloatType complex_x = displayed_texture_canvas_x_min_ +
                          static_cast<FloatType>(canvas_px_old) / static_cast<FloatType>(cw) * old_x_range;
    FloatType complex_y = displayed_texture_canvas_y_max_ -
                          static_cast<FloatType>(canvas_py_old) / static_cast<FloatType>(ch) * old_y_range;

    DEBUG_PRINTF("  Center complex: (%.6Lf, %.6Lf) [using OLD bounds]\n", complex_x, complex_y);

    // 4. Convert complex to canvas pixel in NEW texture (render_start bounds)
    FloatType new_x_range = render_start_canvas_x_max_ - render_start_canvas_x_min_;
    FloatType new_y_range = render_start_canvas_y_max_ - render_start_canvas_y_min_;
    float canvas_px_new = static_cast<float>((complex_x - render_start_canvas_x_min_) / new_x_range * cw);
    float canvas_py_new = static_cast<float>((render_start_canvas_y_max_ - complex_y) / new_y_range * ch);

    DEBUG_PRINTF("  New canvas pixel: (%.2f, %.2f)\n", canvas_px_new, canvas_py_new);

    // 5. New display_offset
    // We want: canvas_px_new = Margin + Screen - New Offset
    // New Offset = Margin + Screen - canvas_px_new
    display_offset_x_ = static_cast<float>(mx) + screen_x - canvas_px_new;
    display_offset_y_ = static_cast<float>(my) + screen_y - canvas_py_new;
    
    DEBUG_PRINTF("  NEW offset=(%.2f, %.2f)\n", display_offset_x_, display_offset_y_);
}

void MandelUI::update_textures()
{
    if (worker_->canvas_.empty())
    {
        return;
    }

    // Convert display_offset from old texture to new so same complex point stays under mouse
    convert_display_offset_on_swap();

    // Upload to back texture
    if (update_texture_func_)
    {
        update_texture_func_(&texture_back_, worker_->canvas_.data(), overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
    }

    // Swap front and back
    std::swap(texture_front_, texture_back_);

    // Update displayed_texture_bounds to what we're now showing
    displayed_texture_canvas_x_min_ = render_start_canvas_x_min_;
    displayed_texture_canvas_x_max_ = render_start_canvas_x_max_;
    displayed_texture_canvas_y_min_ = render_start_canvas_y_min_;
    displayed_texture_canvas_y_max_ = render_start_canvas_y_max_;
}

void MandelUI::draw()
{
    // Poll for async render completion (MandelWorker)
    if (render_pending_ && worker_->try_complete_render())
    {
        update_textures();
        render_pending_ = false;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Handle resize
    if (io.DisplaySize.x != static_cast<float>(overscan_viewport_.viewport_width()) ||
        io.DisplaySize.y != static_cast<float>(overscan_viewport_.viewport_height()))
    {
        handle_resize(static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    }

    // Draw the texture first (background)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    if (texture_front_ != 0)
    {
        OverscanViewport::DrawInfo draw_info = overscan_viewport_.calculate_draw_info(viewport->Pos.x, viewport->Pos.y, display_offset_x_, display_offset_y_, 1.0f, 0.0f, 0.0f);

        // Draw grey background
        ImVec2 viewport_min(viewport->Pos.x, viewport->Pos.y);
        ImVec2 viewport_max(viewport->Pos.x + static_cast<float>(overscan_viewport_.viewport_width()), 
                           viewport->Pos.y + static_cast<float>(overscan_viewport_.viewport_height()));
        draw_list->AddRectFilled(viewport_min, viewport_max, IM_COL32(64, 64, 64, 255));

        // Draw the texture
        float uv_width = draw_info.uv_max_x - draw_info.uv_min_x;
        float uv_height = draw_info.uv_max_y - draw_info.uv_min_y;

        if (uv_width > 0.0f && uv_height > 0.0f)
        {
            float texture_draw_x = draw_info.draw_x + draw_info.texture_offset_x;
            float texture_draw_y = draw_info.draw_y + draw_info.texture_offset_y;

            ImVec2 texture_min(texture_draw_x, texture_draw_y);
            ImVec2 texture_max(texture_draw_x + draw_info.texture_width, texture_draw_y + draw_info.texture_height);
            ImVec2 uv_min(draw_info.uv_min_x, draw_info.uv_min_y);
            ImVec2 uv_max(draw_info.uv_max_x, draw_info.uv_max_y);

            draw_list->AddImage(texture_front_, texture_min, texture_max, uv_min, uv_max);

            // Draw rectangle around canvas area
            float canvas_screen_width = draw_info.texture_width / uv_width;
            float canvas_screen_height = draw_info.texture_height / uv_height;
            float canvas_screen_x = texture_draw_x - (canvas_screen_width * draw_info.uv_min_x);
            float canvas_screen_y = texture_draw_y - (canvas_screen_height * draw_info.uv_min_y);
            ImVec2 canvas_min(canvas_screen_x, canvas_screen_y);
            ImVec2 canvas_max(canvas_screen_x + canvas_screen_width, canvas_screen_y + canvas_screen_height);
            draw_list->AddRect(canvas_min, canvas_max, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
        }
    }

    // Draw control window (so its hover/focus state is set for input)
    control_.draw();

    // Handle pan/drag input after controls: only pan when mouse is over viewport and not over any ImGui window
    handle_input();
}

// MandelUIControlInterface implementation - minimal stubs

ViewState MandelUI::get_viewport_bounds() const
{
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);
    return ViewState(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max, max_iterations_);
}

bool MandelUI::is_render_in_progress() const { return render_pending_; }

int MandelUI::get_max_iterations() const
{
    return max_iterations_;
}

unsigned int MandelUI::get_render_generation() const
{
    return render_generation_.load();
}

bool MandelUI::is_dragging() const
{
    return is_dragging_;
}

bool MandelUI::is_rendering() const
{
    return is_render_in_progress();
}

ViewState MandelUI::get_applied_settings() const { return applied_settings_; }

ViewState MandelUI::get_pending_settings() const { return pending_settings_; }

bool MandelUI::has_pending_settings() const { return has_pending_settings_; }

ViewState MandelUI::get_initial_bounds() const { return ViewState(-2.0L, 0.5L, -1.125L, 1.125L, 512); }

uint64_t MandelUI::get_initial_zoom() const { return 1; }

void MandelUI::set_pending_settings(const ViewState& settings)
{
    pending_settings_ = settings;
    has_pending_settings_ = true;
}

void MandelUI::apply_view_state(const ViewState& state)
{
    convert_viewport_to_canvas_bounds(state.x_min, state.x_max, state.y_min, state.y_max, canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
    max_iterations_ = state.max_iterations;
    applied_settings_ = state;
    has_pending_settings_ = false;
    worker_ = std::make_unique<MandelWorker>(
        overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(), render_generation_,
        canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_, thread_pool_);
    worker_->set_max_iterations(max_iterations_);
    start_render();
}

void MandelUI::save_view_state(const std::string& /*name*/)
{
    // Stub
}

void MandelUI::extend_bounds_for_overscan(FloatType& x_min, FloatType& x_max, FloatType& y_min, FloatType& y_max) const
{
    convert_viewport_to_canvas_bounds(x_min, x_max, y_min, y_max, x_min, x_max, y_min, y_max);
}

void MandelUI::apply_pending_settings_if_ready()
{
    if (has_pending_settings_ && !is_render_in_progress())
    {
        apply_view_state(pending_settings_);
    }
}

void MandelUI::reset_to_initial() { apply_view_state(get_initial_bounds()); }

std::map<std::string, ViewState>& MandelUI::get_saved_views()
{
    static std::map<std::string, ViewState> empty;
    return empty;
}

char* MandelUI::get_new_view_name_buffer()
{
    static char buffer[256] = {0};
    return buffer;
}

}  // namespace mandel
