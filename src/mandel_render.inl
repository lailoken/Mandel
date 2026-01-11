#ifndef MANDEL_RENDER_INL
#define MANDEL_RENDER_INL

#include "mandel_render.hpp"
#include "mandel.hpp"
#include "thread_pool.hpp"

#include "imgui.h"
#include <climits>
#include <algorithm>
#include <cmath>
#include <vector>

namespace mandel
{

template<typename FloatType>
ImGuiRenderer<FloatType>::ImGuiRenderer(MandelbrotRenderer<FloatType>* renderer, 
                             TextureUpdateCallback update_callback,
                             TextureDeleteCallback delete_callback)
    : renderer_(renderer)
    , texture_front_(0)
    , texture_back_(0)
    , width_(0)
    , height_(0)
    , pixels_(nullptr)
    , pixels_being_updated_(nullptr)
    , texture_dirty_(false)
    , double_buffering_enabled_(true)
    , render_generation_(0)
    , pixels_generation_(0)
    , swapped_generation_(UINT_MAX)
    , update_callback_(update_callback)
    , delete_callback_(delete_callback)
    , is_dragging_(false)
    , last_drag_pos_(0.0f, 0.0f)
    , initial_bounds_set_(false)
    , initial_x_min_(static_cast<FloatType>(0.0))
    , initial_x_max_(static_cast<FloatType>(0.0))
    , initial_y_min_(static_cast<FloatType>(0.0))
    , initial_y_max_(static_cast<FloatType>(0.0))
    , initial_zoom_(static_cast<FloatType>(1.0))
    , first_window_size_set_(false)
    , threading_enabled_(true)
{
}

template<typename FloatType>
ImGuiRenderer<FloatType>::~ImGuiRenderer()
{
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
    // Create ImGui window using pure ImGui API
    ImGui::Begin("Mandelbrot Controls");

    if (renderer_)
    {
        // Store initial bounds on first draw (if not already set)
        if (!initial_bounds_set_)
        {
            initial_x_min_ = renderer_->get_x_min();
            initial_x_max_ = renderer_->get_x_max();
            initial_y_min_ = renderer_->get_y_min();
            initial_y_max_ = renderer_->get_y_max();
            initial_zoom_ = renderer_->get_zoom();
            initial_bounds_set_ = true;
        }
        
        // Display controls using ImGui widgets
        // ImGui uses float, so we convert from FloatType to float for UI
        bool changed = false;
        int max_iter = renderer_->get_max_iterations();
        float x_min = static_cast<float>(renderer_->get_x_min());
        float x_max = static_cast<float>(renderer_->get_x_max());
        float y_min = static_cast<float>(renderer_->get_y_min());
        float y_max = static_cast<float>(renderer_->get_y_max());
        float zoom = static_cast<float>(renderer_->get_zoom());
        
        // Get max zoom for clamping (convert to float for ImGui)
        // Use static const to ensure it's evaluated at compile time
        static const float max_zoom_float = static_cast<float>(MandelbrotRenderer<FloatType>::max_zoom);

        changed |= ImGui::SliderInt("Max Iterations", &max_iter, 2, 4096, "%d", ImGuiSliderFlags_Logarithmic);

        ImGui::PushItemWidth(100.f);
        changed |= ImGui::InputFloat("Min X", &x_min, 0.0f, 0.0f, "%.6f");
        ImGui::SameLine();
        changed |= ImGui::InputFloat("Max X", &x_max, 0.0f, 0.0f, "%.6f");
        changed |= ImGui::InputFloat("Min Y", &y_min, 0.0f, 0.0f, "%.6f");
        ImGui::SameLine();
        changed |= ImGui::InputFloat("Max Y", &y_max, 0.0f, 0.0f, "%.6f");

        // Clamp zoom input to max zoom
        if (zoom > max_zoom_float)
        {
            zoom = max_zoom_float;
            changed = true;
        }
        changed |= ImGui::InputFloat("Zoom", &zoom, 0.0f, 0.0f, "%.6f");
        // Clamp again after input (in case user typed a value)
        if (zoom > max_zoom_float)
        {
            zoom = max_zoom_float;
            changed = true;
        }
        ImGui::PopItemWidth();

        changed |= ImGui::Checkbox("Enable Threading", &threading_enabled_);
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

        // Reset button - handle reset directly without triggering changed block
        if (ImGui::Button("Reset View"))
        {
            render_generation_++;
            if (!double_buffering_enabled_)
            {
                clear_canvas();
            }
            renderer_->set_bounds(initial_x_min_, initial_x_max_, initial_y_min_, initial_y_max_);
            renderer_->set_zoom(initial_zoom_);
            ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
            renderer_->regenerate(pool);
        }

        if (changed)
        {
            // Parameters changed - increment render generation to mark new render starting
            render_generation_++;
            
            // Check what changed using floating point comparison with epsilon
            // Use FloatType for epsilon to match precision
            const FloatType eps = static_cast<FloatType>(1e-6);
            FloatType old_x_min = renderer_->get_x_min();
            FloatType old_x_max = renderer_->get_x_max();
            FloatType old_y_min = renderer_->get_y_min();
            FloatType old_y_max = renderer_->get_y_max();
            FloatType old_zoom = renderer_->get_zoom();
            
            bool x_min_changed = std::abs(static_cast<FloatType>(x_min) - old_x_min) > eps;
            bool x_max_changed = std::abs(static_cast<FloatType>(x_max) - old_x_max) > eps;
            bool y_min_changed = std::abs(static_cast<FloatType>(y_min) - old_y_min) > eps;
            bool y_max_changed = std::abs(static_cast<FloatType>(y_max) - old_y_max) > eps;
            bool zoom_changed = std::abs(static_cast<FloatType>(zoom) - old_zoom) > eps;
            bool bounds_changed = x_min_changed || x_max_changed || y_min_changed || y_max_changed;
            
            if (zoom_changed && !bounds_changed)
            {
                // Only zoom changed, adjust bounds around center of current view
                // Clamp zoom to max_zoom
                FloatType clamped_zoom = std::min(static_cast<FloatType>(zoom), MandelbrotRenderer<FloatType>::max_zoom);
                FloatType center_x = (old_x_min + old_x_max) / static_cast<FloatType>(2.0);
                FloatType center_y = (old_y_min + old_y_max) / static_cast<FloatType>(2.0);
                FloatType scale = static_cast<FloatType>(4.0) / clamped_zoom;
                FloatType new_x_min = center_x - scale / static_cast<FloatType>(2.0);
                FloatType new_x_max = center_x + scale / static_cast<FloatType>(2.0);
                FloatType new_y_min = center_y - scale / static_cast<FloatType>(2.0);
                FloatType new_y_max = center_y + scale / static_cast<FloatType>(2.0);
                renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                renderer_->set_zoom(clamped_zoom);
            }
            else if (bounds_changed)
            {
                // Bounds changed directly - use them exactly as specified
                // Validate and ensure min < max for both X and Y
                FloatType final_x_min = static_cast<FloatType>(x_min);
                FloatType final_x_max = static_cast<FloatType>(x_max);
                FloatType final_y_min = static_cast<FloatType>(y_min);
                FloatType final_y_max = static_cast<FloatType>(y_max);
                
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
                    FloatType calculated_zoom = static_cast<FloatType>(4.0) / avg_range;
                    renderer_->set_zoom(calculated_zoom);
                }
            }
            else if (zoom_changed)
            {
                // Only zoom changed (without bounds), adjust bounds around center
                // This branch should rarely execute since we handle zoom-only above
                // Clamp zoom to max_zoom
                FloatType clamped_zoom = std::min(static_cast<FloatType>(zoom), MandelbrotRenderer<FloatType>::max_zoom);
                FloatType center_x = (old_x_min + old_x_max) / static_cast<FloatType>(2.0);
                FloatType center_y = (old_y_min + old_y_max) / static_cast<FloatType>(2.0);
                FloatType scale = static_cast<FloatType>(4.0) / clamped_zoom;
                FloatType new_x_min = center_x - scale / static_cast<FloatType>(2.0);
                FloatType new_x_max = center_x + scale / static_cast<FloatType>(2.0);
                FloatType new_y_min = center_y - scale / static_cast<FloatType>(2.0);
                FloatType new_y_max = center_y + scale / static_cast<FloatType>(2.0);
                renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                renderer_->set_zoom(clamped_zoom);
            }
            
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

    ImGui::End();

    ImGui::Begin("Mandelbrot Set");
    
    if (renderer_)
    {
        // Resize canvas to match available space - extend max coordinates
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x > 0 && canvas_size.y > 0)
        {
            int new_width = static_cast<int>(canvas_size.x);
            int new_height = static_cast<int>(canvas_size.y);
            
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
                
                // Now safe to resize - no tasks are running
                // Invalidate any pending texture updates to prevent using old pixel data
                texture_dirty_ = false;
                pixels_ = nullptr;
                // Don't clear width_/height_ - keep old dimensions for display until new render completes
                
                FloatType new_x_min, new_x_max, new_y_min, new_y_max;
                
                if (!first_window_size_set_)
                {
                    // First window size: center and fit the entire default Mandelbrot view
                    // Default view: x: -2.5 to 1.5 (range 4.0), y: -2.0 to 2.0 (range 4.0)
                    const FloatType default_x_min = static_cast<FloatType>(-2.5);
                    const FloatType default_x_max = static_cast<FloatType>(1.5);
                    const FloatType default_y_min = static_cast<FloatType>(-2.0);
                    const FloatType default_y_max = static_cast<FloatType>(2.0);
                    const FloatType default_x_range = default_x_max - default_x_min;  // 4.0
                    const FloatType default_y_range = default_y_max - default_y_min;  // 4.0
                    const FloatType default_x_center = (default_x_min + default_x_max) / static_cast<FloatType>(2.0);  // -0.5
                    const FloatType default_y_center = (default_y_min + default_y_max) / static_cast<FloatType>(2.0);  // 0.0
                    
                    // Calculate window aspect ratio
                    FloatType window_aspect = static_cast<FloatType>(new_width) / static_cast<FloatType>(new_height);
                    FloatType default_aspect = default_x_range / default_y_range;  // 1.0 (4.0 / 4.0)
                    
                    // Fit the entire default view in the window while maintaining window aspect ratio
                    // We need ranges that are at least as large as the default ranges (4.0 each)
                    // and scaled to match the window aspect ratio
                    FloatType x_range, y_range;
                    
                    // Choose ranges that satisfy both constraints (fit default view AND window aspect)
                    if (window_aspect > default_aspect)
                    {
                        // Window is wider than default (1:1): Y range is the constraint
                        // Use full Y range (4.0) to ensure entire default view fits
                        y_range = default_y_range;  // 4.0
                        x_range = y_range * window_aspect;  // Scale X to match window aspect (>= 4.0)
                    }
                    else
                    {
                        // Window is taller than default (1:1): X range is the constraint
                        // Use full X range (4.0) to ensure entire default view fits
                        x_range = default_x_range;  // 4.0
                        y_range = x_range / window_aspect;  // Scale Y to match window aspect (>= 4.0)
                    }
                    
                    // Center the view on the default center
                    new_x_min = default_x_center - x_range / static_cast<FloatType>(2.0);
                    new_x_max = default_x_center + x_range / static_cast<FloatType>(2.0);
                    new_y_min = default_y_center - y_range / static_cast<FloatType>(2.0);
                    new_y_max = default_y_center + y_range / static_cast<FloatType>(2.0);
                    
                    // Verify the entire default view fits (it should, but double-check)
                    // If somehow it doesn't, expand to ensure it does
                    if (new_x_min > default_x_min) new_x_min = default_x_min;
                    if (new_x_max < default_x_max) new_x_max = default_x_max;
                    if (new_y_min > default_y_min) new_y_min = default_y_min;
                    if (new_y_max < default_y_max) new_y_max = default_y_max;
                    
                    first_window_size_set_ = true;
                }
                else
                {
                    // Subsequent resizes: keep top-left origin fixed and extend max coordinates
                    FloatType current_x_min = renderer_->get_x_min();
                    FloatType current_x_max = renderer_->get_x_max();
                    FloatType current_y_min = renderer_->get_y_min();
                    FloatType current_y_max = renderer_->get_y_max();
                    
                    FloatType current_x_range = current_x_max - current_x_min;
                    FloatType current_y_range = current_y_max - current_y_min;
                    
                    // Scale ranges proportionally to the change in window dimensions
                    FloatType width_scale = static_cast<FloatType>(new_width) / static_cast<FloatType>(renderer_->get_width());
                    FloatType height_scale = static_cast<FloatType>(new_height) / static_cast<FloatType>(renderer_->get_height());
                    
                    // Scale ranges proportionally to window dimensions
                    FloatType new_x_range = current_x_range * width_scale;
                    FloatType new_y_range = current_y_range * height_scale;
                    
                    // Keep origin (top-left) fixed, extend max coordinates
                    new_x_min = current_x_min;  // Origin stays fixed
                    new_x_max = current_x_min + new_x_range;
                    new_y_min = current_y_min;  // Origin stays fixed
                    new_y_max = current_y_min + new_y_range;
                }
                
                // Update zoom from average range
                FloatType x_range = new_x_max - new_x_min;
                FloatType y_range = new_y_max - new_y_min;
                FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                
                render_generation_++;
                renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                renderer_->set_zoom(static_cast<FloatType>(4.0) / avg_range);
                if (!double_buffering_enabled_)
                {
                    clear_canvas();
                }
                // Pass thread pool if threading is enabled, otherwise pass nullptr
                ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                renderer_->init(new_width, new_height, pool);
            }
        }
    }

