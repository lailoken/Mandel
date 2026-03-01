#include "mandel_control.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include "config.hpp"
#include "mandel.hpp"

namespace mandel
{

// ImGui input helper for long double (convert to/from double since ImGui doesn't support long double)
namespace detail
{
    inline bool imgui_input_float(const char* label, FloatType* v, FloatType step, FloatType step_fast, const char* format)
    {
        // For long double, convert to/from double since ImGui doesn't support it directly
        // Convert format string from long double format to double format if provided
        const char* double_format = format;
        if (format != nullptr)
        {
            // Replace "%.10Lf" with "%.10f" (remove L modifier for double)
            // This is a simple approach - format should be something like "%.10Lf"
            static thread_local char format_buf[16];
            if (strstr(format, "Lf") != nullptr || strstr(format, "Le") != nullptr)
            {
                // Copy format and replace L with nothing
                size_t len = strlen(format);
                if (len < sizeof(format_buf))
                {
                    strncpy(format_buf, format, sizeof(format_buf) - 1);
                    format_buf[sizeof(format_buf) - 1] = '\0';
                    // Find and replace L
                    for (char* p = format_buf; *p; ++p)
                    {
                        if (*p == 'L' && (p[1] == 'f' || p[1] == 'e' || p[1] == 'g'))
                        {
                            // Remove L by shifting
                            memmove(p, p + 1, strlen(p));
                            break;
                        }
                    }
                    double_format = format_buf;
                }
            }
        }

        // Validate input value - if NaN or infinite, use 0
        double temp = std::isnan(*v) || std::isinf(*v) ? 0.0 : static_cast<double>(*v);
        double step_d = static_cast<double>(step);
        double step_fast_d = static_cast<double>(step_fast);
        bool result = ImGui::InputScalar(label,
                                         ImGuiDataType_Double,
                                         &temp,
                                         step != static_cast<FloatType>(0.0) ? &step_d : nullptr,
                                         step_fast != static_cast<FloatType>(0.0) ? &step_fast_d : nullptr,
                                         double_format);
        // Only write back when the user actually edited the field.
        // Unconditional write-back silently double-rounds the long-double value every frame;
        // the round-tripped value then compared against full-precision applied_settings_ can
        // fire spuriously at deep zoom (catastrophic cancellation in x_range → imprecise zoom
        // → ImGui clamps the slider → zoom_edited = true → feedback loop).
        if (result && !std::isnan(temp) && !std::isinf(temp))
        {
            *v = static_cast<FloatType>(temp);
        }
        return result;
    }
}

MandelControl::MandelControl(MandelUIControlInterface* ui_interface)
    : ui_interface_(ui_interface), controls_window_should_be_transparent_(false)
{
}

void MandelControl::draw()
{
    // Create controls window (after background setup so it appears on top)
    // Set window alpha based on previous frame's focus/hover state
    if (controls_window_should_be_transparent_)
    {
        ImGui::SetNextWindowBgAlpha(0.5f);
    }
    else
    {
        ImGui::SetNextWindowBgAlpha(1.0f);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(350.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0f, 300.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Controls"))
    {
        // Check if window is focused or hovered for next frame
        bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        controls_window_should_be_transparent_ = !is_focused && !is_hovered;

        if (ui_interface_)
        {
            // Display controls using ImGui widgets
            // Use FloatType directly with appropriate ImGui input functions
            // Initialize applied_settings_ from worker if not yet initialized (first frame after render completes)
            ViewState applied_settings = ui_interface_->get_applied_settings();
            if (applied_settings.max_iterations == 0 && !ui_interface_->is_render_in_progress())
            {
                ViewState viewport = ui_interface_->get_viewport_bounds();
                applied_settings = ViewState(viewport.x_min, viewport.x_max, viewport.y_min, viewport.y_max, ui_interface_->get_max_iterations());
            }

            // Get current settings (from worker if no pending, otherwise from pending)
            // Use viewport bounds (what user sees), not buffer bounds
            int max_iter = ui_interface_->has_pending_settings() ? ui_interface_->get_pending_settings().max_iterations : ui_interface_->get_max_iterations();
            FloatType x_min, x_max, y_min, y_max;
            if (ui_interface_->has_pending_settings())
            {
                ViewState pending = ui_interface_->get_pending_settings();
                x_min = pending.x_min;
                x_max = pending.x_max;
                y_min = pending.y_min;
                y_max = pending.y_max;
            }
            else
            {
                ViewState viewport = ui_interface_->get_viewport_bounds();
                x_min = viewport.x_min;
                x_max = viewport.x_max;
                y_min = viewport.y_min;
                y_max = viewport.y_max;
            }

            // Calculate zoom from bounds (always derive from current bounds, not from worker)
            FloatType zoom = static_cast<FloatType>(1.0);
            FloatType x_range = x_max - x_min;
            FloatType y_range = y_max - y_min;
            if (x_range > static_cast<FloatType>(0.0) && y_range > static_cast<FloatType>(0.0))
            {
                FloatType avg_range = (x_range + y_range) / static_cast<FloatType>(2.0);
                zoom = static_cast<FloatType>(4.0) / avg_range;
            }

            // Get max zoom for clamping
            static const FloatType max_zoom = MandelbrotRenderer::max_zoom;
            static const FloatType min_zoom = static_cast<FloatType>(0.1);

            // Read UI values and check if they differ from applied settings
            bool settings_changed = false;
            ImGui::SliderInt("Max Iterations", &max_iter, 2, 4096, "%d", ImGuiSliderFlags_Logarithmic);

            ImGui::PushItemWidth(120.f);
            // Use the larger epsilon between float and FloatType - scaled for practical UI use
            // Epsilon is of FloatType
            constexpr FloatType eps = std::numeric_limits<FloatType>::epsilon() * static_cast<FloatType>(100.0);

            // Format string for long double precision
            const char* format_str = "%.16Lf";
            FloatType step = static_cast<FloatType>(0.0);

            ImGui::TextUnformatted("Min:");
            ImGui::SameLine();
            detail::imgui_input_float(",##min_x", &x_min, step, step, format_str);
            if (x_min >= x_max)
            {
                x_max = x_min + eps;
            }

            ImGui::SameLine();
            detail::imgui_input_float(",##min_y", &y_min, step, step, format_str);
            if (y_max <= y_min)
            {
                y_max = y_min + eps;
            }

            ImGui::TextUnformatted("Max:");
            ImGui::SameLine();

            detail::imgui_input_float(",##max_x", &x_max, step, step, format_str);
            if (x_min >= x_max)
            {
                x_min = x_max - eps;
            }

            ImGui::SameLine();
            detail::imgui_input_float(",##max_y", &y_max, step, step, format_str);
            if (y_min >= y_max)
            {
                y_min = y_max - eps;
            }
            ImGui::PopItemWidth();

            // Zoom slider - when changed, convert to bounds
            // Note: We use double for ImGui slider, which may lose precision for long double, but we detect changes directly
            double zoom_d = static_cast<double>(zoom);
            double const min_zoom_d = static_cast<double>(min_zoom);
            double const max_zoom_d = static_cast<double>(max_zoom);

            // Convert zoom to log10 for display (power)
            // If zoom is 1.0 (10^0), display 0.0
            // If zoom is 10.0 (10^1), display 1.0
            // If zoom is 1000.0 (10^3), display 3.0
            // We'll construct a format string like "10^%.2f"
            double log_zoom = std::log10(std::max(zoom_d, min_zoom_d));

            // Format string: "10^<power>"
            char zoom_format[32];
            std::snprintf(zoom_format, sizeof(zoom_format), "10^%%.2f");

            // But wait, ImGui::SliderScalar displays the VALUE, not a derived string from the value unless we change the value type or intercept the display.
            // SliderScalar uses the value passed to it. If we want to display the power, we could map the slider to the log scale directly?
            // But the slider is already logarithmic (ImGuiSliderFlags_Logarithmic).
            // Let's keep the slider value as the actual zoom, but provide a custom format string if possible?
            // ImGui::SliderScalar format is for the value itself.

            // Alternative: Don't use standard display. Render the slider with an empty format "" and draw text manually?
            // Or just format the value in the slider as "%.2e" (scientific notation) which is close to 10^X.
            // But the user specifically asked for "10^X".

            // ImGui doesn't support custom value formatting callbacks easily in standard widgets.
            // However, we can use a hack: pass "" as format to hide the number, then draw text over it?
            // Or better: Use ImGui::SliderScalar with "10^%.2f" format? NO, because that would expect the value to be the power.

            // Let's change the slider to control the EXPONENT (log10(zoom)) instead of the zoom directly.
            // Then we can format it as "10^%.2f".
            // min_zoom_log = log10(0.1) = -1.0
            // max_zoom_log = log10(1e15) = 15.0

            double log_zoom_val = std::log10(zoom_d);
            double min_log_zoom = std::log10(min_zoom_d);
            double max_log_zoom = std::log10(max_zoom_d);
            // Pre-clamp before passing to SliderScalar: if the value is already out of the
            // slider range, ImGui will clamp it internally AND return true, which would be
            // indistinguishable from a genuine user interaction and would trigger a spurious
            // re-render (with catastrophic-cancellation coordinates) at deep zoom levels.
            log_zoom_val = std::clamp(log_zoom_val, min_log_zoom, max_log_zoom);

            bool zoom_edited = ImGui::SliderScalar("Zoom", ImGuiDataType_Double, &log_zoom_val, &min_log_zoom, &max_log_zoom, "10^%.2f");

            // Tooltip for precise value
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("Zoom Factor: x%.2f", zoom_d);
            }

            if (zoom_edited)
            {
                // Convert back from log10(zoom) to zoom
                double new_zoom_d = std::pow(10.0, log_zoom_val);

                FloatType new_zoom = static_cast<FloatType>(new_zoom_d);
                new_zoom = std::clamp(new_zoom, min_zoom, max_zoom);
                // Convert zoom change to bounds change
                FloatType center_x = (x_min + x_max) / static_cast<FloatType>(2.0);
                FloatType center_y = (y_min + y_max) / static_cast<FloatType>(2.0);
                FloatType scale = static_cast<FloatType>(4.0) / new_zoom;
                x_min = center_x - scale / static_cast<FloatType>(2.0);
                x_max = center_x + scale / static_cast<FloatType>(2.0);
                y_min = center_y - scale / static_cast<FloatType>(2.0);
                y_max = center_y + scale / static_cast<FloatType>(2.0);
                // Force settings changed when zoom is edited (precision loss in double conversion can make bounds comparison fail)
                settings_changed = true;
            }

            // Compare current UI values with applied settings to detect changes (only if zoom wasn't just edited)
            if (!zoom_edited)
            {
                const FloatType comparison_eps =
                    std::max(static_cast<FloatType>(std::numeric_limits<float>::epsilon()), std::numeric_limits<FloatType>::epsilon()) * static_cast<FloatType>(100.0);
                settings_changed = (max_iter != applied_settings.max_iterations) || (std::abs(x_min - applied_settings.x_min) > comparison_eps) ||
                                   (std::abs(x_max - applied_settings.x_max) > comparison_eps) || (std::abs(y_min - applied_settings.y_min) > comparison_eps) ||
                                   (std::abs(y_max - applied_settings.y_max) > comparison_eps);
            }

            // Update pending settings if changed
            if (settings_changed)
            {
                ui_interface_->set_pending_settings(ViewState(x_min, x_max, y_min, y_max, max_iter));
            }

            ImGui::Text("Render Generation: %d", ui_interface_->get_render_generation());

            bool thread_pool_active = ui_interface_->is_render_in_progress();
            bool is_dragging = ui_interface_->is_dragging();

            ImGui::BeginDisabled();
            ImGui::Checkbox("Thread Pool Active", &thread_pool_active);
            ImGui::SameLine();
            ImGui::Checkbox("Dragging", &is_dragging);
            ImGui::EndDisabled();

            // Saved Views section
            ImGui::Separator();
            ImGui::Text("Saved Views");

            // Input for new view name and save button
            ImGui::PushItemWidth(200.0f);
            char* new_view_name_buffer = ui_interface_->get_new_view_name_buffer();
            ImGui::InputText("##NewViewName", new_view_name_buffer, 256);
            ImGui::SameLine();
            std::string new_view_name(new_view_name_buffer);
            std::map<std::string, ViewState>& saved_views = ui_interface_->get_saved_views();
            bool can_save = !new_view_name.empty() && saved_views.find(new_view_name) == saved_views.end() && ui_interface_ != nullptr;
            if (!can_save)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Save Current View"))
            {
                // Save current viewport state (not buffer bounds)
                ViewState viewport = ui_interface_->get_viewport_bounds();
                saved_views[new_view_name] = viewport;
                new_view_name_buffer[0] = '\0';  // Clear the buffer
                // Save immediately on manual edit
                save_views_to_file(saved_views);  // Don't save current view (only named views)
            }
            if (!can_save)
            {
                ImGui::EndDisabled();
            }
            ImGui::PopItemWidth();

            // Table of saved views
            ImGui::Spacing();

            ImGui::BeginDisabled(ui_interface_->is_render_in_progress());
            ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable | ImGuiTableFlags_NoSavedSettings;
            if (ImGui::BeginTable("SavedViews", 5, table_flags, ImVec2(0, -FLT_MIN)))
            {
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 80);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 180);
                ImGui::TableSetupColumn("Iterations", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("X Range", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 150);
                ImGui::TableSetupColumn("Y Range", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 150);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                // Pseudo item for reset state (cannot be deleted or set, but can be applied)
                ImGui::TableNextRow();
                if (ImGui::TableSetColumnIndex(1))  // Name column - skip the actions column
                {
                    if (ImGui::Selectable("<Initial>", false, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        ui_interface_->reset_to_initial();
                    }
                }

                const char* view_format_str = "%.8f to %.8f";
                ViewState initial = ui_interface_->get_initial_bounds();

                if (ImGui::TableNextColumn())  // Iterations
                {
                    ImGui::Text("%d", initial.max_iterations);
                }

                if (ImGui::TableNextColumn())  // X Range
                {
                    ImGui::Text(view_format_str, static_cast<double>(initial.x_min), static_cast<double>(initial.x_max));
                }

                if (ImGui::TableNextColumn())  // Y Range
                {
                    ImGui::Text(view_format_str, static_cast<double>(initial.y_min), static_cast<double>(initial.y_max));
                }

                // Saved views
                for (auto it = saved_views.begin(); it != saved_views.end();)
                {
                    ImGui::TableNextRow();

                    bool delete_clicked = false;

                    const std::string& name = it->first;
                    const ViewState& state = it->second;

                    if (ImGui::TableNextColumn())  // Actions
                    {
                        ImGui::PushID(name.c_str());
                        if (ImGui::Button("[X]"))
                        {
                            delete_clicked = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("[Save]"))
                        {
                            ui_interface_->save_view_state(name);
                        }
                        ImGui::PopID();
                    }

                    if (ImGui::TableNextColumn())  // Name
                    {
                        if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            ui_interface_->apply_view_state(state);
                        }
                    }

                    if (ImGui::TableNextColumn())  // Iterations
                    {
                        ImGui::Text("%d", state.max_iterations);
                    }

                    if (ImGui::TableNextColumn())  // X Range
                    {
                        ImGui::Text(view_format_str, static_cast<double>(state.x_min), static_cast<double>(state.x_max));
                    }

                    if (ImGui::TableNextColumn())  // Y Range
                    {
                        ImGui::Text(view_format_str, static_cast<double>(state.y_min), static_cast<double>(state.y_max));
                    }

                    // Handle delete button
                    if (delete_clicked)
                    {
                        it = saved_views.erase(it);
                        // Save immediately on manual edit
                        save_views_to_file(saved_views);  // Don't save current view (only named views)
                    }
                    else
                    {
                        ++it;
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndDisabled();

            // Try to apply pending settings if render is ready
            ui_interface_->apply_pending_settings_if_ready();
        }
    }
    ImGui::End();
}

}  // namespace mandel
