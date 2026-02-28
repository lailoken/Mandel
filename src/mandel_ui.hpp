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
#include "worker_base.hpp"

namespace mandel
{

// Texture update callback function type
using TextureUpdateFunc = void (*)(ImTextureID* texture_id, const unsigned char* pixels, int width, int height);
using TextureDeleteFunc = void (*)(ImTextureID texture_id);

// MandelUI class - handles overscan, user input, texture management
// Implements MandelUIControlInterface
class MandelUI : public MandelUIControlInterface
{
public:
   MandelUI(TextureUpdateFunc update_func, TextureDeleteFunc delete_func, ThreadPool& thread_pool);
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

   // Static helper for test: compute swap conversion and verify mouse complex is preserved
   static bool compute_swap_conversion(
       FloatType displayed_canvas_x_min, FloatType displayed_canvas_x_max,
       FloatType displayed_canvas_y_min, FloatType displayed_canvas_y_max,
       FloatType render_start_canvas_x_min, FloatType render_start_canvas_x_max,
       FloatType render_start_canvas_y_min, FloatType render_start_canvas_y_max,
       float display_offset_x, float display_offset_y,
       int viewport_width, int viewport_height,
       int canvas_width, int canvas_height,
       int margin_x, int margin_y,
       FloatType& mouse_complex_x_before, FloatType& mouse_complex_y_before,
       FloatType& mouse_complex_x_after, FloatType& mouse_complex_y_after);

private:
    // Internal helper methods
    void update_textures();
    void handle_input();
    void start_render();
    void convert_viewport_to_canvas_bounds(FloatType viewport_x_min, FloatType viewport_x_max, FloatType viewport_y_min,
                                           FloatType viewport_y_max, FloatType& canvas_x_min, FloatType& canvas_x_max,
                                           FloatType& canvas_y_min, FloatType& canvas_y_max) const;
    void convert_canvas_to_viewport_bounds(FloatType canvas_x_min, FloatType canvas_x_max, FloatType canvas_y_min,
                                           FloatType canvas_y_max, FloatType& viewport_x_min, FloatType& viewport_x_max,
                                           FloatType& viewport_y_min, FloatType& viewport_y_max) const;
    void handle_pan(float display_offset_x, float display_offset_y);
    // Convert display_offset from old texture bounds to new (so same complex point stays under mouse)
    void convert_display_offset_on_swap();

    // Member variables
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
    bool is_dragging_;

    // State
    std::atomic<unsigned int> render_generation_;
    FloatType canvas_x_min_;
    FloatType canvas_x_max_;
    FloatType canvas_y_min_;
    FloatType canvas_y_max_;
    // Bounds for currently displayed texture (what front texture was rendered for)
    FloatType displayed_texture_canvas_x_min_;
    FloatType displayed_texture_canvas_x_max_;
    FloatType displayed_texture_canvas_y_min_;
    FloatType displayed_texture_canvas_y_max_;
    // Bounds for the texture we just rendered (render_start when we started)
    FloatType render_start_canvas_x_min_;
    FloatType render_start_canvas_x_max_;
    FloatType render_start_canvas_y_min_;
    FloatType render_start_canvas_y_max_;

    // Worker (MandelWorker for real Mandelbrot rendering)
    std::unique_ptr<WorkerBase> worker_;
    ThreadPool& thread_pool_;
    bool render_pending_;  // true when async render started but not yet swapped

    // Control window
    MandelControl control_;

    // Settings
    int max_iterations_;
    ViewState applied_settings_;
    ViewState pending_settings_;
    bool has_pending_settings_;

    // Test helper (friend class for unit testing)
    friend class MandelUITestHelper;
};

}  // namespace mandel
