#include "app.h"
#include "utils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>


void AppRoot::render(PendingAppUpdates& up) {
    try {
        logs.clear();
        log(" -- RENDER START -- ");
        const auto last = std::chrono::steady_clock::now();
        settings.start_new_frame();

        render_base_window(up);

        if (!database.is_finished()) {
            images = std::nullopt;
            file_index = std::nullopt;
            manga_list = std::nullopt;
            reader.clear();
            inspect.clear();
            errors.clear();
            json_exp.clear();
        } else {
            if (!images) images.emplace(here);
            if (!file_index) file_index.emplace(here);
        }
        
        const auto render_group = [this]<typename T>(std::map<std::string, T>& group) {
            for (auto it = group.begin(); it != group.end(); ) {
                if (!it->second.render(*this, it->first)) {
                    it = group.erase(it);
                } else {
                    ++it;
                }
            }
        };

        render_group(reader);
        render_group(inspect);
        render_group(errors);
        render_group(json_exp);

        if (show_index && file_index && !file_index->render(*this)) {
            show_index = false;
        }
        if (file_index) file_index->update_cache();

        if (show_cache && images && !images->render(*this)) {
            show_cache = false;
        }
        if (images) images->update_cache();

        if (show_metrics) ImGui::ShowMetricsWindow(&show_metrics);
        if (show_id_stack) ImGui::ShowIDStackToolWindow(&show_id_stack);

        if (show_style_editor) {
            if (ImGui::Begin("Style Editor", &show_style_editor)) {
                ImGui::ShowStyleEditor();
            }
            ImGui::End();
        }

        if (settings.has_pending_writes()) {
            ImGui::MarkIniSettingsDirty();
            settings.notify_saved();
        }

        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = now - last;
        log(" -- ELAPSED : {:.2f}ms -- ", elapsed.count());

        if (show_state) {
            const float line_height = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
            
            ImGui::SetNextWindowSizeConstraints({0, line_height * (25 + !logs.empty() + logs.size())}, {FLT_MAX, FLT_MAX});
            if (ImGui::Begin("State", &show_state, {})) {
                render_window_state(up);
            }   
            ImGui::End();
        }
    } catch (const std::exception& err) {
        dbg("caught exception in render loop body: {}", err.what());
    } catch (...) {
        dbg("caught unknown exception in render loop body");
    }
}


void AppRoot::render_base_window(PendingAppUpdates& up) {
    const auto cflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

    maximize_next_window();
    if (ImGui::Begin("root window", nullptr, cflags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (up.is_dragging || ImGui::IsWindowHovered())) {
                const ImVec2 dv = ImGui::GetIO().MouseDelta;
                up.x += (int) dv.x;
                up.y += (int) dv.y;
                up.is_dragging = true;
            } else {
                up.is_dragging = false;
            }

            if (ImGui::MenuItem("Close")) { up.close = true; }
            if (ImGui::MenuItem("Fullscreen")) { up.maximize = true; }
            if (ImGui::MenuItem("Minimize")) { up.minimize = true; }

            if (ImGui::BeginMenu("Tools")) {
                ImGui::MenuItem("Metric Window", NULL, &show_metrics);
                ImGui::MenuItem("ID Stack", NULL, &show_id_stack);
                ImGui::MenuItem("Image Cache", NULL, &show_cache);
                ImGui::MenuItem("File Index", NULL, &show_index);
                ImGui::MenuItem("Style Editor", NULL, &show_style_editor);
                ImGui::MenuItem("Show State", NULL, &show_state);
                ImGui::EndMenu();
            }

            const float item_width = ImGui::CalcTextSize("X").x + 2.0f * ImGui::GetStyle().FramePadding.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - item_width);
            if (ImGui::MenuItem("X")) {
                up.close = true;
            }

            ImGui::EndMenuBar();
        }

        const ImVec2 window_size = ImGui::GetWindowSize();
        up.w = (int) window_size.x;
        up.h = (int) window_size.y;

        if (!database.is_finished()) {
            database.render_no_window(*this);
        } else {
            if (!manga_list) {
                manga_list.emplace(database.manga);
            }
            manga_list->update(*this);
            if (!manga_list->render_no_window(*this)) {
                manga_list = std::nullopt;
            }
        }

    }
    ImGui::End();
}

