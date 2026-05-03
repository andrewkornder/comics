#include "../app.h"
#include "../utils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <filesystem>

constexpr auto wflags = ImGuiWindowFlags_NoSavedSettings;
constexpr auto no_scrollbars_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

float AppChapterReader::PagedDisplay::width = 1.3333;
bool AppChapterReader::PagedDisplay::ltr = false;

bool AppChapterReader::render(AppRoot& root, std::string_view id) { 
    const std::string wtitle = std::format("{}###reader-{}", title, id);
    bool tmp_open = true;

    if (ImGui::Begin(wtitle.c_str(), &tmp_open, wflags | no_scrollbars_flags)) {
        set_window_size_by_settings(root.settings, "Reader/size");
        tmp_open &= render_no_window(root);
        save_window_size_to_settings(root.settings, "Reader/size", !tmp_open);
    }
    ImGui::End();
    return tmp_open;
}

void AppChapterReader::jump_to(std::size_t page) {
    if (dis.index == 0) {
        float progress = 0;
        for (std::size_t i = 0; i < page; ++i) {
            progress += (float) info[i].height / info[i].width;
        }
        dis.scroll.progress = progress;
    } else if (dis.index == 1) {
        dis.paged.page = page;
    }
}

void AppChapterReader::jump_to(float p) {
    if (dis.index == 0) {
        dis.scroll.progress = p;
    } else if (dis.index == 1) {
        float progress = 0;
        for (std::size_t i = 0; i < info.size(); ++i) {
            progress += (float) info[i].height / info[i].width;
            if (progress >= p || i == info.size() - 1) {
                dis.paged.page = i;
                break;
            }
        }
    }
}


