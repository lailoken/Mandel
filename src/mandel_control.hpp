#pragma once

#include "imgui.h"

#include <map>
#include <string>

#include "config.hpp"
#include "mandel.hpp"

namespace mandel
{

// Forward declaration
class MandelWorker;

// Interface for control window to interact with UI
class MandelUIControlInterface
{
public:
    virtual ~MandelUIControlInterface() = default;
    
    // Get current state
    virtual ViewState get_viewport_bounds() const = 0;
    virtual bool is_render_in_progress() const = 0;
    virtual int get_max_iterations() const = 0;
    virtual unsigned int get_render_generation() const = 0;
    virtual bool is_dragging() const = 0;
    virtual bool is_rendering() const = 0;
    
    // Get pending/applied settings
    virtual ViewState get_applied_settings() const = 0;
    virtual ViewState get_pending_settings() const = 0;
    virtual bool has_pending_settings() const = 0;
    
    // Get initial bounds
    virtual ViewState get_initial_bounds() const = 0;
    virtual uint64_t get_initial_zoom() const = 0;
    
    // Actions
    virtual void set_pending_settings(const ViewState& settings) = 0;
    virtual void apply_view_state(const ViewState& state) = 0;
    virtual void save_view_state(const std::string& name) = 0;
    virtual void extend_bounds_for_overscan(FloatType& x_min, FloatType& x_max, FloatType& y_min, FloatType& y_max) const = 0;
    virtual void apply_pending_settings_if_ready() = 0;
    virtual void reset_to_initial() = 0;
    
    // Saved views
    virtual std::map<std::string, ViewState>& get_saved_views() = 0;
    virtual char* get_new_view_name_buffer() = 0;
};

// Control window class that handles the UI controls
class MandelControl
{
public:
    MandelControl(MandelUIControlInterface* ui_interface);
    ~MandelControl() = default;
    
    void draw();  // Draw the control window
    
private:
    MandelUIControlInterface* ui_interface_;
    bool controls_window_should_be_transparent_;
};

}  // namespace mandel
