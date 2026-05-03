#include "../app.h"
#include "../utils.h"
#include "../colors.h"

#include "../../types_glz.h"

#include <compare>
#include <glaze/glaze.hpp>
#include <imgui/imgui.h>

#include <fstream>


void AppMangaDetails::render_chapter_details(AppRoot& root, const std::string_view where, const Manga::Collection& coll) {
    IDGuard idg(&coll);
    const std::string repr = coll.position.repr();
    const std::string_view tooltip = where;

    const bool is_opened_chapter = opened_chapter && opened_chapter->first == &coll;
    ImGui::SetNextItemOpen(is_opened_chapter);
    if (!ImGui::TreeNode(repr.c_str())) {
        ImGui::SetItemTooltip("%s", tooltip.data());

        if (is_opened_chapter) {
            opened_chapter = std::nullopt;
        }
        if (!coll.errors.empty()) {
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                ImColor(0xff, 0, 0)
            );
        }
        return;
    }
    if (!coll.errors.empty()) {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImColor(0xff, 0, 0)
        );
    }
    ImGui::SetItemTooltip("%s", tooltip.data());

    if (!opened_chapter || opened_chapter->first != &coll) {
        opened_chapter.emplace(&coll, AppErrorList("", header.errs, coll.errors));
    }

    std::string desc = std::format("{}", where);
    if (!coll.images.empty()) {
        desc += std::format("\n{} images",
            coll.images.size()
        );
    }
    ImGui::TextUnformatted(desc.c_str(), desc.c_str() + desc.size());

    if (ImGui::Button("Copy path?")) {
        copy_to_clipboard(where);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Show in explorer")) {
        show_in_explorer(where, true);
        ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Read")) {
        pending_open = &coll;
    }

    if (!coll.errors.empty() && ImGui::TreeNode("Errors")) {
        opened_chapter->second.render_no_window(root);
        ImGui::TreePop();
    }

    ImGui::TreePop();
}


