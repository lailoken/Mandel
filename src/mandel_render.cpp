#include "mandel_render.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include "mandel.hpp"
#include "thread_pool.hpp"

namespace mandel
{

// JSON float storage helpers for long double
namespace detail
{
    std::string float_to_json(FloatType value)
    {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss.precision(21);  // Use maximum precision for long double (19-21 significant digits)
        oss << value;
        return oss.str();
    }

    FloatType float_from_json(const nlohmann::json& j)
    {
        if (j.is_string())
        {
            return std::strtold(j.get<std::string>().c_str(), nullptr);
        }
        else
        {
            // Old format: stored as number (backward compatibility)
            return static_cast<FloatType>(j.get<double>());
        }
    }
}

// ImGui input helper for long double (convert to/from double since ImGui doesn't support long double)
namespace detail
{
    bool imgui_input_float(const char* label, FloatType* v, FloatType step, FloatType step_fast, const char* format)
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
            *v = static_cast<FloatType>(temp);
        }
        return result;
    }
}

ImGuiRenderer::ImGuiRenderer(MandelbrotRenderer* renderer, TextureUpdateCallback update_callback, TextureDeleteCallback delete_callback)
    : renderer_(renderer),
      texture_front_(0),
      texture_back_(0),
      width_(0),
      height_(0),
      viewport_width_(0),
      viewport_height_(0),
      margin_x_(0),
      margin_y_(0),
      pixels_(nullptr),
      texture_dirty_(false),
      render_generation_(0),
      update_callback_(update_callback),
      delete_callback_(delete_callback),
      is_dragging_(false),
      last_drag_pos_(0.0f, 0.0f),
      display_offset_x_(0.0f),
      display_offset_y_(0.0f),
      render_start_offset_x_(0.0f),
      render_start_offset_y_(0.0f),
      display_scale_(1.0f),
      zoom_center_x_(0.0f),
      zoom_center_y_(0.0f),
      suppress_texture_updates_(false),
      is_rendering_(false),
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
      has_loaded_current_view_(false),
      has_pending_settings_(false)
{
    // Initialize new view name buffer
    new_view_name_buffer_[0] = '\0';
    // Initialize applied settings (will be updated on first render)
    applied_settings_ = ViewState();
    pending_settings_ = ViewState();
    // Load saved views from file on construction
    load_views_from_file();
}

ImGuiRenderer::~ImGuiRenderer()
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

bool ImGuiRenderer::is_render_in_progress() const
{
    if (renderer_ == nullptr)
    {
        return false;
    }

    // When suppressing texture updates, don't use texture_dirty_ as indicator
    // (it will stay true until we upload on completion)
    if (!suppress_texture_updates_ && texture_dirty_)
    {
        return true;
    }

    if (threading_enabled_)
    {
        ThreadPool* pool = renderer_->get_thread_pool();
        if (pool != nullptr)
        {
            // Render is in progress if thread pool is not idle
            return !pool->is_idle();
        }
    }

    return false;
}

void ImGuiRenderer::on_pixels_updated(const unsigned char* pixels, int width, int height)
{
    pixels_ = pixels;
    width_ = width;
    height_ = height;
    texture_dirty_ = true;
}

void ImGuiRenderer::apply_pending_settings_if_ready()
{
    // Only apply pending settings if we have them and render is not in progress
    if (!has_pending_settings_ || renderer_ == nullptr)
    {
        return;
    }

    // Don't apply if render is in progress
    if (is_render_in_progress())
    {
        return;
    }

    // Apply the pending settings (these are viewport bounds)
    FloatType x_min = pending_settings_.x_min;
    FloatType x_max = pending_settings_.x_max;
    FloatType y_min = pending_settings_.y_min;
    FloatType y_max = pending_settings_.y_max;
    int max_iter = pending_settings_.max_iterations;

    // Update applied settings
    applied_settings_ = pending_settings_;
    has_pending_settings_ = false;

    // Validate and ensure min < max for both X and Y
    if (x_min >= x_max)
    {
        std::swap(x_min, x_max);
    }
    if (y_min >= y_max)
    {
        std::swap(y_min, y_max);
    }

    // Calculate zoom from viewport bounds (before extending)
    FloatType x_range = x_max - x_min;
    FloatType y_range = y_max - y_min;
    FloatType calculated_zoom = static_cast<FloatType>(1.0);
    if (x_range > static_cast<FloatType>(0.0) && y_range > static_cast<FloatType>(0.0))
    {
        FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
        calculated_zoom = static_cast<FloatType>(4.0) / avg_range;
        // Clamp to max_zoom (which is FloatType, supports much higher precision than uint64_t)
        if (calculated_zoom > MandelbrotRenderer::max_zoom)
        {
            calculated_zoom = MandelbrotRenderer::max_zoom;
        }
    }

    // Extend viewport bounds to buffer bounds (add overscan margin)
    extend_bounds_for_overscan(x_min, x_max, y_min, y_max);

    // Apply settings to renderer
    render_generation_++;
    renderer_->set_bounds(x_min, x_max, y_min, y_max);
    renderer_->set_zoom(calculated_zoom);
    renderer_->set_max_iterations(max_iter);

    ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
    renderer_->regenerate(pool);
    is_rendering_ = true;
    
    // Reset visual state since we're applying new settings
    display_offset_x_ = 0.0f;
    display_offset_y_ = 0.0f;
    render_start_offset_x_ = 0.0f;
    render_start_offset_y_ = 0.0f;
    display_scale_ = 1.0f;
    suppress_texture_updates_ = false;
}

