#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "mandel.hpp"

namespace mandel
{

// Structure to store view state (bounds and iteration count)
struct ViewState
{
    FloatType x_min;
    FloatType x_max;
    FloatType y_min;
    FloatType y_max;
    int max_iterations;

    ViewState() : x_min(0), x_max(0), y_min(0), y_max(0), max_iterations(0) {}
    ViewState(FloatType xmin, FloatType xmax, FloatType ymin, FloatType ymax, int max_iter)
        : x_min(xmin), x_max(xmax), y_min(ymin), y_max(ymax), max_iterations(max_iter)
    {
    }
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
