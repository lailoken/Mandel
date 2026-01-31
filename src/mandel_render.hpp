#pragma once

#include "imgui.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "mandel.hpp"

namespace mandel
{
// Forward declaration
class MandelbrotRenderer;

// Structure to store view state (bounds and iteration count)
struct ViewState
{
    FloatType x_min;
    FloatType x_max;
    FloatType y_min;
    FloatType y_max;
    int max_iterations;

    ViewState() : x_min(0), x_max(0), y_min(0), y_max(0), max_iterations(0) {}
    ViewState(FloatType xmin, FloatType xmax, FloatType ymin, FloatType ymax, int max_iter) : x_min(xmin), x_max(xmax), y_min(ymin), y_max(ymax), max_iterations(max_iter) {}
};

// Texture update callback - called by renderer when texture needs to be created/updated
// Platform-specific implementation should handle texture creation/update
typedef void (*TextureUpdateCallback)(ImTextureID* texture_id, const unsigned char* pixels, int width, int height);

// Texture deletion callback - called by renderer when texture needs to be destroyed
typedef void (*TextureDeleteCallback)(ImTextureID texture_id);

// Pure ImGui-based renderer implementation (no platform-specific code)
// Uses ImGui's Image widget and canvas API exclusively
class ImGuiRenderer : public RenderCallback
{
public:
    ImGuiRenderer(MandelbrotRenderer* renderer, 
                  TextureUpdateCallback update_callback = nullptr,
                  TextureDeleteCallback delete_callback = nullptr);
    ~ImGuiRenderer();
    
    void on_pixels_updated(const unsigned char* pixels, int width, int height) override;
    
    void draw();  // Draw ImGui window using pure ImGui APIs
    
private:
    bool is_render_in_progress() const;  // Check if a render is currently in progress
    void apply_view_state(const ViewState& state);   // Apply a view state to the renderer
    void save_view_state(const std::string& name);              // Update a saved view with current renderer state
    void save_views_to_file(bool include_current_view = true);  // Save saved views to JSON file (include_current_view = true saves current view too)
    void load_views_from_file();                                // Load saved views from JSON file
    std::string get_config_file_path() const;                   // Get the path to the config file (~/.mandel)

    constexpr static FloatType zoom_step_ = static_cast<FloatType>(0.5);

    MandelbrotRenderer* renderer_;
    ImTextureID texture_front_;  // Currently displayed texture
    ImTextureID texture_back_;   // Receives new uploads, then swapped to front
    int width_;
    int height_;
    const unsigned char* pixels_;
    bool texture_dirty_;
    unsigned int render_generation_;  // Counter for debugging
    TextureUpdateCallback update_callback_;
    TextureDeleteCallback delete_callback_;
    
    // Interactive view control
    bool is_dragging_;
    ImVec2 last_drag_pos_;  // Last mouse position during drag
    
    // Visual offset for panning - accumulated during drag for immediate feedback
    float display_offset_x_;
    float display_offset_y_;
    
    // Offset snapshot - the offset when we started the current render
    // Used to calculate effective display offset and adjust after render
    float render_start_offset_x_;
    float render_start_offset_y_;
    
    // Display scale for zooming - stretch/shrink texture for immediate feedback
    float display_scale_;
    
    // Zoom center (in screen pixels) - where to anchor the scale transformation
    float zoom_center_x_;
    float zoom_center_y_;
    
    // When true, don't update display texture (keep showing old content during render)
    bool suppress_texture_updates_;
    
    bool is_rendering_;  // True if a render is in progress

    // Initial view state (for reset) - stored when renderer is first initialized
    bool initial_bounds_set_;
    FloatType initial_x_min_;
    FloatType initial_x_max_;
    FloatType initial_y_min_;
    FloatType initial_y_max_;
    uint64_t initial_zoom_;
    int initial_max_iterations_;

    // Track if first window resize has happened
    bool first_window_size_set_;
    
    // Track threading state (controlled by UI checkbox)
    bool threading_enabled_;

    // Track controls window transparency state
    bool controls_window_should_be_transparent_;

    // Saved views management
    std::map<std::string, ViewState> saved_views_;
    char new_view_name_buffer_[256];            // Buffer for new view name input
    bool views_need_save_;                      // Flag to track if views need to be saved
    ViewState loaded_current_view_;  // Current view loaded from file
    bool has_loaded_current_view_;              // Flag to track if we've loaded a current view

    // Pending settings management (for when render is in progress)
    ViewState applied_settings_;  // Last applied settings (for comparison)
    ViewState pending_settings_;  // Pending settings waiting to be applied
    bool has_pending_settings_;              // Flag indicating if there are pending settings

    void apply_pending_settings_if_ready();  // Check if render is done and apply pending settings
};

}  // namespace mandel