void AppRoot::render_window_state(PendingAppUpdates& up) {
    const std::string info = std::format(
        "GLFWwindow* window = {};\n"
        "AppData state = {{\n"
        "    .x = {}, .y = {},\n"
        "    .w = {}, .h = {},\n"
        "    .maximized = {},\n"
        "    .minimized = {},\n"
        "    .is_dragging = {}\n"
        "}};",
        (void*) window, 
        state.x, state.y, 
        state.w, state.h, 
        state.maximized, state.minimized, 
        state.is_dragging
    );
    ImGui::TextUnformatted(info.c_str(), info.c_str() + info.size());

    {
        ImGui::SeparatorText("children");
        ImGui::Text("database { .loaded = %s, .length = %zu }",
                    database.is_finished() ? "true" : "false",
                    database.loaded);

        if (images) {
            ImGui::Text("image cache { .size = %zu, .archives = %zu (%zu open) }",
                        images->cache_size(), images->total_archives(), 
                        images->opened_archives());
        }

        if (file_index) {
            ImGui::Text("file index { }");
        }

        if (manga_list) {
            const std::string repr = manga_list->selected ? 
                std::format("{{ .selected = {} }}", manga_list->selected->header.name) :
                std::format("{{ .selected = <std::nullopt> }}");
            ImGui::Text("manga list %s", repr.c_str());
        }
        for (const auto& [id, win] : reader) {
            const std::string repr = std::format("{{ manga = {}, .pages = {} }}", 
                win.manga.name, win.files.size()
            );
            ImGui::Text("reader %s", repr.c_str());
        }
        for (const auto& [id, win] : inspect) {
            const std::string repr = std::format("{{ .uv = [{:.2f}, {:.2f}]x[{:.2f}, {:.2f}], .file = \"{}\" }}",
                win.u0, win.u1,
                win.v0, win.v1,
                win.file.filename
            );
            ImGui::Text("image inspect %s", repr.c_str());
        }
        for (const auto& [id, win] : errors) {
            ImGui::Text("errors { .title = %s }", win.title.c_str());
        }
        for (const auto& [id, win] : json_exp) {
            const std::string repr = std::format("{{ .compact = {}, .wrap = {}, .pending = {} }}",
                win.compact, win.wrap, win.has_update
            );
            ImGui::Text("json %s", repr.c_str());
        }
    }

    if (show_cache || show_metrics || show_id_stack || show_style_editor || show_state) {
        ImGui::SeparatorText("debug windows");
        if (show_cache) ImGui::TextUnformatted("image cache");
        if (show_index) ImGui::TextUnformatted("file index");
        if (show_metrics) ImGui::TextUnformatted("metrics");
        if (show_id_stack) ImGui::TextUnformatted("id stack");
        if (show_style_editor) ImGui::TextUnformatted("style editor");
        if (show_state) ImGui::TextUnformatted("app state");
    }

    std::size_t pending_lines = 0;
    if (up.x != state.x || up.y != state.y || 
        up.w != state.w || up.h != state.h ||
        up.minimize || up.maximize) {
        ImGui::SeparatorText("pending events");
        pending_lines += 1;

        if (up.x != state.x || up.y != state.y) {
            ImGui::Text("pos: (%d, %d) -> (%d, %d)", 
                        state.x, state.y, up.x, up.y);
            pending_lines += 1;
        }
        if (up.w != state.w || up.h != state.h) {
            ImGui::Text("dim: (%d, %d) -> (%d, %d)",
                        state.w, state.h, up.w, up.h);
            pending_lines += 1;
        }
        if (up.maximize) {
            ImGui::TextUnformatted("toggle maximize");
            pending_lines += 1;
        }
        if (up.minimize) {
            ImGui::TextUnformatted("toggle minimize");
            pending_lines += 1;
        }
    }
    while (pending_lines--) {
        ImGui::NewLine();
    }
    
    if (!logs.empty()) {
        ImGui::SeparatorText("Logs:");
        for (const std::string& line : logs) {
            ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
        }
    }
}