bool AppChapterReader::render_no_window(AppRoot& root) {
    if (info.empty()) {
        ImGui::Text("No pages found");
        return true;
    }

    static int jump_destination = -1;

    bool new_index = ImGui::RadioButton("Scroll", &dis.index, 0); ImGui::SameLine();
    new_index |= ImGui::RadioButton("Pages", &dis.index, 1);

    if (new_index) {
        if (dis.index == 0) {
            if (dis.paged.page > 0) jump_to((std::size_t) dis.paged.page);
            else jump_to((std::size_t) 0);
        } else if (dis.index == 1) {
            jump_to(dis.scroll.progress);
        }
    }

    if (dis.index == 0) {
        if (render_page_counter(dis.scroll.visible.first + 1, dis.scroll.progress / dis.scroll.max_scroll)) {
            jump_destination = dis.scroll.visible.first + 1;
            ImGui::OpenPopup("##page_jump_popup");
        }
    } else if (dis.index == 1) {
        std::size_t page = dis.paged.page < 0 ? 1 : dis.paged.page + 1;
        if (render_page_counter(page, (float) (page - 1) / (info.size() - 1))) {
            jump_destination = page;
            ImGui::OpenPopup("##page_jump_popup");
        }
    }

    if (ImGui::BeginPopup("##page_jump_popup")) {
        ImGui::InputInt("##jump_input", &jump_destination);
        ImGui::BeginDisabled(jump_destination < 1 || jump_destination > (int) info.size());
        if (ImGui::SameLine(), ImGui::Button("Jump")) {
            jump_to((std::size_t) jump_destination - 1);
            jump_destination = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }
    
    if (ImGui::BeginChild("##pages", {-1, -1}, {}, no_scrollbars_flags)) {
        if (dis.index == 0) render_pages(root, dis.scroll);
        else if (dis.index == 1) render_pages(root, dis.paged);
    }
    ImGui::EndChild();
    return true;
}

bool AppChapterReader::render_page_counter(const std::size_t page, float percentage) {
    const std::string index_d = std::format("{}", page);
    const std::string page_d = std::format(" / {} - {:.0f}%", 
        info.size(), 
        100.0f * percentage
    );
    const ImVec2 index_size = ImGui::CalcTextSize(
        index_d.c_str(),
        index_d.c_str() + index_d.size()
    );
    const ImVec2 page_size = ImGui::CalcTextSize(
        page_d.c_str(),
        page_d.c_str() + page_d.size()
    );
    
    const float display_width = index_size.x + page_size.x + ImGui::GetStyle().FramePadding.x * 2;
    
    const float offset = (ImGui::GetContentRegionAvail().x - display_width) / 2.0f;
    if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

    const bool ret = ImGui::Button((index_d + "###page_select").c_str());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(
        page_d.c_str(),
        page_d.c_str() + page_d.size()
    );
    return ret;
}


void AppChapterReader::render_pages(AppRoot& root, PagedDisplay& dis) {
    ImGui::Checkbox("LTR", &dis.ltr); 
    ImGui::SameLine();

    ImGui::BeginDisabled(false && dis.page == 0);
    if (ImGui::Button("Decrement offset")) {
        --dis.page;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(dis.page >= (std::ptrdiff_t) info.size() - 1);
    if (ImGui::Button("Increment offset")) {
        ++dis.page;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::Text("Aspect Ratio:"); 
    ImGui::SameLine();
    ImGui::DragFloat("##aspect_input", &dis.width, 0.1, 0, 4, "%.3f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::BeginChild("##paged_cnt", {-1, -1})) {
        constexpr static float default_aspect = 2.0f / 3.0f;
        constexpr static float arrow_width = 25;
        auto [width, height] = ImGui::GetContentRegionAvail();  
        width -= 2 * arrow_width;

        ImVec2 viewport;

        std::ptrdiff_t next_page;
        std::size_t page_count = 1;
        {
            float cost = 0;
            for (std::ptrdiff_t i = dis.page; i < (std::ptrdiff_t) info.size(); ++i) {
                if (i < 0) {
                    cost += default_aspect;
                } else {
                    cost += (float) info[i].width / info[i].height;
                }
                if (cost > dis.width && i < 0) {
                    ++dis.page;
                    cost -= default_aspect;
                    continue;
                }
                if (cost > dis.width) {
                    page_count = i - dis.page + 1;
                    break;
                }
            }
            next_page = dis.page + page_count;
            viewport = {width, width / cost};
            if (viewport.y > height) {
                viewport = { viewport.x / viewport.y * height, height };
            }
        }

        std::ptrdiff_t prev_page;
        if (dis.page >= 0) {
            float cost = 0;
            for (std::ptrdiff_t i = dis.page - 1; ; --i) {
                if (i < 0) {
                    cost += default_aspect;
                } else {
                    cost += (float) info[i].width / info[i].height;
                }
                if (cost > dis.width) {
                    prev_page = i;
                    break;
                }
            }
        } else {
            prev_page = dis.page;
        }

        std::pair<std::ptrdiff_t, std::ptrdiff_t> to_load = std::make_pair( 
            dis.page < 3 ? 0 : dis.page - 3,
            next_page + 3
        );
        if (to_load.second - to_load.first > 10) {
            to_load.second = to_load.first + 10;
        }
        if (to_load.second > (std::ptrdiff_t) info.size()) to_load.second = info.size();

        for (std::ptrdiff_t i = to_load.first; i < to_load.second; ++i) {
            if (i >= 0) root.images->get_image_id(files[i], info[i]);
        }

        std::ptrdiff_t a = dis.page, b = next_page, inc = 1;
        if (!dis.ltr) {
            std::swap(a, b);
            --a; --b;
            inc = -1;
        }

        const bool show_next_button = next_page < (std::ptrdiff_t) info.size();
        const bool show_prev_button = dis.page > 0;

        if (width > viewport.x) {
            ImGui::Indent((width - viewport.x) / 2);
        }

        if (dis.ltr ? show_prev_button : show_next_button) {
            if (ImGui::Button("##left_arrow", {arrow_width, viewport.y})) {
                dis.page = dis.ltr ? prev_page : next_page;
            }
            ImGui::SameLine(0.0f, 0.0f);
        } else {
            ImGui::Indent(arrow_width);
        }

        for (std::ptrdiff_t i = a; i != b; i += inc) {
            if (i < 0) {
                ImGui::Indent(viewport.y * default_aspect);
            } else {
                const float w = viewport.y * info[i].width / info[i].height;
                render_single_page(root, i, w, 0, 0, 1, 1);
                ImGui::SameLine(0.0f, 0.0f);
            }
        }
        if (dis.ltr ? show_next_button : show_prev_button) {
            if (ImGui::Button("##right_arrow", {arrow_width, viewport.y})) {
                dis.page = dis.ltr ? next_page : prev_page;
            }
            ImGui::SameLine(0.0f, 0.0f);
        } else {
            ImGui::Indent(arrow_width);
        }
    }
    ImGui::EndChild();
}


void AppChapterReader::render_pages(AppRoot& root, ScrollingDisplay& dis) {
    const auto [width, height] = ImGui::GetContentRegionAvail();

    {
        float distance = 0;
        const float bottom = dis.progress + height / width;
        bool found_first = false, found_last = false;
        for (std::size_t i = 0; i < info.size(); ++i) {
            const float aspect = (float) info[i].height / info[i].width;
            distance += aspect;
            if (!found_first && distance > dis.progress) {
                dis.visible.first = i;
                dis.offsets.first = 1 - (distance - dis.progress) / aspect;
                found_first = true;
            }
            if (!found_last && (distance > bottom || i == info.size() - 1)) {
                dis.visible.second = i + 1;
                dis.offsets.second = distance > bottom ? 1 - (distance - bottom) / aspect : 1;
                found_last = true;
            }
        }

        dis.max_scroll = distance - height / width;
        dis.total_distance = distance;

        root.log("Reader viewport: {}-{} ({:.2f}, {:.2f})",
            dis.visible.first,
            dis.visible.second,
            dis.offsets.first,
            dis.offsets.second
        );
        root.log("Reader progress: {} < [{}, {}]", dis.progress, 0.0f, dis.max_scroll);
    }

    std::pair<std::size_t, std::size_t> to_load = std::make_pair( 
        dis.visible.first < 3 ? 0 : dis.visible.first - 3,
        dis.visible.second >= info.size() - 3 ? info.size() - 1 : dis.visible.second + 3
    );
    if (to_load.second - to_load.first > 10) {
        to_load.second = to_load.first + 10;
    }

    for (std::size_t i = to_load.first; i < to_load.second; ++i) {
        root.images->get_image_id(files[i], info[i]);
    }

    {
        const auto cursor = ImGui::GetCursorPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##page_inv_btn", {width, height});

        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem)) {
            if (auto ds = ImGui::GetIO().MouseWheel) {
                dis.progress -= ds / 3;
            }
        }

        dis.progress = std::clamp(dis.progress, 0.0f, dis.max_scroll);
        ImGui::SetCursorPos(cursor);
    }
    for (std::size_t i = dis.visible.first; i < dis.visible.second; ++i) {
        const ImVec2 cursor = ImGui::GetCursorPos();

        const float u0 = 0, u1 = 1;
        float v0 = 0, v1 = 1;
        if (i == dis.visible.first) {
            v0 = dis.offsets.first;
        }
        if (i == dis.visible.second - 1) {
            v1 = dis.offsets.second;
        }
        const auto dim = render_single_page(root, i, width, u0, v0, u1, v1);
        if (i != dis.visible.second - 1) {
            ImGui::SetCursorPos({cursor.x, cursor.y + dim.second});
        }
    }
}

std::pair<float, float> AppChapterReader::render_single_page(AppRoot& root, const std::size_t i, float width, float u0, float v0, float u1, float v1) {
    ImGui::PushID(i);

    const File& file = files[i];
    const FileInfo& file_info = info[i];

    const auto [loaded, id] = root.images->get_image_id(file, file_info);

    const ImVec2 cursor = ImGui::GetCursorPos();

    const ImVec2 dim = { width, (v1 - v0) * width * file_info.height / file_info.width };
    ImGui::InvisibleButton("##canv", dim, ImGuiButtonFlags_AllowOverlap);
    ImGui::SetCursorPos(cursor);

    if (ImGui::BeginPopupContextItem()) {
        const std::string repr = file.pages ? file.pages->repr() : "<unknown page>";

        ImGui::TextUnformatted(repr.c_str());
        if (file_info.width != (std::size_t) -1) {
            ImGui::Text("%zux%zu, %td bands, %.2f:1 (%s)", 
                file_info.width, file_info.height, file_info.bands,
                (float) file_info.width / file_info.height,
                bytes_to_si_prefix(file.file_size).c_str()
            );
        }
        ImGui::Text("File: %s", file.filename.c_str());
        ImGui::Text("Archive: %s", file.archive_path.c_str());
        ImGui::Text("Manga: %s", manga.name.c_str());

        if (ImGui::Button("Copy archive path?")) {
            copy_to_clipboard(file.archive_path);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::SameLine(), ImGui::Button("Copy name?")) {
            copy_to_clipboard(file.filename);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::SameLine(), ImGui::Button("Copy combined?")) {
            const auto path = std::filesystem::path(file.archive_path) / file.filename;
            copy_to_clipboard(path.string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::BeginDisabled(!loaded);
        if (ImGui::Button("Copy image?")) {
            root.images->copy_image(file);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        if (!file.errors.empty() && ImGui::Button("Show errors?")) {
            root.make_new_window(root.errors, 
                std::format("errors/{}/{}", file.archive_path, file.filename),
                AppErrorList(file.filename, manga.errs, file.errors)
            );
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemClicked()) {
        root.make_new_window(root.inspect, 
            std::format("image_inspect/{}/{}", file.archive_path, file.filename),
            AppImageInspect{ manga, File(file), FileInfo(file_info) }
        );
    }
    if (!file.errors.empty() && ImGui::BeginItemTooltip()) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Image has %zu errors", file.errors.size());
        ImGui::EndTooltip();
    }

    ImGui::Image(id, dim, {u0, v0}, {u1, v1});
    ImGui::PopID();

    return {dim.x, dim.y};
}
