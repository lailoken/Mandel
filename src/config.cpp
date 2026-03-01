#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mandel
{

std::string get_config_file_path()
{
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.mandel" : ".mandel";
}

namespace detail
{
    std::string float_to_json(FloatType value)
    {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss.precision(21);
        oss << value;
        return oss.str();
    }

    FloatType float_from_json(const nlohmann::json& j)
    {
        return j.is_string()
            ? std::strtold(j.get<std::string>().c_str(), nullptr)
            : static_cast<FloatType>(j.get<double>());
    }

    nlohmann::json view_state_to_json(const ViewState& state)
    {
        return {
            {"mid_x", float_to_json(state.midpoint_x)},
            {"mid_y", float_to_json(state.midpoint_y)},
            {"zoom", float_to_json(state.zoom)},
            {"max_itr", state.max_iterations}
        };
    }

    std::optional<ViewState> view_state_from_json(const nlohmann::json& j)
    {
        if (!j.contains("mid_x") || !j.contains("mid_y") || !j.contains("zoom") || !j.contains("max_itr"))
            return std::nullopt;
        return ViewState(float_from_json(j["mid_x"]), float_from_json(j["mid_y"]),
                        float_from_json(j["zoom"]), j["max_itr"]);
    }
}

void save_views_to_file(const std::map<std::string, ViewState>& saved_views, const ViewState* current_view)
{
    try
    {
        nlohmann::json j;
        j["views"] = nlohmann::json::array();
        for (const auto& [name, state] : saved_views)
        {
            auto obj = detail::view_state_to_json(state);
            obj["name"] = name;
            j["views"].push_back(obj);
        }
        if (current_view)
            j["current_view"] = detail::view_state_to_json(*current_view);

        std::ofstream f(get_config_file_path());
        if (f) f << j.dump(2);
    }
    catch (const std::exception&) {}
}

LoadedViews load_views_from_file()
{
    LoadedViews out;
    try
    {
        std::ifstream f(get_config_file_path());
        if (!f) return out;

        nlohmann::json j;
        f >> j;

        if (j.contains("views") && j["views"].is_array())
        {
            for (const auto& v : j["views"])
            {
                if (!v.contains("name")) continue;
                if (auto state = detail::view_state_from_json(v))
                    out.saved_views[v["name"]] = *state;
            }
        }

        if (j.contains("current_view"))
        {
            if (auto state = detail::view_state_from_json(j["current_view"]))
            {
                out.current_view = *state;
                out.has_current_view = true;
            }
        }
    }
    catch (const std::exception&) {}
    return out;
}

}  // namespace mandel