void ImGuiRenderer::draw()
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
            int new_viewport_width = static_cast<int>(canvas_size.x);
            int new_viewport_height = static_cast<int>(canvas_size.y);

            // Only resize if viewport dimensions changed
            if (viewport_width_ != new_viewport_width || viewport_height_ != new_viewport_height)
            {
                // For thread pool: explicitly reset and wait for all tasks to complete
                // This ensures no active tasks before we change dimensions
                if (threading_enabled_)
                {
                    ThreadPool* pool = renderer_->get_thread_pool();
                    pool->pause();
                }

                // Invalidate any pending texture updates to prevent using old pixel data
                texture_dirty_ = false;
                pixels_ = nullptr;

                // Store viewport size and calculate overscan margins (~1/6 of viewport on each side)
                viewport_width_ = new_viewport_width;
                viewport_height_ = new_viewport_height;
                margin_x_ = (viewport_width_ + 5) / 6;   // ~1/6 viewport, rounded up
                margin_y_ = (viewport_height_ + 5) / 6;  // ~1/6 viewport, rounded up

                // Render buffer = viewport + 2*margin (margin on each side)
                int new_width = viewport_width_ + 2 * margin_x_;
                int new_height = viewport_height_ + 2 * margin_y_;

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
                        // Apply the loaded current view - these are viewport bounds, extend for overscan
                        FloatType x_range = loaded_current_view_.x_max - loaded_current_view_.x_min;
                        FloatType y_range = loaded_current_view_.y_max - loaded_current_view_.y_min;
                        FloatType pixel_to_x = x_range / static_cast<FloatType>(viewport_width_);
                        FloatType pixel_to_y = y_range / static_cast<FloatType>(viewport_height_);

                        // Extend bounds by margin in complex plane coordinates
                        FloatType extended_x_min = loaded_current_view_.x_min - pixel_to_x * static_cast<FloatType>(margin_x_);
                        FloatType extended_x_max = loaded_current_view_.x_max + pixel_to_x * static_cast<FloatType>(margin_x_);
                        FloatType extended_y_min = loaded_current_view_.y_min - pixel_to_y * static_cast<FloatType>(margin_y_);
                        FloatType extended_y_max = loaded_current_view_.y_max + pixel_to_y * static_cast<FloatType>(margin_y_);

                        renderer_->set_bounds(extended_x_min, extended_x_max, extended_y_min, extended_y_max);
                        renderer_->set_max_iterations(loaded_current_view_.max_iterations);
                        // Calculate zoom from viewport bounds (not extended)
                        FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                        if (avg_range > static_cast<FloatType>(0.0))
                        {
                            FloatType calculated_zoom = static_cast<FloatType>(4.0) / avg_range;
                            // Clamp to max_zoom (which is FloatType)
                            if (calculated_zoom > MandelbrotRenderer::max_zoom)
                            {
                                calculated_zoom = MandelbrotRenderer::max_zoom;
                            }
                            renderer_->set_zoom(calculated_zoom);
                        }
                    }
                    else
                    {
                        // Apply the default initial view - extend for overscan
                        FloatType x_range = initial_x_max_ - initial_x_min_;
                        FloatType y_range = initial_y_max_ - initial_y_min_;
                        FloatType pixel_to_x = x_range / static_cast<FloatType>(viewport_width_);
                        FloatType pixel_to_y = y_range / static_cast<FloatType>(viewport_height_);

                        FloatType extended_x_min = initial_x_min_ - pixel_to_x * static_cast<FloatType>(margin_x_);
                        FloatType extended_x_max = initial_x_max_ + pixel_to_x * static_cast<FloatType>(margin_x_);
                        FloatType extended_y_min = initial_y_min_ - pixel_to_y * static_cast<FloatType>(margin_y_);
                        FloatType extended_y_max = initial_y_max_ + pixel_to_y * static_cast<FloatType>(margin_y_);

                        renderer_->set_bounds(extended_x_min, extended_x_max, extended_y_min, extended_y_max);
                        renderer_->set_zoom(initial_zoom_);
                    }
                    first_window_size_set_ = true;
                }
                else
                {
                    // Resize while preserving the viewport center - need to recalculate extended bounds
                    // Get current viewport bounds (center of the buffer)
                    FloatType current_x_min = renderer_->get_x_min();
                    FloatType current_x_max = renderer_->get_x_max();
                    FloatType current_y_min = renderer_->get_y_min();
                    FloatType current_y_max = renderer_->get_y_max();

                    // These are buffer bounds; calculate viewport bounds (center portion)
                    FloatType x_range = current_x_max - current_x_min;
                    FloatType y_range = current_y_max - current_y_min;
                    FloatType center_x = (current_x_min + current_x_max) / static_cast<FloatType>(2.0);
                    FloatType center_y = (current_y_min + current_y_max) / static_cast<FloatType>(2.0);

                    // Calculate new pixel-to-complex ratio based on new viewport size
                    // Keep the center fixed, calculate new range proportional to new viewport
                    int old_buffer_width = width_ > 0 ? width_ : new_width;
                    int old_buffer_height = height_ > 0 ? height_ : new_height;
                    FloatType old_pixel_to_x = x_range / static_cast<FloatType>(old_buffer_width);
                    FloatType old_pixel_to_y = y_range / static_cast<FloatType>(old_buffer_height);

                    // New buffer bounds based on new size but same pixel scale
                    FloatType new_x_range = old_pixel_to_x * static_cast<FloatType>(new_width);
                    FloatType new_y_range = old_pixel_to_y * static_cast<FloatType>(new_height);

                    FloatType extended_x_min = center_x - new_x_range / static_cast<FloatType>(2.0);
                    FloatType extended_x_max = center_x + new_x_range / static_cast<FloatType>(2.0);
                    FloatType extended_y_min = center_y - new_y_range / static_cast<FloatType>(2.0);
                    FloatType extended_y_max = center_y + new_y_range / static_cast<FloatType>(2.0);

                    renderer_->set_bounds(extended_x_min, extended_x_max, extended_y_min, extended_y_max);
                }

                // Resize the canvas only - complex plane bounds already set above
                render_generation_++;
                ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                renderer_->init(new_width, new_height, pool);
                is_rendering_ = true;  // init() triggers a render
                // Update applied settings after init (init will trigger first render)
                // Use viewport bounds for UI consistency
                if (renderer_ && first_window_size_set_)
                {
                    applied_settings_ = get_viewport_bounds();
                }
                // Reset visual state since dimensions changed
                display_offset_x_ = 0.0f;
                display_offset_y_ = 0.0f;
                render_start_offset_x_ = 0.0f;
                render_start_offset_y_ = 0.0f;
                display_scale_ = 1.0f;
                suppress_texture_updates_ = false;
            }
        }
    }

    // Upload any pending pixels to back texture, then swap to front
    // This prevents flickering by never modifying the displayed texture
    // Skip updates during pan/zoom to keep showing old content at offset/scale
    if (texture_dirty_ && !suppress_texture_updates_ && pixels_ != nullptr && 
        update_callback_ != nullptr && renderer_ != nullptr && width_ > 0 && height_ > 0)
    {
        // Upload to back buffer
        update_callback_(&texture_back_, pixels_, width_, height_);
        // Swap back to front
        std::swap(texture_front_, texture_back_);
        texture_dirty_ = false;
    }

    // Draw Mandelbrot set on the background and create input window BEFORE controls window
    // This ensures the input window is behind the controls window
    // Display texture_front_ at display_offset_ for buttery smooth dragging
    if (texture_front_ != 0 && renderer_ && width_ > 0 && height_ > 0)
    {
        ImVec2 image_size(static_cast<float>(width_), static_cast<float>(height_));

        // Ensure image size is valid (ImGui requires non-zero dimensions)
        if (image_size.x > 0.0f && image_size.y > 0.0f)
        {
            // Draw the image directly on the background using the background draw list
            // Apply visual offset (for panning) and scale (for zooming) for immediate feedback
            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            
            // Calculate scaled size
            float scaled_width = image_size.x * display_scale_;
            float scaled_height = image_size.y * display_scale_;
            
            // Scale centered on zoom_center_ (mouse position when zoom started)
            // The point at zoom_center_ should stay fixed during scaling
            // offset = zoom_center - zoom_center * scale = zoom_center * (1 - scale)
            float scale_offset_x = zoom_center_x_ * (1.0f - display_scale_);
            float scale_offset_y = zoom_center_y_ * (1.0f - display_scale_);

            // Offset by -margin so viewport (0,0) shows the center of the buffer
            // The margin offset also scales with display_scale_ for proper zoom behavior
            float margin_offset_x = static_cast<float>(margin_x_) * display_scale_;
            float margin_offset_y = static_cast<float>(margin_y_) * display_scale_;

            ImVec2 image_min(scale_offset_x + display_offset_x_ - margin_offset_x, scale_offset_y + display_offset_y_ - margin_offset_y);
            ImVec2 image_max(scale_offset_x + scaled_width + display_offset_x_ - margin_offset_x, scale_offset_y + scaled_height + display_offset_y_ - margin_offset_y);
            draw_list->AddImage(texture_front_, image_min, image_max);

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

                // Convert to pixel coordinates within the buffer (add margin offset)
                // Screen pixel (0,0) maps to buffer pixel (margin_x_, margin_y_)
                float pixel_x = mouse_x + static_cast<float>(margin_x_);
                float pixel_y = mouse_y + static_cast<float>(margin_y_);

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

                // Update bounds and regenerate (wait for current render to complete)
                if (!is_render_in_progress() && !is_rendering_)
                {
                    render_generation_++;
                    renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                    ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                    renderer_->regenerate(pool);
                    is_rendering_ = true;
                    
                    // Reset visual state since we're centering on a new location
                    display_offset_x_ = 0.0f;
                    display_offset_y_ = 0.0f;
                    render_start_offset_x_ = 0.0f;
                    render_start_offset_y_ = 0.0f;
                    display_scale_ = 1.0f;
                    suppress_texture_updates_ = false;
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

                // Convert to pixel coordinates within the buffer (add margin offset)
                // Screen pixel (0,0) maps to buffer pixel (margin_x_, margin_y_)
                float pixel_x = mouse_x + static_cast<float>(margin_x_);
                float pixel_y = mouse_y + static_cast<float>(margin_y_);

                // Handle dragging
                if (is_active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    ImVec2 current_pos(mouse_x, mouse_y);
                    
                    // Start dragging when mouse button is first pressed
                    if (!is_dragging_)
                    {
                        is_dragging_ = true;
                        last_drag_pos_ = current_pos;
                    }

                    // Calculate incremental delta from last frame
                    ImVec2 frame_delta(current_pos.x - last_drag_pos_.x, current_pos.y - last_drag_pos_.y);
                    
                    // Update visual offset immediately for instant feedback
                    display_offset_x_ += frame_delta.x;
                    display_offset_y_ += frame_delta.y;
                    
                    // Remember position for next frame
                    last_drag_pos_ = current_pos;
                    
                    // Calculate pending offset (how much we've dragged past what's being rendered)
                    float pending_offset_x = display_offset_x_ - render_start_offset_x_;
                    float pending_offset_y = display_offset_y_ - render_start_offset_y_;

                    // Start a new render when we've used up most of the overscan margin
                    // Use 80% of margin as threshold - gives buffer before edge becomes visible
                    float start_threshold = static_cast<float>(std::min(margin_x_, margin_y_)) * 0.8f;
                    // When already rendering, use full margin to avoid constant re-rendering
                    float restart_threshold = static_cast<float>(std::min(margin_x_, margin_y_));
                    float threshold = is_rendering_ ? restart_threshold : start_threshold;
                    
                    if (std::abs(pending_offset_x) >= threshold || std::abs(pending_offset_y) >= threshold)
                    {
                        // Calculate new bounds based on PENDING offset (since last render start)
                        // NOT total offset - current bounds already incorporate previous renders
                        FloatType x_range = renderer_->get_x_max() - renderer_->get_x_min();
                        FloatType y_range = renderer_->get_y_max() - renderer_->get_y_min();
                        
                        // Use texture dimensions (width_/height_) for conversion, NOT viewport_size
                        // The texture is drawn at (width_, height_) size, so 1 pixel of visual offset
                        // corresponds to (x_range / width_) complex units. Using viewport_size causes
                        // drift when it differs from texture size (float truncation, resize timing).
                        FloatType screen_to_x = x_range / static_cast<FloatType>(width_);
                        FloatType screen_to_y = y_range / static_cast<FloatType>(height_);
                        
                        // Calculate pan in complex plane (opposite direction of screen drag)
                        // Use PENDING offset, not total - bounds already shifted by previous renders
                        FloatType pan_x = -static_cast<FloatType>(pending_offset_x) * screen_to_x;
                        FloatType pan_y = -static_cast<FloatType>(pending_offset_y) * screen_to_y;
                        
                        FloatType new_x_min = renderer_->get_x_min() + pan_x;
                        FloatType new_x_max = renderer_->get_x_max() + pan_x;
                        FloatType new_y_min = renderer_->get_y_min() + pan_y;
                        FloatType new_y_max = renderer_->get_y_max() + pan_y;
                        
                        // Snapshot current offset - will be subtracted when render completes
                        render_start_offset_x_ = display_offset_x_;
                        render_start_offset_y_ = display_offset_y_;
                        
                        // Suppress texture updates during pan - keep showing old content at offset
                        suppress_texture_updates_ = true;
                        
                        // Increment generation to invalidate any stale in-progress render
                        render_generation_++;
                        
                        // Start the render
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        
                        // Pan pixel-shift optimization disabled during active drag
                        // Full regenerate is more reliable
                        renderer_->regenerate(pool, 0, 0);
                        is_rendering_ = true;
                    }
                }
                else
                {
                    // Stop dragging when button is released
                    if (is_dragging_)
                    {
                        is_dragging_ = false;
                    }
                }

                // Handle scroll wheel zoom (wait for current render to complete)
                float current_wheel = io.MouseWheel;
                if (std::abs(current_wheel) > 0.01f && !is_render_in_progress() && !is_dragging_)
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

                        // Zoom factor using exponential for symmetry
                        // zoom_in then zoom_out returns to original: base^1 * base^(-1) = 1
                        FloatType zoom_base = static_cast<FloatType>(1.0) + static_cast<FloatType>(zoom_step_);
                        FloatType zoom_factor = std::pow(zoom_base, static_cast<FloatType>(current_wheel));

                        // Clamp zoom_factor to reasonable bounds
                        constexpr FloatType min_zoom_factor = static_cast<FloatType>(0.1);
                        constexpr FloatType max_single_step = static_cast<FloatType>(10.0);
                        if (zoom_factor < min_zoom_factor)
                        {
                            zoom_factor = min_zoom_factor;
                        }
                        else if (zoom_factor > max_single_step)
                        {
                            zoom_factor = max_single_step;
                        }

                        // Get max zoom from constant (FloatType, not uint64_t)
                        constexpr FloatType max_zoom = MandelbrotRenderer::max_zoom;
                        constexpr FloatType min_range = static_cast<FloatType>(4.0) / max_zoom;

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
                        FloatType new_zoom = static_cast<FloatType>(4.0) / avg_range;

                        // Clamp zoom to maximum (safety check) - max_zoom is FloatType
                        if (new_zoom > max_zoom)
                        {
                            new_zoom = max_zoom;
                        }

                        // Clamp zoom to minimum (prevent zooming out too far)
                        constexpr FloatType min_zoom = static_cast<FloatType>(0.25);
                        if (new_zoom < min_zoom)
                        {
                            // Recalculate bounds based on minimum zoom
                            new_zoom = min_zoom;
                            FloatType clamped_range = static_cast<FloatType>(4.0) / min_zoom;
                            new_x_range = clamped_range;
                            new_y_range = clamped_range;
                            new_x_min = mouse_complex_x - mouse_x_ratio * new_x_range;
                            new_x_max = new_x_min + new_x_range;
                            new_y_min = mouse_complex_y - mouse_y_ratio * new_y_range;
                            new_y_max = new_y_min + new_y_range;
                        }

                        // Set display scale for immediate visual feedback
                        // zoom_factor > 1 means zooming in (stretch texture)
                        // zoom_factor < 1 means zooming out (shrink texture)
                        display_scale_ = static_cast<float>(zoom_factor);

                        // Set zoom center to mouse position (in screen pixels, not buffer pixels)
                        // The point under the mouse should stay fixed during scaling
                        zoom_center_x_ = mouse_x;
                        zoom_center_y_ = mouse_y;

                        // Suppress texture updates during zoom - keep showing old content scaled
                        suppress_texture_updates_ = true;
                        
                        render_generation_++;
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        renderer_->set_zoom(new_zoom);
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                        is_rendering_ = true;
                        
                        // Reset drag offset since zoom changes the entire view
                        display_offset_x_ = 0.0f;
                        display_offset_y_ = 0.0f;
                        render_start_offset_x_ = 0.0f;
                        render_start_offset_y_ = 0.0f;
                }
            }
            else
            {
                // Reset dragging state if mouse leaves the image
                if (is_dragging_)
                {
                    is_dragging_ = false;
                }
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
            // Initialize applied_settings_ from renderer if not yet initialized (first frame after render completes)
            if (applied_settings_.max_iterations == 0 && !is_render_in_progress())
            {
                applied_settings_ =
                    ViewState(renderer_->get_x_min(), renderer_->get_x_max(), renderer_->get_y_min(), renderer_->get_y_max(), renderer_->get_max_iterations());
            }

            // Get current settings (from renderer if no pending, otherwise from pending)
            // Use viewport bounds (what user sees), not buffer bounds
            int max_iter = has_pending_settings_ ? pending_settings_.max_iterations : renderer_->get_max_iterations();
            FloatType x_min, x_max, y_min, y_max;
            if (has_pending_settings_)
            {
                x_min = pending_settings_.x_min;
                x_max = pending_settings_.x_max;
                y_min = pending_settings_.y_min;
                y_max = pending_settings_.y_max;
            }
            else
            {
                ViewState viewport = get_viewport_bounds();
                x_min = viewport.x_min;
                x_max = viewport.x_max;
                y_min = viewport.y_min;
                y_max = viewport.y_max;
            }

            // Calculate zoom from bounds (always derive from current bounds, not from renderer)
            FloatType zoom = static_cast<FloatType>(1.0);
            FloatType x_range = x_max - x_min;
            FloatType y_range = y_max - y_min;
            if (x_range > static_cast<FloatType>(0.0) && y_range > static_cast<FloatType>(0.0))
            {
                FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                zoom = static_cast<FloatType>(4.0) / avg_range;
            }

            // Get max zoom for clamping
            static const FloatType max_zoom = MandelbrotRenderer::max_zoom;
            static const FloatType min_zoom = static_cast<FloatType>(0.1);

            // Read UI values and check if they differ from applied settings
            bool settings_changed = false;
            ImGui::SliderInt("Max Iterations", &max_iter, 2, 4096, "%d", ImGuiSliderFlags_Logarithmic);

            ImGui::PushItemWidth(120.f);
            // Use the larger epsilon between float and FloatType - scaled for practical UI use
            // Epsilon is of FloatType
            constexpr FloatType eps = std::numeric_limits<FloatType>::epsilon() * static_cast<FloatType>(100.0);

            // Format string for long double precision
            const char* format_str = "%.16Lf";
            FloatType step = static_cast<FloatType>(0.0);

            ImGui::TextUnformatted("Min:");
            ImGui::SameLine();
            detail::imgui_input_float(",##min_x", &x_min, step, step, format_str);
            if (x_min >= x_max)
            {
                x_max = x_min + eps;
            }

            ImGui::SameLine();
            detail::imgui_input_float(",##min_y", &y_min, step, step, format_str);
            if (y_max <= y_min)
            {
                y_max = y_min + eps;
            }

            ImGui::TextUnformatted("Max:");
            ImGui::SameLine();

            detail::imgui_input_float(",##max_x", &x_max, step, step, format_str);
            if (x_min >= x_max)
            {
                x_min = x_max - eps;
            }

            ImGui::SameLine();
            detail::imgui_input_float(",##max_y", &y_max, step, step, format_str);
            if (y_min >= y_max)
            {
                y_min = y_max - eps;
            }
            ImGui::PopItemWidth();

            // Zoom slider - when changed, convert to bounds
            // Note: We use double for ImGui slider, which may lose precision for long double, but we detect changes directly
            double zoom_d = static_cast<double>(zoom);
            double const min_zoom_d = static_cast<double>(min_zoom);
            double const max_zoom_d = static_cast<double>(max_zoom);
            bool zoom_edited = ImGui::SliderScalar("Zoom", ImGuiDataType_Double, &zoom_d, &min_zoom_d, &max_zoom_d, "%.4f", ImGuiSliderFlags_Logarithmic);
            if (zoom_edited)
            {
                FloatType new_zoom = static_cast<FloatType>(zoom_d);
                new_zoom = std::clamp(new_zoom, min_zoom, max_zoom);
                // Convert zoom change to bounds change
                FloatType center_x = (x_min + x_max) / static_cast<FloatType>(2.0);
                FloatType center_y = (y_min + y_max) / static_cast<FloatType>(2.0);
                FloatType scale = static_cast<FloatType>(4.0) / new_zoom;
                x_min = center_x - scale / static_cast<FloatType>(2.0);
                x_max = center_x + scale / static_cast<FloatType>(2.0);
                y_min = center_y - scale / static_cast<FloatType>(2.0);
                y_max = center_y + scale / static_cast<FloatType>(2.0);
                // Force settings changed when zoom is edited (precision loss in double conversion can make bounds comparison fail)
                settings_changed = true;
            }

            ImGui::Checkbox("Threading", &threading_enabled_);

            // Compare current UI values with applied settings to detect changes (only if zoom wasn't just edited)
            if (!zoom_edited)
            {
                const FloatType comparison_eps =
                    std::max(static_cast<FloatType>(std::numeric_limits<float>::epsilon()), std::numeric_limits<FloatType>::epsilon()) * static_cast<FloatType>(100.0);
                settings_changed = (max_iter != applied_settings_.max_iterations) || (std::abs(x_min - applied_settings_.x_min) > comparison_eps) ||
                                   (std::abs(x_max - applied_settings_.x_max) > comparison_eps) || (std::abs(y_min - applied_settings_.y_min) > comparison_eps) ||
                                   (std::abs(y_max - applied_settings_.y_max) > comparison_eps);
            }

            // Update pending settings if changed
            if (settings_changed)
            {
                pending_settings_ = ViewState(x_min, x_max, y_min, y_max, max_iter);
                has_pending_settings_ = true;
            }

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
                // Save current viewport state (not buffer bounds)
                ViewState viewport = get_viewport_bounds();
                saved_views_[new_view_name] = viewport;
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

            ImGui::BeginDisabled(is_render_in_progress());
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
                        // Apply reset state - extend for overscan
                        FloatType x_min = initial_x_min_;
                        FloatType x_max = initial_x_max_;
                        FloatType y_min = initial_y_min_;
                        FloatType y_max = initial_y_max_;
                        extend_bounds_for_overscan(x_min, x_max, y_min, y_max);

                        render_generation_++;
                        renderer_->set_bounds(x_min, x_max, y_min, y_max);
                        renderer_->set_zoom(initial_zoom_);
                        renderer_->set_max_iterations(initial_max_iterations_);
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                        is_rendering_ = true;
                        // Update applied settings with viewport bounds (not buffer)
                        applied_settings_ = ViewState(initial_x_min_, initial_x_max_, initial_y_min_, initial_y_max_, initial_max_iterations_);
                        has_pending_settings_ = false;
                        // Reset visual state
                        display_offset_x_ = 0.0f;
                        display_offset_y_ = 0.0f;
                        render_start_offset_x_ = 0.0f;
                        render_start_offset_y_ = 0.0f;
                        display_scale_ = 1.0f;
                        suppress_texture_updates_ = false;
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
                    const ViewState& state = it->second;

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

            // Try to apply pending settings if render is ready
            apply_pending_settings_if_ready();
        }
    }
    ImGui::End();

    // Check if render completed
    if (is_rendering_ && !is_render_in_progress() && renderer_ != nullptr)
    {
        // Render completed - upload to back buffer, then swap to front
        // This prevents flickering by never modifying the displayed texture
        const unsigned char* final_pixels = renderer_->get_pixels();
        int final_width = renderer_->get_width();
        int final_height = renderer_->get_height();
        
        if (final_pixels != nullptr && update_callback_ != nullptr && final_width > 0 && final_height > 0)
        {
            // Upload to back buffer
            update_callback_(&texture_back_, final_pixels, final_width, final_height);
            // Swap back to front - now front has new content
            std::swap(texture_front_, texture_back_);
        }
        texture_dirty_ = false;
        
        // Subtract the offset we rendered for from the current offset
        // This preserves any additional drag that happened during the render
        display_offset_x_ -= render_start_offset_x_;
        display_offset_y_ -= render_start_offset_y_;
        render_start_offset_x_ = 0.0f;
        render_start_offset_y_ = 0.0f;
        
        // Reset scale and re-enable texture updates
        display_scale_ = 1.0f;
        suppress_texture_updates_ = false;
        is_rendering_ = false;
    }

    // Try to apply pending settings after checking render completion
    apply_pending_settings_if_ready();

    // Views are saved immediately on manual edits, not here
}

