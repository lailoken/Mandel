#ifndef MANDEL_RENDER_INL
#define MANDEL_RENDER_INL

#include "imgui.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "mandel.hpp"
#include "mandel_render.hpp"
#include "thread_pool.hpp"

namespace mandel
{

// Template helper to map FloatType to ImGuiDataType
template <typename FloatType>
struct ImGuiDataTypeMap;

template <>
struct ImGuiDataTypeMap<float>
{
    static constexpr ImGuiDataType value = ImGuiDataType_Float;
};

template <>
struct ImGuiDataTypeMap<double>
{
    static constexpr ImGuiDataType value = ImGuiDataType_Double;
};

template <>
struct ImGuiDataTypeMap<long double>
{
    // ImGui doesn't have ImGuiDataType_LongDouble, so use Double with conversion
    static constexpr ImGuiDataType value = ImGuiDataType_Double;
};

// Template helper to use InputScalar with appropriate data type
template <typename FloatType>
struct ImGuiInputHelper
{
    static bool input(const char* label, FloatType* v, FloatType step = static_cast<FloatType>(0.0), FloatType step_fast = static_cast<FloatType>(0.0),
                      const char* format = nullptr)
    {
        if constexpr (std::is_same_v<FloatType, long double>)
        {
            // For long double, convert to/from double since ImGui doesn't support it directly
            // Convert format string from long double format to double format if provided
            const char* double_format = format;
            if (format != nullptr)
            {
                // Replace "%.10Lf" with "%.10f" (remove L modifier for double)
                // This is a simple approach - format should be something like "%.10Lf"
                static thread_local char format_buf[16];
                if (strstr(format, "Lf") != nullptr || strstr(format, "Le") != nullptr)
                {
                    // Copy format and replace L with nothing
                    size_t len = strlen(format);
                    if (len < sizeof(format_buf))
                    {
                        strncpy(format_buf, format, sizeof(format_buf) - 1);
                        format_buf[sizeof(format_buf) - 1] = '\0';
                        // Find and replace L
                        for (char* p = format_buf; *p; ++p)
                        {
                            if (*p == 'L' && (p[1] == 'f' || p[1] == 'e' || p[1] == 'g'))
                            {
                                // Remove L by shifting
                                memmove(p, p + 1, strlen(p));
                                break;
                            }
                        }
                        double_format = format_buf;
                    }
                }
            }

            // Validate input value - if NaN or infinite, use 0
            double temp = std::isnan(*v) || std::isinf(*v) ? 0.0 : static_cast<double>(*v);
            double step_d = static_cast<double>(step);
            double step_fast_d = static_cast<double>(step_fast);
            bool result = ImGui::InputScalar(label,
                                             ImGuiDataType_Double,
                                             &temp,
                                             step != static_cast<FloatType>(0.0) ? &step_d : nullptr,
                                             step_fast != static_cast<FloatType>(0.0) ? &step_fast_d : nullptr,
                                             double_format);
            // Validate output - ensure we don't get NaN back
            if (!std::isnan(temp) && !std::isinf(temp))
            {
                *v = static_cast<long double>(temp);
            }
            return result;
        }
        else
        {
            // For float and double, use InputScalar directly
            // Validate input value - if NaN or infinite, reset to 0
            if (std::isnan(*v) || std::isinf(*v))
            {
                *v = static_cast<FloatType>(0.0);
            }
            bool result = ImGui::InputScalar(label,
                                             ImGuiDataTypeMap<FloatType>::value,
                                             v,
                                             step != static_cast<FloatType>(0.0) ? &step : nullptr,
                                             step_fast != static_cast<FloatType>(0.0) ? &step_fast : nullptr,
                                             format);
            // Validate output - ensure we don't get NaN back
            if (std::isnan(*v) || std::isinf(*v))
            {
                *v = static_cast<FloatType>(0.0);
            }
            return result;
        }
    }
};

template <typename FloatType>
ImGuiRenderer<FloatType>::ImGuiRenderer(MandelbrotRenderer<FloatType>* renderer, TextureUpdateCallback update_callback, TextureDeleteCallback delete_callback)
    : renderer_(renderer),
      texture_front_(0),
      texture_back_(0),
      width_(0),
      height_(0),
      pixels_(nullptr),
      pixels_being_updated_(nullptr),
      texture_dirty_(false),
      double_buffering_enabled_(true),
      render_generation_(0),
      pixels_generation_(0),
      swapped_generation_(UINT_MAX),
      update_callback_(update_callback),
      delete_callback_(delete_callback),
      is_dragging_(false),
      last_drag_pos_(0.0f, 0.0f),
      initial_bounds_set_(false),
      initial_x_min_(static_cast<FloatType>(0.0)),
      initial_x_max_(static_cast<FloatType>(0.0)),
      initial_y_min_(static_cast<FloatType>(0.0)),
      initial_y_max_(static_cast<FloatType>(0.0)),
      initial_zoom_(1ULL),
      initial_max_iterations_(0),
      first_window_size_set_(false),
      threading_enabled_(true),
      controls_window_should_be_transparent_(false),
      has_loaded_current_view_(false)
{
    // Initialize new view name buffer
    new_view_name_buffer_[0] = '\0';
    // Load saved views from file on construction
    load_views_from_file();
}

template<typename FloatType>
ImGuiRenderer<FloatType>::~ImGuiRenderer()
{
    // Save current view and all views before destroying (include_current_view = true)
    save_views_to_file(true);

    if (delete_callback_ != nullptr)
    {
        if (texture_front_ != 0)
        {
            delete_callback_(texture_front_);
        }
        if (texture_back_ != 0)
        {
            delete_callback_(texture_back_);
        }
    }
}

template<typename FloatType>
void ImGuiRenderer<FloatType>::swap_buffers()
{
    // Swap front and back buffers
    ImTextureID temp = texture_front_;
    texture_front_ = texture_back_;
    texture_back_ = temp;
}

template<typename FloatType>
void ImGuiRenderer<FloatType>::clear_canvas()
{
    // Clear the canvas by updating the front texture with black pixels
    // This is only used when double buffering is disabled
    if (!double_buffering_enabled_ && texture_front_ != 0 && update_callback_ != nullptr && 
        renderer_ != nullptr && renderer_->get_width() > 0 && renderer_->get_height() > 0)
    {
        int width = renderer_->get_width();
        int height = renderer_->get_height();
        size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        
        // Create temporary black pixel buffer
        std::vector<unsigned char> black_pixels(pixel_count, 0);
        
        // Update the texture with black pixels
        update_callback_(&texture_front_, black_pixels.data(), width, height);
    }
}

template <typename FloatType>
bool ImGuiRenderer<FloatType>::is_render_in_progress() const
{
    if (renderer_ == nullptr)
    {
        return false;
    }

    // Check if texture is dirty - this indicates pixels are being updated
    // This works for both threaded and non-threaded rendering
    if (texture_dirty_)
    {
        return true;
    }

    if (threading_enabled_)
    {
        ThreadPool* pool = renderer_->get_thread_pool();
        if (pool != nullptr)
        {
            // Render is in progress if thread pool is running and not idle
            // Also check if tasks are executing, even if pool appears idle temporarily
            return pool->is_running() && !pool->is_idle();
        }
    }

    return false;
}

template<typename FloatType>
void ImGuiRenderer<FloatType>::on_pixels_updated(const unsigned char* pixels, int width, int height)
{
    // Always update pixels_generation_ to the current render_generation_ when pixels are updated
    // This ensures we track which generation the pixels belong to
    pixels_generation_ = render_generation_;
    
    pixels_ = pixels;
    width_ = width;
    height_ = height;
    texture_dirty_ = true;
}

template<typename FloatType>
void ImGuiRenderer<FloatType>::draw()
{
    // Draw Mandelbrot set on the main app background (not in a window)
    // This must be done before creating other windows so it's drawn behind them
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 viewport_size = io.DisplaySize;

    // Handle resizing: only resize canvas, keep complex plane bounds unchanged
    if (renderer_)
    {
        ImVec2 canvas_size = viewport_size;
        if (canvas_size.x > 0 && canvas_size.y > 0)
        {
            int new_width = static_cast<int>(canvas_size.x);
            int new_height = static_cast<int>(canvas_size.y);

            // Only resize if dimensions changed
            if (renderer_->get_width() != new_width || renderer_->get_height() != new_height)
            {
                // For thread pool: explicitly reset and wait for all tasks to complete
                // This ensures no active tasks before we change dimensions
                if (threading_enabled_)
                {
                    ThreadPool* pool = renderer_->get_thread_pool();
                    if (pool != nullptr && pool->is_running())
                    {
                        pool->reset();  // This waits for all tasks to complete
                    }
                }

                // Invalidate any pending texture updates to prevent using old pixel data
                texture_dirty_ = false;
                pixels_ = nullptr;

                // Initialize bounds on first resize only (if not already set)
                if (!first_window_size_set_)
                {
                    // Store the true initial/default values BEFORE applying any loaded view
                    // This ensures <Reset> always goes back to the program's default state
                    if (!initial_bounds_set_)
                    {
                        // Default Mandelbrot view: x: -2.5 to 1.5, y: -2.0 to 2.0
                        initial_x_min_ = static_cast<FloatType>(-2.5);
                        initial_x_max_ = static_cast<FloatType>(1.5);
                        initial_y_min_ = static_cast<FloatType>(-2.0);
                        initial_y_max_ = static_cast<FloatType>(2.0);
                        initial_zoom_ = 1ULL;
                        // Get the initial max_iterations from renderer (it should have a default)
                        initial_max_iterations_ = renderer_->get_max_iterations();
                        initial_bounds_set_ = true;
                    }

                    // Check if we have a loaded current view
                    if (has_loaded_current_view_)
                    {
                        // Apply the loaded current view
                        renderer_->set_bounds(loaded_current_view_.x_min, loaded_current_view_.x_max, loaded_current_view_.y_min, loaded_current_view_.y_max);
                        renderer_->set_max_iterations(loaded_current_view_.max_iterations);
                        // Calculate zoom from bounds
                        FloatType x_range = loaded_current_view_.x_max - loaded_current_view_.x_min;
                        FloatType y_range = loaded_current_view_.y_max - loaded_current_view_.y_min;
                        FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                        if (avg_range > static_cast<FloatType>(0.0))
                        {
                            FloatType calculated_zoom_f = static_cast<FloatType>(4.0) / avg_range;
                            uint64_t calculated_zoom = static_cast<uint64_t>(calculated_zoom_f);
                            if (calculated_zoom > MandelbrotRenderer<FloatType>::max_zoom)
                            {
                                calculated_zoom = MandelbrotRenderer<FloatType>::max_zoom;
                            }
                            renderer_->set_zoom(calculated_zoom);
                        }
                    }
                    else
                    {
                        // Apply the default initial view
                        renderer_->set_bounds(initial_x_min_, initial_x_max_, initial_y_min_, initial_y_max_);
                        renderer_->set_zoom(initial_zoom_);
                    }
                    first_window_size_set_ = true;
                }

                // Resize the canvas only - complex plane bounds remain unchanged
                render_generation_++;
                if (!double_buffering_enabled_)
                {
                    clear_canvas();
                }
                ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                renderer_->init(new_width, new_height, pool);
            }
        }
    }

    // Draw Mandelbrot set on the background and create input window BEFORE controls window
    // This ensures the input window is behind the controls window
    ImTextureID display_texture = texture_front_;
    if (display_texture != 0 && renderer_ && width_ > 0 && height_ > 0)
    {
        ImVec2 image_size(static_cast<float>(width_), static_cast<float>(height_));

        // Ensure image size is valid (ImGui requires non-zero dimensions)
        if (image_size.x > 0.0f && image_size.y > 0.0f)
        {
            // Draw the image directly on the background using the background draw list
            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            ImVec2 image_min(0.0f, 0.0f);
            ImVec2 image_max(image_size.x, image_size.y);
            draw_list->AddImage(display_texture, image_min, image_max);

            // Create an invisible full-screen window to capture mouse input on the background
            // This window must be created BEFORE the controls window so it's behind it
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(viewport_size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));  // Transparent background
            ImGui::Begin("Canvas Input",
                         nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::InvisibleButton("canvas_interaction", viewport_size);

            // Handle interactive view control (dragging and zooming)
            // Check item state directly - if the item is active, we clicked on it
            // The item state correctly reflects whether we're interacting with the input area
            bool is_hovered = ImGui::IsItemHovered();
            bool is_active = ImGui::IsItemActive();
            bool is_double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::End();

            // Handle double-click to center FIRST (before dragging logic)
            // This ensures double-click is processed even if dragging state would interfere
            if (is_double_clicked && !is_dragging_)
            {
                // Get mouse position relative to the viewport
                ImVec2 mouse_pos = ImGui::GetMousePos();

                // Clamp mouse position to viewport bounds
                float mouse_x = std::max(0.0f, std::min(mouse_pos.x, viewport_size.x));
                float mouse_y = std::max(0.0f, std::min(mouse_pos.y, viewport_size.y));

                // Convert to pixel coordinates within the image
                float pixel_x = (mouse_x / viewport_size.x) * static_cast<float>(width_);
                float pixel_y = (mouse_y / viewport_size.y) * static_cast<float>(height_);

                // Get current bounds
                FloatType current_x_min = renderer_->get_x_min();
                FloatType current_x_max = renderer_->get_x_max();
                FloatType current_y_min = renderer_->get_y_min();
                FloatType current_y_max = renderer_->get_y_max();

                // Convert pixel coordinates to complex plane coordinates
                // Pixel (0, 0) is top-left of viewport, maps to complex (x_min, y_max)
                // Pixel (width, height) is bottom-right, maps to complex (x_max, y_min)
                FloatType x_range = current_x_max - current_x_min;
                FloatType y_range = current_y_max - current_y_min;
                FloatType pixel_to_x = x_range / static_cast<FloatType>(width_);
                FloatType pixel_to_y = y_range / static_cast<FloatType>(height_);

                // Convert pixel coordinates to complex plane coordinates
                // X: pixel 0 (left) -> x_min, pixel width (right) -> x_max
                FloatType click_complex_x = current_x_min + pixel_to_x * static_cast<FloatType>(pixel_x);
                // Y: pixel 0 (top) -> y_max, pixel height (bottom) -> y_min (inverted)
                FloatType click_complex_y = current_y_max - pixel_to_y * static_cast<FloatType>(pixel_y);

                // Calculate current center of view
                FloatType current_center_x = (current_x_min + current_x_max) / static_cast<FloatType>(2.0);
                FloatType current_center_y = (current_y_min + current_y_max) / static_cast<FloatType>(2.0);

                // Calculate offset needed to move clicked point to center
                // X: If clicked point is to the right of center, we need to move view right (positive offset)
                FloatType offset_x = click_complex_x - current_center_x;
                // Y: Note that click_complex_y uses inverted Y (top of screen = y_max, bottom = y_min)
                // So if clicked point is above center (click_complex_y > current_center_y), we need negative offset
                FloatType offset_y = current_center_y - click_complex_y;

                // Calculate new bounds (centered on clicked point)
                FloatType new_x_min = current_x_min + offset_x;
                FloatType new_x_max = current_x_max + offset_x;
                FloatType new_y_min = current_y_min + offset_y;
                FloatType new_y_max = current_y_max + offset_y;

                // Update bounds and regenerate
                // When double-buffering, wait for current render to complete before starting new one
                if (double_buffering_enabled_ && is_render_in_progress())
                {
                    // Defer starting new render until current one completes
                    // Just update bounds for now, render will start next frame when current one is done
                    renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                }
                else
                {
                    // Double-check one more time right before we actually start the render
                    // This prevents race conditions where a render might have started between checks
                    if (double_buffering_enabled_ && is_render_in_progress())
                    {
                        // Render started between checks, just update bounds and defer
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                    }
                    else
                    {
                        render_generation_++;
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        if (!double_buffering_enabled_)
                        {
                            clear_canvas();
                        }
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                    }
                }

                // Reset dragging state to prevent interference
                is_dragging_ = false;
            }
            else if (is_hovered || is_active)
            {
                // Get mouse position relative to the viewport
                ImVec2 mouse_pos = ImGui::GetMousePos();

                // Clamp mouse position to viewport bounds
                float mouse_x = std::max(0.0f, std::min(mouse_pos.x, viewport_size.x));
                float mouse_y = std::max(0.0f, std::min(mouse_pos.y, viewport_size.y));

                // Convert to pixel coordinates within the image
                // Image fills the entire viewport
                float pixel_x = (mouse_x / viewport_size.x) * static_cast<float>(width_);
                float pixel_y = (mouse_y / viewport_size.y) * static_cast<float>(height_);

                // Handle dragging - simplified detection
                if (is_active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    // Start dragging when mouse button is first pressed
                    if (!is_dragging_)
                    {
                        is_dragging_ = true;
                        last_drag_pos_ = ImVec2(mouse_x, mouse_y);
                    }

                    // Only regenerate if mouse has actually moved (with small threshold to avoid float precision issues)
                    const float move_threshold = 1.0f;
                    ImVec2 current_pos(mouse_x, mouse_y);
                    float move_distance = std::sqrt((current_pos.x - last_drag_pos_.x) * (current_pos.x - last_drag_pos_.x) +
                                                    (current_pos.y - last_drag_pos_.y) * (current_pos.y - last_drag_pos_.y));

                    // When double-buffering, only regenerate if current render is complete
                    // Otherwise, wait for it to finish before starting new render
                    if (move_distance >= move_threshold)
                    {
                        // Get current bounds and calculate incremental delta
                        FloatType current_x_min = renderer_->get_x_min();
                        FloatType current_x_max = renderer_->get_x_max();
                        FloatType current_y_min = renderer_->get_y_min();
                        FloatType current_y_max = renderer_->get_y_max();

                        // Calculate incremental pixel delta from last position
                        ImVec2 drag_delta(current_pos.x - last_drag_pos_.x, current_pos.y - last_drag_pos_.y);

                        // Convert pixel delta to complex plane delta
                        FloatType x_range = current_x_max - current_x_min;
                        FloatType y_range = current_y_max - current_y_min;
                        FloatType pixel_to_x = x_range / static_cast<FloatType>(width_);
                        FloatType pixel_to_y = y_range / static_cast<FloatType>(height_);

                        // Pan in opposite direction (drag right = view moves left, drag down = view moves down)
                        FloatType delta_x = -static_cast<FloatType>(drag_delta.x) * pixel_to_x;
                        FloatType delta_y = -static_cast<FloatType>(drag_delta.y) * pixel_to_y;

                        FloatType new_x_min = current_x_min + delta_x;
                        FloatType new_x_max = current_x_max + delta_x;
                        FloatType new_y_min = current_y_min + delta_y;
                        FloatType new_y_max = current_y_max + delta_y;

                        // Calculate pixel offsets for optimized panning
                        // Only use panning optimization when double buffering is enabled
                        // Without double buffering, the canvas may not be reliable for pixel reuse
                        int pan_dx = 0;
                        int pan_dy = 0;
                        if (double_buffering_enabled_)
                        {
                            // drag_delta is in screen pixels, and represents how much the mouse moved
                            // Positive drag_delta.x means dragged right (view moves left, pixels shift right)
                            // Positive drag_delta.y means dragged down (view moves up, pixels shift down)
                            // Pixel shift direction matches drag direction to reuse existing pixels
                            pan_dx = static_cast<int>(std::round(drag_delta.x));
                            pan_dy = static_cast<int>(std::round(drag_delta.y));

                            // Only use panning if the pan is not too large to reuse existing data
                            if (std::abs(pan_dx) >= width_ || std::abs(pan_dy) >= height_)
                            {
                                pan_dx = 0;
                                pan_dy = 0;
                            }
                        }

                        // When double-buffering, only start new render if current render is complete
                        // This ensures renders complete and display before starting new ones
                        // Check right before calling regenerate() to avoid race conditions
                        bool can_start_new_render = !double_buffering_enabled_ || !is_render_in_progress();
                        if (can_start_new_render)
                        {
                            // Double-check one more time right before we actually start the render
                            // This prevents race conditions where a render might have started between checks
                            if (double_buffering_enabled_ && is_render_in_progress())
                            {
                                // Render started between checks, skip this frame
                                // Will try again next frame when render completes
                            }
                            else
                            {
                                // Update bounds and regenerate
                                render_generation_++;
                                renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                                if (!double_buffering_enabled_)
                                {
                                    clear_canvas();
                                }
                                ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                                renderer_->regenerate(pool, pan_dx, pan_dy);

                                // Remember this position so we don't regenerate again until mouse moves more
                                last_drag_pos_ = current_pos;
                            }
                        }
                        // else: render in progress, will try again next frame when render completes
                    }
                }
                else
                {
                    // Stop dragging when button is released or item is no longer active
                    is_dragging_ = false;
                }

                // Handle scroll wheel zoom
                // When double-buffering, wait for current render to complete before starting new one
                float current_wheel = io.MouseWheel;
                if (std::abs(current_wheel) > 0.01f)
                {
                    // If double-buffering and render in progress, defer zoom until next frame
                    if (double_buffering_enabled_ && is_render_in_progress())
                    {
                        // Defer zoom - will be processed next frame when render completes
                    }
                    else
                    {
                        // Get current bounds
                        FloatType current_x_min = renderer_->get_x_min();
                        FloatType current_x_max = renderer_->get_x_max();
                        FloatType current_y_min = renderer_->get_y_min();
                        FloatType current_y_max = renderer_->get_y_max();

                        // Calculate complex plane coordinates at mouse position
                        FloatType x_range = current_x_max - current_x_min;
                        FloatType y_range = current_y_max - current_y_min;
                        FloatType pixel_to_x = x_range / static_cast<FloatType>(width_);
                        FloatType pixel_to_y = y_range / static_cast<FloatType>(height_);

                        // Complex plane coordinates at mouse position
                        FloatType mouse_complex_x = current_x_min + pixel_to_x * static_cast<FloatType>(pixel_x);
                        FloatType mouse_complex_y = current_y_min + pixel_to_y * static_cast<FloatType>(pixel_y);

                        // Zoom factor (positive wheel = zoom in)
                        FloatType zoom_factor = static_cast<FloatType>(1.0) + static_cast<FloatType>(current_wheel) * static_cast<FloatType>(zoom_step_);

                        // Get max zoom from template constant
                        constexpr uint64_t max_zoom = MandelbrotRenderer<FloatType>::max_zoom;
                        constexpr FloatType min_range = static_cast<FloatType>(4.0) / static_cast<FloatType>(max_zoom);

                        // Limit zoom_factor to prevent ranges from going below minimum
                        FloatType max_zoom_factor_x = x_range / min_range;
                        FloatType max_zoom_factor_y = y_range / min_range;
                        FloatType max_zoom_factor = std::min(max_zoom_factor_x, max_zoom_factor_y);

                        // Clamp zoom_factor to maximum allowed
                        if (zoom_factor > max_zoom_factor)
                        {
                            zoom_factor = max_zoom_factor;
                        }

                        // Calculate new range (smaller range = more zoomed in)
                        FloatType new_x_range = x_range / zoom_factor;
                        FloatType new_y_range = y_range / zoom_factor;

                        // Adjust bounds so mouse position stays at the same complex coordinates
                        FloatType mouse_x_ratio = static_cast<FloatType>(pixel_x) / static_cast<FloatType>(width_);
                        FloatType mouse_y_ratio = static_cast<FloatType>(pixel_y) / static_cast<FloatType>(height_);

                        FloatType new_x_min = mouse_complex_x - mouse_x_ratio * new_x_range;
                        FloatType new_x_max = new_x_min + new_x_range;
                        FloatType new_y_min = mouse_complex_y - mouse_y_ratio * new_y_range;
                        FloatType new_y_max = new_y_min + new_y_range;

                        // Update bounds and zoom (zoom is derived from range)
                        FloatType avg_range = (new_x_range + new_y_range) / static_cast<FloatType>(2.0);
                        FloatType new_zoom_f = static_cast<FloatType>(4.0) / avg_range;
                        uint64_t new_zoom = static_cast<uint64_t>(new_zoom_f);

                        // Clamp zoom to maximum (safety check)
                        if (new_zoom > max_zoom)
                        {
                            new_zoom = max_zoom;
                        }

                        // Double-check one more time right before we actually start the render
                        // This prevents race conditions where a render might have started between checks
                        if (double_buffering_enabled_ && is_render_in_progress())
                        {
                            // Render started between checks, defer zoom until next frame
                        }
                        else
                        {
                            render_generation_++;
                            renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                            renderer_->set_zoom(new_zoom);
                            if (!double_buffering_enabled_)
                            {
                                clear_canvas();
                            }
                            ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                            renderer_->regenerate(pool);
                        }
                    }
                }
            }
            else
            {
                // Reset dragging state if mouse leaves the image
                is_dragging_ = false;
            }
        }
    }

    // Create controls window (after background setup so it appears on top)
    // Set window alpha based on previous frame's focus/hover state
    if (controls_window_should_be_transparent_)
    {
        ImGui::SetNextWindowBgAlpha(0.5f);
    }
    else
    {
        ImGui::SetNextWindowBgAlpha(1.0f);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(350.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0f, 300.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Controls"))
    {
        // Check if window is focused or hovered for next frame
        bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        controls_window_should_be_transparent_ = !is_focused && !is_hovered;

        if (renderer_)
        {
            // Initial bounds are now stored when renderer is first initialized, not here

            // Display controls using ImGui widgets
            // Use FloatType directly with appropriate ImGui input functions
            bool changed = false;
            int max_iter = renderer_->get_max_iterations();
            FloatType x_min = renderer_->get_x_min();
            FloatType x_max = renderer_->get_x_max();
            FloatType y_min = renderer_->get_y_min();
            FloatType y_max = renderer_->get_y_max();
            FloatType zoom = renderer_->get_zoom();

            // Get max zoom for clamping
            static const FloatType max_zoom = MandelbrotRenderer<FloatType>::max_zoom;
            static const FloatType min_zoom = static_cast<FloatType>(0.1);

            changed |= ImGui::SliderInt("Max Iterations", &max_iter, 2, 4096, "%d", ImGuiSliderFlags_Logarithmic);

            ImGui::PushItemWidth(120.f);
            // Use the larger epsilon between float and FloatType - scaled for practical UI use
            // Epsilon is of FloatType
            constexpr FloatType eps = std::numeric_limits<FloatType>::epsilon() * static_cast<FloatType>(100.0);

            // Format string based on FloatType precision
            // Use higher precision for more significant digits
            const char* format_str = std::is_same_v<FloatType, long double> ? "%.16Lf" : (std::is_same_v<FloatType, double> ? "%.13f" : "%.8f");
            FloatType step = static_cast<FloatType>(0.0);

            ImGui::TextUnformatted("Min:");
            ImGui::SameLine();
            bool x_min_edited = ImGuiInputHelper<FloatType>::input(",##min_x", &x_min, step, step, format_str);
            if (x_min_edited && x_min >= x_max)
            {
                changed = true;
                if (x_max <= x_min)
                {
                    x_max = x_min + eps;
                }
            }
            changed |= x_min_edited;

            ImGui::SameLine();
            bool y_min_edited = ImGuiInputHelper<FloatType>::input(",##min_y", &y_min, step, step, format_str);
            if (y_min_edited)
            {
                changed = true;
                if (y_max <= y_min)
                {
                    y_max = y_min + eps;
                }
            }

            ImGui::TextUnformatted("Max:");
            ImGui::SameLine();

            bool x_max_edited = ImGuiInputHelper<FloatType>::input(",##max_x", &x_max, step, step, format_str);
            if (x_max_edited)
            {
                changed = true;
                if (x_min >= x_max)
                {
                    x_min = x_max - eps;
                }
            }

            ImGui::SameLine();
            bool y_max_edited = ImGuiInputHelper<FloatType>::input(",##max_y", &y_max, step, step, format_str);
            if (y_max_edited)
            {
                changed = true;
                if (y_min >= y_max)
                {
                    y_min = y_max - eps;
                }
            }
            ImGui::PopItemWidth();

            double zoom_d = static_cast<double>(zoom);
            double const min_zoom_d = static_cast<double>(min_zoom);
            double const max_zoom_d = static_cast<double>(max_zoom);
            bool zoom_edited = ImGui::SliderScalar("Zoom", ImGuiDataType_Double, &zoom_d, &min_zoom_d, &max_zoom_d, "%.4f", ImGuiSliderFlags_Logarithmic);
            if (zoom_edited)
            {
                changed = true;
                zoom = static_cast<FloatType>(zoom_d);
                zoom = std::clamp(zoom, min_zoom, max_zoom);
            }

            changed |= ImGui::Checkbox("Threading", &threading_enabled_);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Double Buffering", &double_buffering_enabled_);

            ImGui::Text("Render Generation: %d", render_generation_);

            bool thread_pool_active = !renderer_->get_thread_pool()->is_idle();

            ImGui::BeginDisabled();
            ImGui::Checkbox("Thread Pool Active", &thread_pool_active);
            ImGui::SameLine();
            ImGui::Checkbox("Dragging", &is_dragging_);
            ImGui::SameLine();
            ImGui::Checkbox("Texture Dirty", &texture_dirty_);
            ImGui::EndDisabled();

            // Saved Views section
            ImGui::Separator();
            ImGui::Text("Saved Views");

            // Input for new view name and save button
            ImGui::PushItemWidth(200.0f);
            ImGui::InputText("##NewViewName", new_view_name_buffer_, sizeof(new_view_name_buffer_));
            ImGui::SameLine();
            std::string new_view_name(new_view_name_buffer_);
            bool can_save = !new_view_name.empty() && saved_views_.find(new_view_name) == saved_views_.end() && renderer_ != nullptr;
            if (!can_save)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Save Current View"))
            {
                // Save current view state
                ViewState<FloatType> current_state(renderer_->get_x_min(), renderer_->get_x_max(), renderer_->get_y_min(), renderer_->get_y_max(), renderer_->get_max_iterations());
                saved_views_[new_view_name] = current_state;
                new_view_name_buffer_[0] = '\0';  // Clear the buffer
                // Save immediately on manual edit
                save_views_to_file(false);  // Don't save current view (only named views)
            }
            if (!can_save)
            {
                ImGui::EndDisabled();
            }
            ImGui::PopItemWidth();

            // Table of saved views
            ImGui::Spacing();

            ImGui::BeginDisabled(double_buffering_enabled_ && is_render_in_progress());
            ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable | ImGuiTableFlags_NoSavedSettings;
            if (ImGui::BeginTable("SavedViews", 5, table_flags, ImVec2(0, -FLT_MIN)))
            {
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 80);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 180);
                ImGui::TableSetupColumn("Iterations", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("X Range", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 150);
                ImGui::TableSetupColumn("Y Range", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 150);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                // Pseudo item for reset state (cannot be deleted or set, but can be applied)
                ImGui::TableNextRow();
                if (ImGui::TableSetColumnIndex(1))  // Name column - skip the actions column
                {
                    if (ImGui::Selectable("<Initial>", false, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        // Apply reset state
                        render_generation_++;
                        if (!double_buffering_enabled_)
                        {
                            clear_canvas();
                        }
                        renderer_->set_bounds(initial_x_min_, initial_x_max_, initial_y_min_, initial_y_max_);
                        renderer_->set_zoom(initial_zoom_);
                        renderer_->set_max_iterations(initial_max_iterations_);
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                    }
                }

                const char* view_format_str = "%.8f to %.8f";

                if (ImGui::TableNextColumn())  // Iterations
                {
                    ImGui::Text("%d", initial_max_iterations_);
                }

                if (ImGui::TableNextColumn())  // X Range
                {
                    ImGui::Text(view_format_str, static_cast<double>(initial_x_min_), static_cast<double>(initial_x_max_));
                }

                if (ImGui::TableNextColumn())  // Y Range
                {
                    ImGui::Text(view_format_str, static_cast<double>(initial_y_min_), static_cast<double>(initial_y_max_));
                }

                // Saved views
                for (auto it = saved_views_.begin(); it != saved_views_.end();)
                {
                    ImGui::TableNextRow();

                    bool delete_clicked = false;

                    const std::string& name = it->first;
                    const ViewState<FloatType>& state = it->second;

                    if (ImGui::TableNextColumn())  // Actions
                    {
                        ImGui::PushID(name.c_str());
                        if (ImGui::Button("[X]"))
                        {
                            delete_clicked = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("[Save]"))
                        {
                            save_view_state(name);
                        }
                        ImGui::PopID();
                    }

                    if (ImGui::TableNextColumn())  // Name
                    {
                        if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            apply_view_state(state);
                        }
                    }

                    if (ImGui::TableNextColumn())  // Iterations
                    {
                        ImGui::Text("%d", state.max_iterations);
                    }

                    if (ImGui::TableNextColumn())  // X Range
                    {
                        ImGui::Text(view_format_str, static_cast<double>(state.x_min), static_cast<double>(state.x_max));
                    }

                    if (ImGui::TableNextColumn())  // Y Range
                    {
                        ImGui::Text(view_format_str, static_cast<double>(state.y_min), static_cast<double>(state.y_max));
                    }

                    // Handle delete button
                    if (delete_clicked)
                    {
                        it = saved_views_.erase(it);
                        // Save immediately on manual edit
                        save_views_to_file(false);  // Don't save current view (only named views)
                    }
                    else
                    {
                        ++it;
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndDisabled();

            if (changed)
            {
                // Parameters changed - when double-buffering, wait for current render to complete before starting new one
                if (double_buffering_enabled_ && is_render_in_progress())
                {
                    // Defer parameter change render until current render completes
                    // The changed flag will remain true, so we'll try again next frame
                }
                else
                {
                    // Parameters changed - increment render generation to mark new render starting
                    render_generation_++;

                    // Check what changed using floating point comparison with epsilon
                    // Use FloatType for epsilon to match precision
                    const FloatType eps =
                        std::max(static_cast<FloatType>(std::numeric_limits<float>::epsilon()), std::numeric_limits<FloatType>::epsilon()) * static_cast<FloatType>(100.0);
                    FloatType old_x_min = renderer_->get_x_min();
                    FloatType old_x_max = renderer_->get_x_max();
                    FloatType old_y_min = renderer_->get_y_min();
                    FloatType old_y_max = renderer_->get_y_max();
                    FloatType old_zoom = renderer_->get_zoom();

                    bool x_min_changed = std::abs(x_min - old_x_min) > eps;
                    bool x_max_changed = std::abs(x_max - old_x_max) > eps;
                    bool y_min_changed = std::abs(y_min - old_y_min) > eps;
                    bool y_max_changed = std::abs(y_max - old_y_max) > eps;
                    bool zoom_changed = (zoom != old_zoom);
                    bool bounds_changed = x_min_changed || x_max_changed || y_min_changed || y_max_changed;

                    if (zoom_changed && !bounds_changed)
                    {
                        // Only zoom changed, adjust bounds around center of current view
                        // Clamp zoom to max_zoom
                        FloatType center_x = (old_x_min + old_x_max) / static_cast<FloatType>(2.0);
                        FloatType center_y = (old_y_min + old_y_max) / static_cast<FloatType>(2.0);
                        FloatType scale = static_cast<FloatType>(4.0) / zoom;
                        FloatType new_x_min = center_x - scale / static_cast<FloatType>(2.0);
                        FloatType new_x_max = center_x + scale / static_cast<FloatType>(2.0);
                        FloatType new_y_min = center_y - scale / static_cast<FloatType>(2.0);
                        FloatType new_y_max = center_y + scale / static_cast<FloatType>(2.0);
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        renderer_->set_zoom(zoom);
                    }
                    else if (bounds_changed)
                    {
                        // Bounds changed directly - use them exactly as specified
                        // Validate and ensure min < max for both X and Y
                        FloatType final_x_min = x_min;
                        FloatType final_x_max = x_max;
                        FloatType final_y_min = y_min;
                        FloatType final_y_max = y_max;

                        // Ensure min < max
                        if (final_x_min >= final_x_max)
                        {
                            // Invalid range, swap them
                            std::swap(final_x_min, final_x_max);
                        }
                        if (final_y_min >= final_y_max)
                        {
                            // Invalid range, swap them
                            std::swap(final_y_min, final_y_max);
                        }

                        // Set bounds exactly as specified (or corrected)
                        renderer_->set_bounds(final_x_min, final_x_max, final_y_min, final_y_max);

                        // Calculate zoom from average range for display purposes only (doesn't affect rendering)
                        FloatType x_range = final_x_max - final_x_min;
                        FloatType y_range = final_y_max - final_y_min;
                        FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                        if (avg_range > static_cast<FloatType>(0.0))
                        {
                            FloatType calculated_zoom_f = static_cast<FloatType>(4.0) / avg_range;
                            uint64_t calculated_zoom = static_cast<uint64_t>(calculated_zoom_f);
                            // Clamp to max_zoom
                            if (calculated_zoom > MandelbrotRenderer<FloatType>::max_zoom)
                            {
                                calculated_zoom = MandelbrotRenderer<FloatType>::max_zoom;
                            }
                            renderer_->set_zoom(calculated_zoom);
                        }
                    }
                    else if (zoom_changed)
                    {
                        // Only zoom changed (without bounds), adjust bounds around center
                        // This branch should rarely execute since we handle zoom-only above
                        // Clamp zoom to max_zoom
                        uint64_t clamped_zoom = std::min(zoom, MandelbrotRenderer<FloatType>::max_zoom);
                        FloatType center_x = (old_x_min + old_x_max) / static_cast<FloatType>(2.0);
                        FloatType center_y = (old_y_min + old_y_max) / static_cast<FloatType>(2.0);
                        FloatType scale = static_cast<FloatType>(4.0) / static_cast<FloatType>(clamped_zoom);
                        FloatType new_x_min = center_x - scale / static_cast<FloatType>(2.0);
                        FloatType new_x_max = center_x + scale / static_cast<FloatType>(2.0);
                        FloatType new_y_min = center_y - scale / static_cast<FloatType>(2.0);
                        FloatType new_y_max = center_y + scale / static_cast<FloatType>(2.0);
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        renderer_->set_zoom(clamped_zoom);
                    }

                    // Double-check one more time right before we actually start the render
                    // This prevents race conditions where a render might have started between checks
                    if (double_buffering_enabled_ && is_render_in_progress())
                    {
                        // Render started between checks, defer parameter change render until next frame
                        // The changed flag will remain true, so we'll try again next frame
                    }
                    else
                    {
                        renderer_->set_max_iterations(max_iter);
                        if (!double_buffering_enabled_)
                        {
                            clear_canvas();
                        }
                        // Pass thread pool if threading is enabled, otherwise pass nullptr
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                    }
                }
            }
        }
    }
    ImGui::End();

    // Update texture if needed using platform-specific callback
    // Only update if we have valid pixels and dimensions match renderer
    if (texture_dirty_ && pixels_ != nullptr && update_callback_ != nullptr && renderer_ != nullptr && width_ > 0 && height_ > 0 && width_ == renderer_->get_width() &&
        height_ == renderer_->get_height())
    {
        // Store the pixels pointer we're about to update to detect interruptions
        const unsigned char* pixels_to_update = pixels_;

        // Determine target texture
        ImTextureID* target_texture = nullptr;
        if (double_buffering_enabled_)
        {
            // When double buffering is enabled:
            // - If front buffer doesn't exist yet, update it (first render)
            // - Otherwise, update the back buffer (subsequent renders)
            if (texture_front_ == 0)
            {
                target_texture = &texture_front_;
            }
            else
            {
                target_texture = &texture_back_;
            }
        }
        else
        {
            // Single buffering - always update front buffer
            target_texture = &texture_front_;
        }

        pixels_being_updated_ = pixels_to_update;
        update_callback_(target_texture, pixels_to_update, width_, height_);

        // Only mark as not dirty if the update wasn't interrupted
        // (i.e., if pixels_ hasn't changed to a new render)
        if (pixels_ == pixels_to_update)
        {
            texture_dirty_ = false;
        }
        // else: pixels_ changed during update, meaning a new render started
        // Keep texture_dirty_ true so we update again next frame with the new pixels

        pixels_being_updated_ = nullptr;
    }

    // Check if we can swap buffers (only when double-buffering is enabled)
    // Swapping should only happen after ALL updates for a render are complete
    // Allow completed renders to swap even if a newer render has been requested
    if (double_buffering_enabled_ && pixels_ != nullptr)
    {
        // Check if thread pool tasks have all completed (pool is running and idle)
        // This ensures tasks completed naturally, not because reset() was called
        bool thread_pool_complete = false;
        if (renderer_ != nullptr)
        {
            if (threading_enabled_)
            {
                ThreadPool* pool = renderer_->get_thread_pool();
                if (pool != nullptr)
                {
                    // Tasks are complete if pool is running (not stopped) AND idle (all tasks done)
                    thread_pool_complete = pool->is_running() && pool->is_idle();
                }
                else
                {
                    thread_pool_complete = !texture_dirty_;
                }
            }
            else
            {
                // No thread pool means synchronous rendering - all updates complete when texture_dirty_ is false
                // For synchronous rendering, if texture is not dirty, all updates are done
                thread_pool_complete = !texture_dirty_;
            }
        }

        // Check if the current pixels represent a completed render that hasn't been swapped yet
        // Allow swapping for any completed render generation, not just the latest one
        // This ensures renders complete and swap even if a new render was requested
        bool can_swap_for_generation = false;
        if (thread_pool_complete && pixels_generation_ != UINT_MAX && pixels_generation_ != swapped_generation_)
        {
            // This generation completed and hasn't been swapped yet - allow it to swap
            can_swap_for_generation = true;
        }

        // For double buffering to swap, we need:
        // 1. Thread pool tasks completed naturally (if using thread pool) OR texture not dirty (if synchronous)
        // 2. Back buffer exists (we've updated it)
        // 3. The pixels represent a completed generation that hasn't been swapped yet

        // Normal swap: Both buffers exist and all tasks are complete
        if (can_swap_for_generation && texture_back_ != 0 && texture_front_ != 0)
        {
            swap_buffers();
            swapped_generation_ = pixels_generation_;
        }
        // Initial swap: First render with double buffering - initialize front buffer
        else if (can_swap_for_generation && texture_back_ != 0 && texture_front_ == 0)
        {
            swap_buffers();
            swapped_generation_ = pixels_generation_;
        }
    }

    // Views are saved immediately on manual edits, not here
}

template <typename FloatType>
void ImGuiRenderer<FloatType>::apply_view_state(const ViewState<FloatType>& state)
{
    if (renderer_ == nullptr)
    {
        return;
    }

    // Set max_iterations immediately so it's applied even if render is deferred
    // This ensures the UI reflects the correct value right away
    renderer_->set_max_iterations(state.max_iterations);

    // When double-buffering, wait for current render to complete before starting new one
    if (double_buffering_enabled_ && is_render_in_progress())
    {
        // Defer bounds/zoom update until current render completes
        // Max iterations already set above
        return;
    }

    // Double-check one more time right before we actually start the render
    if (double_buffering_enabled_ && is_render_in_progress())
    {
        // Render started between checks, defer bounds/zoom update until next frame
        // Max iterations already set above
        return;
    }

    render_generation_++;
    if (!double_buffering_enabled_)
    {
        clear_canvas();
    }
    renderer_->set_bounds(state.x_min, state.x_max, state.y_min, state.y_max);
    // Calculate zoom from bounds
    FloatType x_range = state.x_max - state.x_min;
    FloatType y_range = state.y_max - state.y_min;
    FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
    if (avg_range > static_cast<FloatType>(0.0))
    {
        FloatType calculated_zoom_f = static_cast<FloatType>(4.0) / avg_range;
        uint64_t calculated_zoom = static_cast<uint64_t>(calculated_zoom_f);
        if (calculated_zoom > MandelbrotRenderer<FloatType>::max_zoom)
        {
            calculated_zoom = MandelbrotRenderer<FloatType>::max_zoom;
        }
        renderer_->set_zoom(calculated_zoom);
    }
    ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
    renderer_->regenerate(pool);
}

template <typename FloatType>
void ImGuiRenderer<FloatType>::save_view_state(const std::string& name)
{
    if (renderer_ == nullptr)
    {
        return;
    }

    // Check if the view exists in saved_views_
    auto it = saved_views_.find(name);
    if (it != saved_views_.end())
    {
        // Update the existing view with current renderer state
        ViewState<FloatType> current_state(renderer_->get_x_min(), renderer_->get_x_max(), renderer_->get_y_min(), renderer_->get_y_max(), renderer_->get_max_iterations());
        it->second = current_state;
        // Save immediately on manual edit
        save_views_to_file(false);  // Don't save current view (only named views)
    }
}

template <typename FloatType>
std::string ImGuiRenderer<FloatType>::get_config_file_path() const
{
    const char* home = std::getenv("HOME");
    if (home != nullptr)
    {
        return std::string(home) + "/.mandel";
    }
    // Fallback to current directory if HOME is not set
    return ".mandel";
}

template <typename FloatType>
void ImGuiRenderer<FloatType>::save_views_to_file(bool include_current_view)
{
    try
    {
        std::string config_path = get_config_file_path();
        nlohmann::json json_data;
        json_data["views"] = nlohmann::json::array();

        for (const auto& [name, state] : saved_views_)
        {
            nlohmann::json view_obj;
            view_obj["name"] = name;
            view_obj["x_min"] = static_cast<double>(state.x_min);
            view_obj["x_max"] = static_cast<double>(state.x_max);
            view_obj["y_min"] = static_cast<double>(state.y_min);
            view_obj["y_max"] = static_cast<double>(state.y_max);
            view_obj["max_iterations"] = state.max_iterations;
            json_data["views"].push_back(view_obj);
        }

        // Save current view state only if requested (on exit)
        if (include_current_view && renderer_ != nullptr)
        {
            nlohmann::json current_view;
            current_view["x_min"] = static_cast<double>(renderer_->get_x_min());
            current_view["x_max"] = static_cast<double>(renderer_->get_x_max());
            current_view["y_min"] = static_cast<double>(renderer_->get_y_min());
            current_view["y_max"] = static_cast<double>(renderer_->get_y_max());
            current_view["max_iterations"] = renderer_->get_max_iterations();
            json_data["current_view"] = current_view;
        }

        std::ofstream file(config_path);
        if (file.is_open())
        {
            file << json_data.dump(2);  // Pretty print with 2-space indentation
            file.close();
        }
    }
    catch (const std::exception& e)
    {
        // Silently fail - user can still use views in this session
    }
}

template <typename FloatType>
void ImGuiRenderer<FloatType>::load_views_from_file()
{
    try
    {
        std::string config_path = get_config_file_path();
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            return;  // File doesn't exist yet, that's okay
        }

        nlohmann::json json_data;
        file >> json_data;
        file.close();

        if (json_data.contains("views") && json_data["views"].is_array())
        {
            saved_views_.clear();
            for (const auto& view_obj : json_data["views"])
            {
                if (view_obj.contains("name") && view_obj.contains("x_min") && view_obj.contains("x_max") && view_obj.contains("y_min") && view_obj.contains("y_max") &&
                    view_obj.contains("max_iterations"))
                {
                    std::string name = view_obj["name"];
                    FloatType x_min = static_cast<FloatType>(view_obj["x_min"].get<double>());
                    FloatType x_max = static_cast<FloatType>(view_obj["x_max"].get<double>());
                    FloatType y_min = static_cast<FloatType>(view_obj["y_min"].get<double>());
                    FloatType y_max = static_cast<FloatType>(view_obj["y_max"].get<double>());
                    int max_iter = view_obj["max_iterations"];
                    saved_views_[name] = ViewState<FloatType>(x_min, x_max, y_min, y_max, max_iter);
                }
            }
        }

        // Load the current view if it exists
        if (json_data.contains("current_view"))
        {
            const auto& current_view = json_data["current_view"];
            if (current_view.contains("x_min") && current_view.contains("x_max") && current_view.contains("y_min") && current_view.contains("y_max") &&
                current_view.contains("max_iterations"))
            {
                FloatType x_min = static_cast<FloatType>(current_view["x_min"].get<double>());
                FloatType x_max = static_cast<FloatType>(current_view["x_max"].get<double>());
                FloatType y_min = static_cast<FloatType>(current_view["y_min"].get<double>());
                FloatType y_max = static_cast<FloatType>(current_view["y_max"].get<double>());
                int max_iter = current_view["max_iterations"];

                // Store the loaded current view to apply after initialization
                loaded_current_view_ = ViewState<FloatType>(x_min, x_max, y_min, y_max, max_iter);
                has_loaded_current_view_ = true;
            }
        }
    }
    catch (const std::exception& e)
    {
        // Silently fail - start with empty views
        saved_views_.clear();
        has_loaded_current_view_ = false;
    }
}

}  // namespace mandel

#endif // MANDEL_RENDER_INL
