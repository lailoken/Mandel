#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace mandel
{

std::string get_config_file_path()
{
    const char* home = std::getenv("HOME");
    if (home != nullptr)
    {
        return std::string(home) + "/.mandel";
    }
    // Fallback to current directory if HOME is not set
    return ".mandel";
}

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

    nlohmann::json view_state_to_json(const ViewState& state)
    {
        nlohmann::json j;
        j["x_min"] = float_to_json(state.x_min);
        j["x_max"] = float_to_json(state.x_max);
        j["y_min"] = float_to_json(state.y_min);
        j["y_max"] = float_to_json(state.y_max);
        j["max_iterations"] = state.max_iterations;
        return j;
    }

    std::optional<ViewState> view_state_from_json(const nlohmann::json& j)
    {
        if (!j.contains("x_min") || !j.contains("x_max") || !j.contains("y_min") || !j.contains("y_max") ||
            !j.contains("max_iterations"))
        {
            return std::nullopt;
        }

        return ViewState(float_from_json(j["x_min"]), float_from_json(j["x_max"]), float_from_json(j["y_min"]),
                         float_from_json(j["y_max"]), j["max_iterations"]);
    }
}

void save_views_to_file(const std::map<std::string, ViewState>& saved_views, const ViewState* current_view)
{
    try
    {
        nlohmann::json json_data;
        json_data["views"] = nlohmann::json::array();

        for (const auto& [name, state] : saved_views)
        {
            nlohmann::json view_obj = detail::view_state_to_json(state);
            view_obj["name"] = name;
            json_data["views"].push_back(view_obj);
        }

        if (current_view != nullptr)
        {
            json_data["current_view"] = detail::view_state_to_json(*current_view);
        }

        std::ofstream file(get_config_file_path());
        if (file.is_open())
        {
            file << json_data.dump(2);
        }
    }
    catch (const std::exception&)
    {
        // Silently fail - user can still use views in this session
    }
}

LoadedViews load_views_from_file()
{
    LoadedViews result;

    try
    {
        std::ifstream file(get_config_file_path());
        if (!file.is_open())
        {
            return result;  // File doesn't exist yet, that's okay
        }

        nlohmann::json json_data;
        file >> json_data;

        if (json_data.contains("views") && json_data["views"].is_array())
        {
            for (const auto& view_obj : json_data["views"])
            {
                if (view_obj.contains("name"))
                {
                    if (auto state = detail::view_state_from_json(view_obj))
                    {
                        result.saved_views[view_obj["name"]] = *state;
                    }
                }
            }
        }

        if (json_data.contains("current_view"))
        {
            if (auto state = detail::view_state_from_json(json_data["current_view"]))
            {
                result.current_view = *state;
                result.has_current_view = true;
            }
        }
    }
    catch (const std::exception&)
    {
        // Silently fail - start with empty views
    }

    return result;
}

}  // namespace mandel