void ImGuiRenderer::apply_view_state(const ViewState& state)
{
    if (renderer_ == nullptr)
    {
        return;
    }

    // Set max_iterations immediately so it's applied even if render is deferred
    renderer_->set_max_iterations(state.max_iterations);

    // Wait for current render to complete before starting new one
    if (is_render_in_progress())
    {
        return;
    }

    render_generation_++;

    // Extend viewport bounds to buffer bounds (add overscan margin)
    FloatType x_min = state.x_min;
    FloatType x_max = state.x_max;
    FloatType y_min = state.y_min;
    FloatType y_max = state.y_max;
    extend_bounds_for_overscan(x_min, x_max, y_min, y_max);

    renderer_->set_bounds(x_min, x_max, y_min, y_max);
    // Calculate zoom from viewport bounds (not extended)
    FloatType x_range = state.x_max - state.x_min;
    FloatType y_range = state.y_max - state.y_min;
    FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
    if (avg_range > static_cast<FloatType>(0.0))
    {
        FloatType calculated_zoom = static_cast<FloatType>(4.0) / avg_range;
        // Clamp to max_zoom (which is FloatType)
        if (calculated_zoom > MandelbrotRenderer::max_zoom)
        {
            calculated_zoom = MandelbrotRenderer::max_zoom;
        }
        renderer_->set_zoom(calculated_zoom);
    }
    ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
    renderer_->regenerate(pool);
    is_rendering_ = true;
    // Update applied settings and clear pending settings (view state takes precedence)
    applied_settings_ = state;
    has_pending_settings_ = false;
    
    // Reset visual state
    display_offset_x_ = 0.0f;
    display_offset_y_ = 0.0f;
    render_start_offset_x_ = 0.0f;
    render_start_offset_y_ = 0.0f;
    display_scale_ = 1.0f;
    suppress_texture_updates_ = false;
}

