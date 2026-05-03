#include "../app.h"
#include "../utils.h"

#include <imgui/imgui.h>

#include <glaze/glaze.hpp>

#include <cstring>


AppJSONExplorer::AppJSONExplorer(const std::string_view here, std::string s) : json(s), jq(here, json) {
    filter[0] = '.';
    filter[1] = '\0';

    has_update = true;
    last_update = clock::now();
}


bool AppJSONExplorer::render_no_window(AppRoot&) {
    has_update |= ImGui::InputText("##filt", filter.data(), filter.size());
    has_update |= ImGui::Checkbox("Compact", &compact); ImGui::SameLine();
    ImGui::Checkbox("Wrap text", &wrap);

    const auto now = clock::now();
    const bool do_update = has_update && (now - last_update) > min_update_ms;
    jq.set_compact(compact);
    if (do_update && jq.update(filter.data())) {
        last_update = now;
        has_update = false;
    }

    const auto out = jq.get_output();
    if (!out) {
        ImGui::TextUnformatted("loading...");
    } else {
        const auto& [cout, cerr] = *out;
        if (!cerr.empty()) {
            render_copyable_text(cerr, (ImU32) ImColor(0xff, 0, 0), wrap);
        }

        if (!cout.empty()) {
            render_copyable_text(cout, {}, wrap);
        } else if (cerr.empty()) {
            ImGui::TextUnformatted("(jq did not write to stdout or stderr, showing input)");
            render_copyable_text(json, {}, wrap);
        }
    }
    return true;
}


bool AppJSONExplorer::render(AppRoot& root, std::string_view id) {
    constexpr auto wflags = ImGuiWindowFlags_NoSavedSettings;
    const std::string title = std::format("JSON Explorer###json_exp-{}", id);
    bool is_open = true;

    if (ImGui::Begin(title.data(), &is_open, wflags)) {
        set_window_size_by_settings(root.settings, "JSONExplorer/size");
        is_open &= render_no_window(root);
        save_window_size_to_settings(root.settings, "JSONExplorer/size", !is_open);
    }
    ImGui::End();
    return is_open;
}
