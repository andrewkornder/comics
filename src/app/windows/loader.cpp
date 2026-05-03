#include "../app.h"
#include "../utils.h"
#include "../colors.h"

#include "../../types_glz.h"

#include <imgui/imgui.h>
#include <zip.h>
#include <glaze/glaze.hpp>

#include <algorithm>
#include <optional>
#include <thread>
#include <vector>


AppMangaLoader::AppMangaLoader(std::string_view here) {
    std::filesystem::path where = here;
    where = where / MANGA_DB_PATH;
    path = where.string();

    reload();
}

void AppMangaLoader::reload() {
    loading = true;
    loaded = 0;
    total = 0;
    manga.clear();

    loader = std::jthread([this](std::stop_token token) {
        std::vector<glz::raw_json_view> unparsed;
        std::string buf;
        const auto err = glz::read_file_json(unparsed, path.data(), buf);
        if (err) {
            dbg(std::cerr, "failed to read data: {}", path);
            dbg(std::cerr, "{}", glz::format_error(err, buf));
        } else {
            total = unparsed.size();
            dbg("read {} of data: {}", bytes_to_si_prefix(err.count), path);

            for (std::size_t i = 0; i < unparsed.size(); ++i) {
                if (token.stop_requested()) break;
                
                auto ec = glz::read_json<MangaHeader>(unparsed[i].str);
                if (ec) {
                    manga.emplace_back(std::move(ec).value());
                    ++loaded;
                } else {
                    auto e = ec.error();

                    dbg(std::cerr, "manga entry #{} of {} did not parse", i, path);
                    dbg(std::cerr, "{}", glz::format_error(e, buf));
                    total -= 1;
                }
            }
        }

        std::ranges::sort(manga, std::less{}, &MangaHeader::name); 
        error_counts.clear();
        for (const auto& m : manga) {
            for (const auto& [key, cnt] : m.errs.counts) {
                error_counts[key] += cnt;
            }
        }
        loading = false;
    });
}

AppMangaLoader::~AppMangaLoader() {
    if (loader && loader->joinable()) {
        loader->request_stop();
        loader->join();
    }
}

bool AppMangaLoader::render_no_window(AppRoot&) {
    const float p = (float) loaded / total;
    const std::string text = std::format("{} / {}", loaded, total);

    if (ImGui::Button("Interrupt")) {
        loader->request_stop();
    }
    if (!loading) {
        loader->join();
        loader = std::nullopt;
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, get_gradient_interp(p));
    ImGui::ProgressBar(p, ImVec2(0, 0), text.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Adding manga from database...");
    return true;
}

bool AppMangaLoader::is_finished() {
    return !loader;
}
