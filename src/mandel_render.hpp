#pragma once

#include "imgui.h"
#include "mandel.hpp"

namespace mandel
{
// Forward declaration
template<typename FloatType>
class MandelbrotRenderer;

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
    
    // Initial view state (for reset)
    bool initial_bounds_set_;
    FloatType initial_x_min_;
    FloatType initial_x_max_;
    FloatType initial_y_min_;
    FloatType initial_y_max_;
    uint64_t initial_zoom_;
    
    // Track if first window resize has happened
    bool first_window_size_set_;
    
    // Track threading state (controlled by UI checkbox)
    bool threading_enabled_;
};

}  // namespace mandel

// Include template implementation
#include "mandel_render.inl"
