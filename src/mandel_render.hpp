#pragma once

#include "imgui.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "mandel.hpp"

namespace mandel
{
// Forward declaration
template<typename FloatType>
class MandelbrotRenderer;

// Structure to store view state (bounds and iteration count)
template <typename FloatType>
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
template<typename FloatType>
class ImGuiRenderer : public RenderCallback
{
public:
    ImGuiRenderer(MandelbrotRenderer<FloatType>* renderer, 
                  TextureUpdateCallback update_callback = nullptr,
                  TextureDeleteCallback delete_callback = nullptr);
    ~ImGuiRenderer();
    
    void on_pixels_updated(const unsigned char* pixels, int width, int height) override;
    
    void draw();  // Draw ImGui window using pure ImGui APIs
    
private:
    void swap_buffers();
    void clear_canvas();  // Clear canvas before rendering when double buffering is disabled
    bool is_render_in_progress() const;  // Check if a render is currently in progress
    void apply_view_state(const ViewState<FloatType>& state);   // Apply a view state to the renderer
    void save_view_state(const std::string& name);              // Update a saved view with current renderer state
    void save_views_to_file(bool include_current_view = true);  // Save saved views to JSON file (include_current_view = true saves current view too)
    void load_views_from_file();                                // Load saved views from JSON file
    std::string get_config_file_path() const;                   // Get the path to the config file (~/.mandel)

    constexpr static FloatType zoom_step_ = static_cast<FloatType>(0.5);

    MandelbrotRenderer<FloatType>* renderer_;
    ImTextureID texture_front_;  // Currently displayed texture (front buffer)
    ImTextureID texture_back_;   // Texture being updated (back buffer)
    int width_;
    int height_;
    const unsigned char* pixels_;
    const unsigned char* pixels_being_updated_;  // Track which pixels we're currently updating
    bool texture_dirty_;
    bool double_buffering_enabled_;
    unsigned int render_generation_;  // Increments each time a new render starts
    unsigned int pixels_generation_;  // Generation of the current pixels_
    unsigned int swapped_generation_;  // Last generation we swapped buffers for
    TextureUpdateCallback update_callback_;
    TextureDeleteCallback delete_callback_;
    
    // Interactive view control
    bool is_dragging_;
    ImVec2 last_drag_pos_;  // Last mouse position we regenerated for

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

    // Recurse size limit
    int recurse_size_limit_;

    // Track controls window transparency state
    bool controls_window_should_be_transparent_;

    // Saved views management
    std::map<std::string, ViewState<FloatType>> saved_views_;
    char new_view_name_buffer_[256];            // Buffer for new view name input
    bool views_need_save_;                      // Flag to track if views need to be saved
    ViewState<FloatType> loaded_current_view_;  // Current view loaded from file
    bool has_loaded_current_view_;              // Flag to track if we've loaded a current view
};

}  // namespace mandel

// Include template implementation
#include "mandel_render.inl"
