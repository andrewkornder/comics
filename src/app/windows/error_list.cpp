#include "../app.h"
#include "../utils.h"
#include "../colors.h"
#include "json.hpp"

#include <imgui/imgui.h>
#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>


AppErrorList::AppErrorList(std::string title, const ErrorList& errors, const std::vector<std::size_t>& subset) 
    : title(std::move(title)), errors{} {
    if (subset.empty()) {
        for (const auto& item : errors.items) {
            this->errors.add(item);
        }
    } else {
        for (const std::size_t i : subset) {
            this->errors.add(errors.items[i]);
        }
    }
}

bool AppErrorList::render_no_window(AppRoot& root) {
    filter.render_no_window(root, errors.counts, false);
    ImGui::Checkbox("Render as json?", &as_json);

    const auto render_one = [this](const Error& err) {
        if (filter.apply(err)) {
            IDGuard id(&err);
            if (as_json) {
                render_json(std::make_pair(get_error_tag(err), err), false);
            } else {
                const std::string text = format_error(err);
                render_copyable_text(text);
                if (ImGui::IsItemHovered()) {
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();

                    ImGui::GetWindowDrawList()->AddRect(
                        {min.x - 5, min.y - 5}, {max.x + 5, max.y + 5},
                        ImColor(0xff, 0xff, 0xff)
                    );
                }
            }
        }
    };

    for (const Error& err : errors.items) {
        render_one(err);
    }
    return true;
}

bool AppErrorList::render(AppRoot& root, std::string_view id) {
    constexpr auto wflags = ImGuiWindowFlags_NoSavedSettings;

    const std::string title = std::format("Errors: {}###error list-{}", this->title, id);
    bool is_open = true;
    if (ImGui::Begin(title.c_str(), &is_open, wflags)) {
        set_window_size_by_settings(root.settings, "ErrorList/size");
        is_open &= render_no_window(root);
        save_window_size_to_settings(root.settings, "ErrorList/size", !is_open);
    }
    ImGui::End();
    return is_open;
}