void ImGuiRenderer::save_view_state(const std::string& name)
{
    if (renderer_ == nullptr)
    {
        return;
    }

    // Check if the view exists in saved_views_
    auto it = saved_views_.find(name);
    if (it != saved_views_.end())
    {
        // Update the existing view with current viewport state (not buffer bounds)
        ViewState viewport = get_viewport_bounds();
        it->second = viewport;
        // Save immediately on manual edit
        save_views_to_file(false);  // Don't save current view (only named views)
    }
}

std::string ImGuiRenderer::get_config_file_path() const
{
    const char* home = std::getenv("HOME");
    if (home != nullptr)
    {
        return std::string(home) + "/.mandel";
    }
    // Fallback to current directory if HOME is not set
    return ".mandel";
}

void ImGuiRenderer::save_views_to_file(bool include_current_view)
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
            view_obj["x_min"] = detail::float_to_json(state.x_min);
            view_obj["x_max"] = detail::float_to_json(state.x_max);
            view_obj["y_min"] = detail::float_to_json(state.y_min);
            view_obj["y_max"] = detail::float_to_json(state.y_max);
            view_obj["max_iterations"] = state.max_iterations;
            json_data["views"].push_back(view_obj);
        }

        // Save current view state only if requested (on exit)
        // Save viewport bounds (what user sees), not buffer bounds
        if (include_current_view && renderer_ != nullptr)
        {
            ViewState viewport = get_viewport_bounds();
            nlohmann::json current_view;
            current_view["x_min"] = detail::float_to_json(viewport.x_min);
            current_view["x_max"] = detail::float_to_json(viewport.x_max);
            current_view["y_min"] = detail::float_to_json(viewport.y_min);
            current_view["y_max"] = detail::float_to_json(viewport.y_max);
            current_view["max_iterations"] = viewport.max_iterations;
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

ViewState ImGuiRenderer::get_viewport_bounds() const
{
    if (renderer_ == nullptr || width_ <= 0 || height_ <= 0)
    {
        return ViewState();
    }

    // Buffer bounds from renderer
    FloatType buffer_x_min = renderer_->get_x_min();
    FloatType buffer_x_max = renderer_->get_x_max();
    FloatType buffer_y_min = renderer_->get_y_min();
    FloatType buffer_y_max = renderer_->get_y_max();

    // Calculate pixel-to-complex ratio
    FloatType x_range = buffer_x_max - buffer_x_min;
    FloatType y_range = buffer_y_max - buffer_y_min;
    FloatType pixel_to_x = x_range / static_cast<FloatType>(width_);
    FloatType pixel_to_y = y_range / static_cast<FloatType>(height_);

    // Viewport bounds = buffer bounds shrunk by margin
    FloatType viewport_x_min = buffer_x_min + pixel_to_x * static_cast<FloatType>(margin_x_);
    FloatType viewport_x_max = buffer_x_max - pixel_to_x * static_cast<FloatType>(margin_x_);
    FloatType viewport_y_min = buffer_y_min + pixel_to_y * static_cast<FloatType>(margin_y_);
    FloatType viewport_y_max = buffer_y_max - pixel_to_y * static_cast<FloatType>(margin_y_);

    return ViewState(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max, renderer_->get_max_iterations());
}

void ImGuiRenderer::extend_bounds_for_overscan(FloatType& x_min, FloatType& x_max, FloatType& y_min, FloatType& y_max) const
{
    if (viewport_width_ <= 0 || viewport_height_ <= 0)
    {
        return;
    }

    // Calculate pixel-to-complex ratio based on viewport
    FloatType x_range = x_max - x_min;
    FloatType y_range = y_max - y_min;
    FloatType pixel_to_x = x_range / static_cast<FloatType>(viewport_width_);
    FloatType pixel_to_y = y_range / static_cast<FloatType>(viewport_height_);

    // Extend bounds by margin
    x_min -= pixel_to_x * static_cast<FloatType>(margin_x_);
    x_max += pixel_to_x * static_cast<FloatType>(margin_x_);
    y_min -= pixel_to_y * static_cast<FloatType>(margin_y_);
    y_max += pixel_to_y * static_cast<FloatType>(margin_y_);
}

void ImGuiRenderer::load_views_from_file()
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
                    FloatType x_min = detail::float_from_json(view_obj["x_min"]);
                    FloatType x_max = detail::float_from_json(view_obj["x_max"]);
                    FloatType y_min = detail::float_from_json(view_obj["y_min"]);
                    FloatType y_max = detail::float_from_json(view_obj["y_max"]);
                    int max_iter = view_obj["max_iterations"];
                    saved_views_[name] = ViewState(x_min, x_max, y_min, y_max, max_iter);
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
                FloatType x_min = detail::float_from_json(current_view["x_min"]);
                FloatType x_max = detail::float_from_json(current_view["x_max"]);
                FloatType y_min = detail::float_from_json(current_view["y_min"]);
                FloatType y_max = detail::float_from_json(current_view["y_max"]);
                int max_iter = current_view["max_iterations"];

                // Store the loaded current view to apply after initialization
                loaded_current_view_ = ViewState(x_min, x_max, y_min, y_max, max_iter);
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