    // Update texture if needed using platform-specific callback
    // Only update if we have valid pixels and dimensions match renderer
    if (texture_dirty_ && pixels_ != nullptr && update_callback_ != nullptr && 
        renderer_ != nullptr && width_ > 0 && height_ > 0 &&
        width_ == renderer_->get_width() && height_ == renderer_->get_height())
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
    if (double_buffering_enabled_ && pixels_ != nullptr)
    {
        // Check if this render generation completed without interruption
        bool generation_matches = (pixels_generation_ == render_generation_);
        
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
        
        // For double buffering to swap, we need:
        // 1. Generation matches (render completed without interruption)
        // 2. Thread pool tasks completed naturally (if using thread pool) OR texture not dirty (if synchronous)
        // 3. Back buffer exists (we've updated it) 
        // 4. We haven't already swapped for this generation
        
        // Normal swap: Both buffers exist and all tasks are complete
        if (generation_matches && thread_pool_complete && texture_back_ != 0 && 
            texture_front_ != 0 && swapped_generation_ != render_generation_)
        {
            swap_buffers();
            swapped_generation_ = render_generation_;
        }
        // Initial swap: First render with double buffering - initialize front buffer
        else if (texture_back_ != 0 && texture_front_ == 0 && 
                 thread_pool_complete && swapped_generation_ != render_generation_)
        {
            swap_buffers();
            swapped_generation_ = render_generation_;
        }
    }