void AppMangaDetails::render_no_window(AppRoot& root) {
    if (!data) return;

    if (chapter_order.empty()) {
        chapter_order = data->collections | std::views::keys | std::ranges::to<std::vector<std::string>>();
        std::ranges::sort(chapter_order, [this](const std::string& a, const std::string& b) {
            const Position& a_p = data->collections.at(a).begin()->second.position;
            const Position& b_p = data->collections.at(b).begin()->second.position;
            return a_p < b_p;
        });
    }

    const Manga& manga = *data;

    ImGui::Text("%s @ %s", manga.name.c_str(), manga.path.c_str());
    if (ImGui::Button("Copy path?")) {
        copy_to_clipboard(manga.path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) {
        show_in_explorer(manga.path, false);
        ImGui::CloseCurrentPopup();
    }

    ImGui::Text("%zu files, %zu chapters/volumes", 
        manga.images.size(),
        manga.collections.size()
    );

    if (ImGui::Button("Explore data")) {
        std::string json = glz::write<glz::opts{
            .prettify = true
        }>(manga).value_or("failed to write json");
        root.make_new_window(root.json_exp,
            std::format("json_exp/{}", header.path),
            AppJSONExplorer(root.get_root_dir(), std::move(json))
        );
    }

    if (ImGui::TreeNode("Content")) {
        for (const auto& repr : chapter_order) {
            for (const auto& [archive_path, coll] : manga.collections.at(repr)) {
                render_chapter_details(root, archive_path, coll);
            }
        }
        ImGui::TreePop();
    }

    if (manga.errs.items.size() && ImGui::TreeNode("Errors")) {
        if (!errors) {
            errors.emplace("", header.errs, std::vector<std::size_t>{});
        }
        errors->render_no_window(root);
        ImGui::TreePop();
    }
}

void AppMangaDetails::update(AppRoot& root) {
    if (!data) {
        data = root.file_index->get_info_for_manga(header.path);
    }

    if (data && pending_open) {
        if (auto loaded = root.file_index->get_file_info_for_manga(header.path)) {
            std::vector<File> files;
            std::vector<FileInfo> info;

            for (const std::size_t i : (*pending_open)->images) {
                files.emplace_back(data->images[i]);
                info.emplace_back(std::move((*loaded)[i]));
            }

            const auto key = (*pending_open)->position.repr();
            root.make_new_window(root.reader,
                std::format("reader/{}/{}", header.path, key),
                AppChapterReader{
                    .manga = header,
                    .files = files,
                    .info = info,
                    .title = std::format("{} - {}", key, header.name),
                }
            );
            pending_open.reset();
        }
    }
}

void AppMangaList::update(AppRoot& root) {
    if (selected) selected->update(root);
}


bool AppMangaList::render_no_window(AppRoot& root) {
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float width = window_size.x / (1 + selected.has_value());

    bool reload_library = false;

    if (ImGui::BeginChild("##list", {width, 0})) {
        search.render_no_window(root);
        ImGui::SameLine();
        ImGui::DragFloat("Accuracy", &min_score, 0.05, 0.0f, 100.0f, "%.1f%%");

        ImGui::SameLine();
        if (ImGui::Button("Reload from disk")) {
            reload_library = true;
        }

        error_filter.render_no_window(root, root.database.error_counts, true);

        std::size_t filtered_out = 0;
        for (std::size_t i = 0; i < all_manga->size(); ++i) {
            const auto res = search.at(i);
            const MangaHeader& manga = (*all_manga)[res.index];
            if (!error_filter.apply(manga.errs)) {
                ++filtered_out;
                continue;
            }

            if (0 <= res.score && res.score < min_score) {
                ++filtered_out;
                continue;
            }

            const std::string_view name = manga.name;

            assert(res.score == -1 || (0 <= res.score && res.score <= 100));
            if (res.score >= min_score) {
                ImGui::TextColored(get_gradient_interp((res.score - 50) / 50), "%3d", (int) (res.score + 0.5));
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted("% | ");
                ImGui::SameLine(0.0f, 0.0f);
            } else if (res.score >= 0) {
                ImGui::TextUnformatted("       ");
                ImGui::SameLine(0.0f, 0.0f);
            }

            const bool is_selected = selected && manga.path == selected->header.path;
            ImGui::PushID(i);
            {
                const ImVec2 start = ImGui::GetCursorPos();
                const ImVec2 area = ImGui::CalcTextSize(name.data(), name.data() + name.size());
                ImGui::InvisibleButton("##ml_line", {-1, area.y}, ImGuiButtonFlags_AllowOverlap);
                if (ImGui::IsItemClicked()) {
                    if (is_selected) {
                        selected = std::nullopt;
                    } else {
                        selected.emplace(AppMangaDetails{ manga });
                    }
                }

                if (!error_filter.present && !manga.errs.items.empty()) {
                    const ImVec2 min = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddRect(
                        min, {min.x + area.x, min.y + area.y}, 
                        ImColor(0xff, 0, 0)
                    );
                }
                ImGui::SetCursorPos(start);
            }
            ImGui::PopID();

            {
                if (res.score > 50) {
                    if (res.start != 0) {
                        highlight_text_block(name, 0, res.start, highlight_row_color, is_selected);
                        ImGui::SameLine(0.0f, 0.0f);
                    }
                    {
                        highlight_text_block(name, res.start, res.end, search_substr_color); 
                    }
                    if (res.end != name.size()) {
                        ImGui::SameLine(0.0f, 0.0f);
                        highlight_text_block(name, res.end, name.size(), highlight_row_color, is_selected);
                    }
                } else {
                    highlight_text_block(name, 0, name.size(), highlight_row_color, is_selected);
                }
            }
        }
        if (filtered_out != 0) {
            ImGui::TextDisabled("%zu filtered out (%zu results)", filtered_out, all_manga->size() - filtered_out);
        }
    }
    ImGui::EndChild();

    if (selected) {
        ImGui::SameLine();

        if (ImGui::BeginChild("##details", {width, 0})) {
            selected->render_no_window(root);
        }
        ImGui::EndChild();
    }

    if (reload_library) {
        root.database.reload();
        return false;
    }

    return true;
}

AppMangaList::AppMangaList(const std::vector<MangaHeader>& manga) : all_manga(&manga) {
    for (const MangaHeader& m : manga) {
        search.add_candidate(m.name);
    }
}
