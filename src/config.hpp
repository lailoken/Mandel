#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "mandel.hpp"

namespace mandel
{

// Structure to store view state (midpoint + zoom)
// Bounds are derived with 1:1 scale (no stretch): extent follows viewport aspect.
// For square viewport: half_extent = 2/zoom. For non-square: more extent on the longer axis.
struct ViewState
{
    FloatType midpoint_x;
    FloatType midpoint_y;
    FloatType zoom;
    int max_iterations;

    ViewState() : midpoint_x(0), midpoint_y(0), zoom(1), max_iterations(0) {}
    ViewState(FloatType mid_x, FloatType mid_y, FloatType z, int max_iter)
        : midpoint_x(mid_x), midpoint_y(mid_y), zoom(z), max_iterations(max_iter)
    {
    }

    // Derived bounds for square viewport (e.g. saved-views display)
    FloatType half_extent() const { return static_cast<FloatType>(2.0) / zoom; }
    FloatType x_min() const { return midpoint_x - half_extent(); }
    FloatType x_max() const { return midpoint_x + half_extent(); }
    FloatType y_min() const { return midpoint_y - half_extent(); }
    FloatType y_max() const { return midpoint_y + half_extent(); }
};

// Get the path to the config file (~/.mandel)
std::string get_config_file_path();

// JSON serialization/deserialization for ViewState
namespace detail
{
    // Convert FloatType to JSON string (for long double precision)
    std::string float_to_json(FloatType value);

    // Convert JSON to FloatType (supports both string and number formats)
    FloatType float_from_json(const nlohmann::json& j);

    // Convert ViewState to JSON object
    nlohmann::json view_state_to_json(const ViewState& state);

    // Convert JSON object to ViewState (returns nullopt if invalid)
    std::optional<ViewState> view_state_from_json(const nlohmann::json& j);
}

// Save views to JSON file
void save_views_to_file(const std::map<std::string, ViewState>& saved_views,
                        const ViewState* current_view = nullptr);

// Load views from JSON file
struct LoadedViews
{
    std::map<std::string, ViewState> saved_views;
    ViewState current_view;
    bool has_current_view;
};

LoadedViews load_views_from_file();

}  // namespace mandel
