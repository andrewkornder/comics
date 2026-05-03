#include "../app.h"

#include <imgui/imgui.h>


bool AppErrorFilter::apply(const Error& err) {
    return !present || value.empty() || value == get_error_tag(err);
}
bool AppErrorFilter::apply(const ErrorList& errs) {
    return !present || (value.empty() ? !errs.items.empty() : errs.counts.find(value) != errs.counts.end());
}

void AppErrorFilter::render_no_window(AppRoot&, const std::map<std::string, std::size_t, std::less<>>& cnts, bool optional) {
    if (!optional && !present) {
        present = true;
        value.clear();
    }

    // optional  => "", "Any", ...
    // !optional => "" (Any), ...
    
    const std::string any = std::format("[{}] Any", cnts.contains("") ? cnts.at("") : 0);

    const std::string preview = optional ? 
        present ? value.empty() ? any : value : "" : 
                  value;
    if (!ImGui::BeginCombo("Error Filter", preview.data())) {
        return;
    }

    if (optional) {
        if (ImGui::Selectable("###err_none", !present)) {
            present = false;
            value = "";
        }
        if (ImGui::Selectable(any.data(), present && value.empty())) {
            present = true;
            value = "";
        }
    } else {
        if (ImGui::Selectable("###err_none", value.empty())) {
            present = true;
            value = "";
        }
    }

    for (const auto& [name, cnt] : cnts) {
        if (name.empty()) continue;

        if (ImGui::Selectable(std::format("[{}] {}", cnt, name).c_str(), value == name)) {
            present = true;
            value = name;
        }
    }
    ImGui::EndCombo();
}

