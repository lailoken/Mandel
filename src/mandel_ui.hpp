#pragma once

#include "imgui.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "config.hpp"
#include "mandel_control.hpp"
#include "mandel_worker.hpp"
#include "overscan_viewport.hpp"
#include "thread_pool.hpp"

namespace mandel
{

// Texture update callback function type
using TextureUpdateFunc = void (*)(ImTextureID* texture_id, const unsigned char* pixels, int width, int height);
using TextureDeleteFunc = void (*)(ImTextureID texture_id);

// MandelUI class - handles overscan, user input, texture management, and visual offset/scale
// Implements MandelUIControlInterface
class MandelUI : public MandelUIControlInterface
{
public:
    MandelUI(TextureUpdateFunc update_func, TextureDeleteFunc delete_func, ThreadPool* thread_pool);
    ~MandelUI();

    // MandelUIControlInterface interface
    ViewState get_viewport_bounds() const override;
    bool is_render_in_progress() const override;
    int get_max_iterations() const override;
    unsigned int get_render_generation() const override;
    bool is_dragging() const override;
    bool is_rendering() const override;
    ViewState get_applied_settings() const override;
    ViewState get_pending_settings() const override;
    bool has_pending_settings() const override;
    ViewState get_initial_bounds() const override;
    uint64_t get_initial_zoom() const override;
    void set_pending_settings(const ViewState& settings) override;
    void apply_view_state(const ViewState& state) override;
    void save_view_state(const std::string& name) override;
    void extend_bounds_for_overscan(FloatType& x_min, FloatType& x_max, FloatType& y_min, FloatType& y_max) const override;
    void apply_pending_settings_if_ready() override;
    void reset_to_initial() override;
    std::map<std::string, ViewState>& get_saved_views() override;
    char* get_new_view_name_buffer() override;

    // Main draw function (called each frame)
    void draw();

    // Handle window resize
    void handle_resize(int viewport_width, int viewport_height);

private:
    // Internal helper methods
    void update_textures();
    void handle_input();
    void start_render();
    void calculate_overscan_margins();
    void convert_viewport_to_canvas_bounds(FloatType viewport_x_min, FloatType viewport_x_max, FloatType viewport_y_min,
                                           FloatType viewport_y_max, FloatType& canvas_x_min, FloatType& canvas_x_max,
                                           FloatType& canvas_y_min, FloatType& canvas_y_max) const;
    void convert_canvas_to_viewport_bounds(FloatType canvas_x_min, FloatType canvas_x_max, FloatType canvas_y_min,
                                           FloatType canvas_y_max, FloatType& viewport_x_min, FloatType& viewport_x_max,
                                           FloatType& viewport_y_min, FloatType& viewport_y_max) const;
    void handle_zoom(float zoom_factor, float mouse_x, float mouse_y);
    void handle_pan(float delta_x, float delta_y);
    void start_background_render(float offset_delta_x, float offset_delta_y);
    
    // Swap calculation - extracts the logic for preserving mouse coordinate during buffer swap
    void calculate_swap_display_offset_adjustment(float& display_offset_x, float& display_offset_y) const;

    // Member variables
    ThreadPool* thread_pool_;  // External thread pool (owned by main_sdl.cpp)
    TextureUpdateFunc update_texture_func_;
    TextureDeleteFunc delete_texture_func_;

    // Viewport with overscan support
    OverscanViewport overscan_viewport_;

    // Double-buffered textures
    ImTextureID texture_front_;
    ImTextureID texture_back_;

    // Panning state
    float display_offset_x_;
    float display_offset_y_;
    float render_start_offset_x_;
    float render_start_offset_y_;
    FloatType render_start_canvas_x_min_;
    FloatType render_start_canvas_x_max_;
    FloatType render_start_canvas_y_min_;
    FloatType render_start_canvas_y_max_;
    // Bounds of the currently displayed texture (what texture_front_ was rendered for)
    FloatType displayed_texture_canvas_x_min_;
    FloatType displayed_texture_canvas_x_max_;
    FloatType displayed_texture_canvas_y_min_;
    FloatType displayed_texture_canvas_y_max_;
    bool is_dragging_;
    bool suppress_texture_updates_;
    unsigned int processed_generation_;  // Track which generation was last processed (prevents old renders from being processed)

    // Zooming state
    float display_scale_;
    float zoom_center_x_;
    float zoom_center_y_;
    constexpr static float zoom_step_ = 0.5f;  // Zoom base = 1.5 (doubles/halves)

    // State (owned by UI)
    std::atomic<unsigned int> render_generation_;
    FloatType canvas_x_min_;
    FloatType canvas_x_max_;
    FloatType canvas_y_min_;
    FloatType canvas_y_max_;
    // No mutex needed: UI updates bounds freely, workers capture snapshot at render start

    // Worker (back buffer)
    std::unique_ptr<MandelWorker> worker_;

    // Settings management
    ViewState applied_settings_;
    ViewState pending_settings_;
    bool has_pending_settings_;

    // Initial state (for reset)
    ViewState initial_bounds_;
    uint64_t initial_zoom_;

    // Saved views
    std::map<std::string, ViewState> saved_views_;
    char new_view_name_buffer_[256];

    // Control window
    MandelControl control_;

    // Max iterations
    int max_iterations_;
    
    // Test helper (friend class for unit testing)
    friend class MandelUITestHelper;
};

}  // namespace mandel
