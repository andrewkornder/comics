#include "../app.h"
#include "../utils.h"
#include "../../types_glz.h"
#include "json.hpp"

#include <cmath>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>


float unbounded_lerp(float a, float b, float t) {
    return (1 - t) * a + t * b;
}

void rescale(float& r, float& s, float k, float t) {
    // the uv coordinates AFTER scaling should remain the same
    // also, the value of (u1 - u0) should become zoom * (u1 - u0)
    // 
    // for each axis, u or v, define the endpoints to be r and s
    // the endpoints after the transformation are named a and b
    // let k be the zoom factor and t be the interpolated distance along the axis of the mouse
    //
    // (s - r) * k = b - a
    // lerp(r, s, t) = lerp(a, b, t)
    //
    // r + (s - r)t = a + (b - a)t
    // r + (s - r)t = a + (s - r)kt
    // a = r + (s - r)t - (s - r)kt
    // a = r + (s - r)(1 - k)t
    // a = lerp(r, s, t * (1 - k))
    //
    // b - a = (s - r)k
    // b = a + (s - r)k
    const float a = unbounded_lerp(r, s, t * (1 - k));
    const float b = a + (s - r) * k;

    r = a;
    s = b;
}


void AppImageInspect::update_image(float dx, float dy, float ds) {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float w = max.x - min.x, h = max.y - min.y;

    auto [x, y] = ImGui::GetIO().MousePos;
    x -= min.x;
    y -= min.y;

    if (ds != 0) {
        const float zoom = std::pow(0.1f, ds * ImGui::GetIO().DeltaTime);
       
        rescale(u0, u1, zoom, x / w);
        rescale(v0, v1, zoom, y / h);
    }

    if (dx != 0 || dy != 0) {
        u0 -= (u1 - u0) * dx / w;
        u1 -= (u1 - u0) * dx / w;
        v0 -= (v1 - v0) * dy / h;
        v1 -= (v1 - v0) * dy / h;
    }

    /*
    ImGui::SetTooltip(
        "xy = (%5.2f, %5.2f)\n"
        "uv = (%.2f, %.2f) ⊂ [%.2f, %.2f]x[%.2f, %.2f]",
        x, y,
        lerp(u0, u1, x / w), lerp(v0, v1, y / h), u0, u1, v0, v1
    );
    */
}


bool AppImageInspect::render_no_window(AppRoot& root) { 
    const float cheight = ImGui::GetContentRegionAvail().y;
    const float cwidth = cheight * info.width / info.height;
    if (ImGui::BeginChild("##viewer_inner", {cwidth, cheight}, {},
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
        auto [w, h] = ImGui::GetWindowSize();
        h = w * info.height / info.width;

        const auto [loaded, id] = root.images->get_image_id(file, info);
        root.images->add_imgui_image(id, w, h, u0, v0, u1, v1);
        ImGui::InvisibleButton("##viewer_invis_btn", {w, h});
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseLeft);
        ImGui::SetItemKeyOwner(ImGuiKey_R);

        if (ImGui::IsItemHovered()) {
            const auto [dx, dy] = !ImGui::IsMouseDown(ImGuiMouseButton_Left) ? ImVec2() : ImGui::GetIO().MouseDelta;
            auto ds = ImGui::GetIO().MouseWheel;

            update_image(dx, dy, ds);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            u0 = v0 = 0;
            u1 = v1 = 1;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##viewer_details", {}, {}, {})) {
        std::string information = info.width != (std::size_t) -1 ? std::format(
            "{}x{}, {} bands, {:.2f}:1 ({})",
            info.width, info.height, info.bands, (float) info.width / info.height,
            bytes_to_si_prefix(file.file_size)
        ) : "couldn't load dimension information: bad image data";
        const std::string page = file.pages ? file.pages->repr() : "p??";
        information += std::format("\n{} - {}", page, file.position.repr());

        /*
        const auto& mtime = file.mtime;
        information += std::format("\nLast modified: {:04d}-{:02d}-{:02d} at {:02d}:{:02d}:{:02d}",
            mtime[0], mtime[1], mtime[2], 
            mtime[3], mtime[4], mtime[5]
        );
        */

        std::string_view archive_relpath = std::string_view(file.archive_path);
        archive_relpath.remove_prefix(manga.path.size() + 1);
        
        ImGui::TextWrapped("%s", information.c_str());
        ImGui::TextWrapped("%s", manga.name.c_str()); ImGui::Indent();
        ImGui::TextWrapped("%s", archive_relpath.data()); ImGui::Indent();
        ImGui::TextWrapped("%s", file.filename.c_str());
        ImGui::Unindent();
        ImGui::Unindent();

        if (!file.errors.empty() && ImGui::TreeNode("Errors")) {
            if (!error_list) {
                error_list.emplace("", manga.errs, file.errors);
            }
            error_list->render_no_window(root);
            ImGui::TreePop();
        }
        
        if (ImGui::Button("Explore data")) {
            std::string json = glz::write_json(glz::merge{file, info}).value_or("error writing json");
            root.make_new_window(root.json_exp,
                std::format("json_exp/{}/{}", file.archive_path, file.filename), 
                AppJSONExplorer(root.get_root_dir(), std::move(json))
            );
        }

        const std::string meta_title = std::format("Metadata ({} fields)###metadata", info.metadata.size());
        if (!info.metadata.empty() && ImGui::TreeNode(meta_title.c_str())) {
            render_json(info.metadata, true);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    return true;
}

bool AppImageInspect::render(AppRoot& root, std::string_view id) { 
    constexpr auto wflags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    const std::string title = std::format("{}x{} - {}###viewer-{}",
        info.width, info.height, file.filename, id
    );

    bool is_open = true;
    if (ImGui::Begin(title.c_str(), &is_open, wflags)) {
        set_window_size_by_settings(root.settings, "ImageInspect/size");
        is_open &= render_no_window(root);
        save_window_size_to_settings(root.settings, "ImageInspect/size", !is_open);
    }
    ImGui::End();
    return is_open;
}