    // Display the front buffer (or the only buffer if single-buffering)
    ImTextureID display_texture = texture_front_;
    if (display_texture != 0 && renderer_ && width_ > 0 && height_ > 0)
    {
        ImVec2 image_size(static_cast<float>(width_), static_cast<float>(height_));
        
        // Ensure image size is valid (ImGui requires non-zero dimensions)
        if (image_size.x > 0.0f && image_size.y > 0.0f)
        {
            // Save cursor position before drawing image
            ImVec2 cursor_pos_before = ImGui::GetCursorPos();
            
            ImGui::Image(display_texture, image_size);
            
            // Save image rectangle for coordinate calculations
            ImVec2 image_min = ImGui::GetItemRectMin();
            ImVec2 image_max = ImGui::GetItemRectMax();
            
            // Add an invisible button on top of the image to capture mouse input
            // This prevents ImGui's default window dragging behavior from interfering
            ImGui::SetCursorPos(cursor_pos_before);  // Reset to position before image
            ImGui::SetNextItemAllowOverlap();  // Allow overlap with image
            ImGui::InvisibleButton("canvas_interaction", image_size);
            
            // Handle interactive view control (dragging and zooming)
            ImGuiIO& io = ImGui::GetIO();
            bool is_hovered = ImGui::IsItemHovered();
            bool is_active = ImGui::IsItemActive();
            
            if (is_hovered || is_active)
            {
                    // Get mouse position relative to the image
                ImVec2 mouse_pos = ImGui::GetMousePos();
                
                // Clamp mouse position to image bounds
                float mouse_x = std::max(image_min.x, std::min(mouse_pos.x, image_max.x));
                float mouse_y = std::max(image_min.y, std::min(mouse_pos.y, image_max.y));
                
                // Convert to pixel coordinates within the image
                float pixel_x = ((mouse_x - image_min.x) / (image_max.x - image_min.x)) * static_cast<float>(width_);
                float pixel_y = ((mouse_y - image_min.y) / (image_max.y - image_min.y)) * static_cast<float>(height_);
                
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
                    float move_distance = std::sqrt(
                        (current_pos.x - last_drag_pos_.x) * (current_pos.x - last_drag_pos_.x) +
                        (current_pos.y - last_drag_pos_.y) * (current_pos.y - last_drag_pos_.y)
                    );
                    
                    if (move_distance >= move_threshold && (renderer_->get_thread_pool()->is_idle() || !double_buffering_enabled_))
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
                        
                        // Update bounds and regenerate
                        render_generation_++;
                        renderer_->set_bounds(new_x_min, new_x_max, new_y_min, new_y_max);
                        if (!double_buffering_enabled_)
                        {
                            clear_canvas();
                        }
                        ThreadPool* pool = threading_enabled_ ? renderer_->get_thread_pool() : nullptr;
                        renderer_->regenerate(pool);
                        
                        // Remember this position so we don't regenerate again until mouse moves more
                        last_drag_pos_ = current_pos;
                    }
                }
                else
                {
                    // Stop dragging when button is released or item is no longer active
                    is_dragging_ = false;
                }
                
                // Handle scroll wheel zoom
                float current_wheel = io.MouseWheel;
                if (std::abs(current_wheel) > 0.01f)
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
                    FloatType zoom_factor = static_cast<FloatType>(1.0) + static_cast<FloatType>(current_wheel) * static_cast<FloatType>(0.1);
                    
                    // Get max zoom from template constant
                    constexpr FloatType max_zoom = MandelbrotRenderer<FloatType>::max_zoom;
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
                    // The mouse position in complex space should remain constant
                    FloatType mouse_x_ratio = static_cast<FloatType>(pixel_x) / static_cast<FloatType>(width_);
                    FloatType mouse_y_ratio = static_cast<FloatType>(pixel_y) / static_cast<FloatType>(height_);
                    
                    FloatType new_x_min = mouse_complex_x - mouse_x_ratio * new_x_range;
                    FloatType new_x_max = new_x_min + new_x_range;
                    FloatType new_y_min = mouse_complex_y - mouse_y_ratio * new_y_range;
                    FloatType new_y_max = new_y_min + new_y_range;
                    
                    // Update bounds and zoom (zoom is derived from range)
                    FloatType avg_range = (new_x_range + new_y_range) / static_cast<FloatType>(2.0);
                    FloatType new_zoom = static_cast<FloatType>(4.0) / avg_range;
                    
                    // Clamp zoom to maximum (safety check)
                    if (new_zoom > max_zoom)
                    {
                        new_zoom = max_zoom;
                    }
                    
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
            else
            {
                // Reset dragging state if mouse leaves the image
                is_dragging_ = false;
            }
        }
    }

    ImGui::End();
}

}  // namespace mandel

#endif // MANDEL_RENDER_INL

