#include "state.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <ranges>
#include <regex>

#include <imgui.h>
#include <imgui_demo.cpp>
#include <imgui_zoomable_image/imgui_zoomable_image.h>

#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>

#include <rapidfuzz/fuzz.hpp>


struct SanitizedText {
    std::string clean;
    std::vector<std::size_t> indices;

    explicit SanitizedText(std::string_view text = "") {
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = std::tolower(text[i]);
            const bool ws = std::isspace(c);
            if (i > 0 && ws && std::isspace(text[i - 1])) {
                continue;
            }
            if (!ws && !std::isalnum(c)) {
                continue;
            }

            indices.push_back(i);
            clean.push_back(c);
        }
        indices.push_back(text.size());
    }
};


class Filterer {
    std::array<char, 256> text;
    SanitizedText filt {};

    std::vector<SanitizedText> candidates;

    using result_t = rapidfuzz::ScoreAlignment<double>;
    std::vector<std::pair<std::size_t, std::optional<result_t>>> results;

    void extract() {
        results.clear();
        if (!text[0]) {
            std::println("Clearing filter");
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                results.emplace_back(i, std::nullopt);
            }
            return;
        }

        std::println("Sorting entries by filter: \"{}\"", filt.clean);

        for (std::size_t i = 0; const auto& choice : candidates) {
            const result_t res = rapidfuzz::fuzz::partial_ratio_alignment(filt.clean, choice.clean);
            const result_t mapped( 
                res.score,
                filt.indices[res.src_start],
                filt.indices[res.src_end],
                choice.indices[res.dest_start],
                choice.indices[res.dest_end]
            );

            results.emplace_back(i, std::optional(mapped));
            ++i;
        }
        
        std::ranges::sort(results, std::greater{}, [](const auto& res) {
            assert(res.second);
            return res.second->score;
        });

        std::println("Best match: \"{}\" -> score={:.2f}", 
                candidates[results[0].first].clean,
                results[0].second->score);
    }
    
public:
    bool ensure_loaded(const std::vector<Manga>& all_manga) {
        if (candidates.size() == all_manga.size()) return false;

        for (std::size_t i = candidates.size(); i < all_manga.size(); ++i) {
            candidates.emplace_back(all_manga[i].name);
        }
        return true;
    }

    void render(bool force_reload) {
        if (ImGui::InputText("##search", text.data(), text.size()) || force_reload) {
            filt = SanitizedText(text.data());
            extract();
        }
    }

    const auto& get() {
        return results;
    }
};

State::State() {
    filter = std::make_unique<Filterer>();
}
State::~State() {
    if (manga_parser.joinable()) {
        manga_parser.request_stop();
        manga_parser.join();
    }
}


int State::load(std::string_view path) {
    manga_source_path = path;
    manga_parser = std::jthread([this](std::stop_token token) {
        using clock = std::chrono::steady_clock;
        namespace chr = std::chrono;

        std::vector<glz::raw_json_view> unparsed;
        std::string buffer;

        const auto start = clock::now();
        const auto ec = glz::read_file_json(unparsed, manga_source_path, buffer);
        const auto elapsed = chr::duration_cast<chr::duration<double>>(clock::now() - start);
        if (ec) {
            std::println(std::cerr, "Failed to read data @ \"{}\" in {:.2f}s", manga_source_path, elapsed.count());
            std::println(std::cerr, "{}", glz::format_error(ec, buffer));
            return;
        }
        std::println("Read {:.2f} MiB of data in {:.2f}s", ec.count / (double) (1 << 20), elapsed.count());

        std::vector<Manga> block;
        for (std::size_t i = 0; i < unparsed.size(); ++i) {
            if (token.stop_requested()) break;
            
            auto ec = glz::read_json<Manga>(unparsed[i].str);
            if (ec) {
                block.emplace_back(std::move(ec).value());
            } else {
                std::println(std::cerr, "Manga entry #{} did not parse", i);
                std::println(std::cerr, "{}", glz::format_error(ec.error(), unparsed[i].str));
            }
            if (block.size() == 16) {
                manga_mutex.lock();
                for (Manga& m : block) newly_parsed_manga.emplace_back(std::move(m));
                manga_mutex.unlock();

                has_new_manga = true;
                block.clear();
            }
        }

        if (!block.empty()) {
            manga_mutex.lock();
            for (Manga& m : block) newly_parsed_manga.emplace_back(std::move(m));
            manga_mutex.unlock();

            has_new_manga = true;
        }
    });
    return 0;
}

void center_next_window(float w, float h) {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowSize({ w, h });
    ImGui::SetNextWindowPos({ (screen.x - w) / 2.f, (screen.y - h) / 2.f });
}


void State::render() {
    auto& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowPos({});
    if (ImGui::Begin("root window", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const float width = io.DisplaySize.x / (1 + selected_manga.has_value());
        if (ImGui::BeginChild("manga list", {width, 0})) {
            render_manga_list();
        }
        ImGui::EndChild();

        if (selected_manga) {
            ImGui::SameLine();

            open_chapter = false;
            open_inspect = false;
        
            if (ImGui::BeginChild("manga details", {width, 0})) {
                render_manga_details();
            }

            const ImVec2 screen = ImGui::GetIO().DisplaySize;

            if (open_chapter) {
                std::println("opening chapter: {}", *reading);
                ImGui::OpenPopup("reader");
            }
            if (center_next_window(screen.x * 0.5f, screen.y * 0.95f), ImGui::BeginPopup("reader")) {
                if (!render_manga_chapter()) {
                    reading = std::nullopt;
                    ImGui::SetScrollY(0.0f);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            ImGui::EndChild();
        }

        // constexpr auto wflags = ImGuiWindowFlags_NoTitleBar;
        // ImGui::Begin("##reader", nullptr, wflags);
    }
    ImGui::End();
}


const std::uint32_t search_substr_color = 0x5A0582;
const std::uint32_t highlight_row_color = 0x046a9e;

const std::array<std::uint32_t, 256> heat_cmap = { 0x14bae7, 0x19bae7, 0x1dbae7, 0x20bae7, 0x24bae7, 0x26bae7, 0x29b9e7, 0x2cb9e8, 0x2eb9e8, 0x31b9e8, 0x33b9e8, 0x35b9e8, 0x37b8e8, 0x39b8e8, 0x3bb8e8, 0x3db8e9, 0x3eb8e9, 0x40b8e9, 0x42b8e9, 0x44b7e9, 0x45b7e9, 0x47b7e9, 0x48b7e9, 0x4ab7ea, 0x4bb7ea, 0x4db6ea, 0x4eb6ea, 0x50b6ea, 0x51b6ea, 0x52b6ea, 0x54b6ea, 0x55b5ea, 0x56b5eb, 0x58b5eb, 0x59b5eb, 0x5ab5eb, 0x5bb5eb, 0x5db4eb, 0x5eb4eb, 0x5fb4eb, 0x60b4eb, 0x61b4eb, 0x63b4eb, 0x64b3ec, 0x65b3ec, 0x66b3ec, 0x67b3ec, 0x68b3ec, 0x69b2ec, 0x6ab2ec, 0x6bb2ec, 0x6db2ec, 0x6eb2ec, 0x6fb2ec, 0x70b1ec, 0x71b1ec, 0x72b1ec, 0x73b1ed, 0x74b1ed, 0x75b0ed, 0x76b0ed, 0x77b0ed, 0x78b0ed, 0x79b0ed, 0x7ab0ed, 0x7bafed, 0x7cafed, 0x7dafed, 0x7eafed, 0x7fafed, 0x80aeed, 0x81aeed, 0x82aeed, 0x83aeed, 0x84aeed, 0x85aded, 0x85aded, 0x86aded, 0x87aded, 0x88aded, 0x89aced, 0x8aaced, 0x8baced, 0x8caced, 0x8daced, 0x8eabed, 0x8fabed, 0x90abed, 0x91abed, 0x91abed, 0x92aaed, 0x93aaed, 0x94aaed, 0x95aaec, 0x96a9ec, 0x97a9ec, 0x98a9ec, 0x99a9ec, 0x9aa9ec, 0x9aa8ec, 0x9ba8ec, 0x9ca8ec, 0x9da8ec, 0x9ea7eb, 0x9fa7eb, 0xa0a7eb, 0xa1a7eb, 0xa2a7eb, 0xa2a6eb, 0xa3a6eb, 0xa4a6ea, 0xa5a6ea, 0xa6a5ea, 0xa7a5ea, 0xa8a5ea, 0xa8a5e9, 0xa9a5e9, 0xaaa4e9, 0xaba4e9, 0xaca4e8, 0xada4e8, 0xaea3e8, 0xaea3e8, 0xafa3e7, 0xb0a3e7, 0xb1a3e7, 0xb2a2e6, 0xb2a2e6, 0xb3a2e6, 0xb4a2e5, 0xb5a1e5, 0xb6a1e5, 0xb6a1e4, 0xb7a1e4, 0xb8a1e4, 0xb9a0e3, 0xbaa0e3, 0xbaa0e2, 0xbba0e2, 0xbc9fe2, 0xbd9fe1, 0xbd9fe1, 0xbe9fe1, 0xbf9fe0, 0xc09ee0, 0xc09edf, 0xc19edf, 0xc29edf, 0xc39dde, 0xc39dde, 0xc49ddd, 0xc59ddd, 0xc59ddd, 0xc69cdc, 0xc79cdc, 0xc79cdb, 0xc89cdb, 0xc99cdb, 0xca9bda, 0xca9bda, 0xcb9bd9, 0xcc9bd9, 0xcc9ad8, 0xcd9ad8, 0xce9ad8, 0xce9ad7, 0xcf9ad7, 0xd099d6, 0xd099d6, 0xd199d5, 0xd199d5, 0xd298d5, 0xd398d4, 0xd398d4, 0xd498d3, 0xd598d3, 0xd597d2, 0xd697d2, 0xd697d2, 0xd797d1, 0xd897d1, 0xd896d0, 0xd996d0, 0xd996cf, 0xda96cf, 0xdb95ce, 0xdb95ce, 0xdc95cd, 0xdc95cd, 0xdd95cd, 0xde94cc, 0xde94cc, 0xdf94cb, 0xdf94cb, 0xe093ca, 0xe093ca, 0xe193c9, 0xe293c9, 0xe293c8, 0xe392c8, 0xe392c8, 0xe492c7, 0xe492c7, 0xe591c6, 0xe591c6, 0xe691c5, 0xe791c5, 0xe791c4, 0xe890c4, 0xe890c3, 0xe990c3, 0xe990c2, 0xea8fc2, 0xea8fc2, 0xeb8fc1, 0xeb8fc1, 0xec8fc0, 0xec8ec0, 0xed8ebf, 0xed8ebf, 0xee8ebe, 0xee8dbe, 0xef8dbd, 0xef8dbd, 0xf08dbc, 0xf08cbc, 0xf18cbb, 0xf18cbb, 0xf28cba, 0xf28cba, 0xf38bba, 0xf38bb9, 0xf48bb9, 0xf48bb8, 0xf58ab8, 0xf58ab7, 0xf68ab7, 0xf68ab6, 0xf789b6, 0xf789b5, 0xf889b5, 0xf889b4, 0xf989b4, 0xf988b3, 0xf988b3, 0xfa88b2, 0xfa88b2, 0xfb87b1, 0xfb87b1, 0xfc87b0, 0xfc87b0, 0xfd86b0, 0xfd86af, 0xfe86af, 0xfe86ae, 0xff85ae };

ImVec4 to_color(std::uint32_t rgb) {
    return ImVec4(
        (rgb >> 16 & 0xff) / 255.f,
        (rgb >>  8 & 0xff) / 255.f,
        (rgb & 0xff) / 255.f,
        1.0f
    );
}


void highlight_text_block(const std::string_view text, std::size_t start, std::size_t end, std::uint32_t color, bool enabled = true) {
    const char* ptr = text.data();
    
    end = end < text.size() ? end : text.size();
    if (start >= end) {
        return;
    }

    if (enabled) {
        ImVec2 extent = ImGui::CalcTextSize(ptr + start, ptr + end);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + extent.x, pos.y + extent.y), ImColor(to_color(color)));
    }
    ImGui::TextUnformatted(ptr + start, ptr + end);
}


void State::render_manga_list() {
    if (has_new_manga) {
        manga_mutex.lock();
        for (Manga& m : newly_parsed_manga) all_manga.emplace_back(std::move(m));
        newly_parsed_manga.clear();
        manga_mutex.unlock();

        has_new_manga = false;
    }

    bool new_filter = filter->ensure_loaded(all_manga);
    filter->render(new_filter);

    for (const auto& [i, res] : filter->get()) {
        const std::string& name = all_manga[i].name;

        bool clicked = false;
        if (res) {
            const std::size_t start = res->dest_start, end = res->dest_end;
            
            assert(0 <= res->score && res->score <= 100);
            if (res->score > 50) {
                const double interp = (res->score - 50) / 50 * (heat_cmap.size() - 1);
                const std::size_t c_i = heat_cmap.size() - 1 - (std::uint32_t) interp;

                ImGui::TextColored(to_color(heat_cmap[c_i]), "%3d", (int) (res->score + 0.5));
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted("% | ");
            } else {
                //                     "100% | "
                ImGui::TextUnformatted("       ");
            }

            if (start != 0) {
                ImGui::SameLine(0.0f, 0.0f);
                highlight_text_block(name, 0,     start,       highlight_row_color, i == selected_manga);
                clicked |= ImGui::IsItemClicked();
            }
            {
                ImGui::SameLine(0.0f, 0.0f);
                highlight_text_block(name, start, end,         search_substr_color); 
                clicked |= ImGui::IsItemClicked();
            }
            if (end != name.size()) {
                ImGui::SameLine(0.0f, 0.0f);
                highlight_text_block(name, end,   name.size(), highlight_row_color, i == selected_manga);
                clicked |= ImGui::IsItemClicked();
            }
        } else {
            highlight_text_block(name, 0, name.size(), highlight_row_color, i == selected_manga);
            clicked |= ImGui::IsItemClicked();
        }

        if (clicked) {
            if (i != selected_manga) {
                std::println("Selected manga at index {}: \"{}\"", i, name);
                selected_manga = i;
            } else {
                std::println("Deselected manga at index {}: \"{}\"", i, name);
                selected_manga = std::nullopt;
            }
        }
    }
}


template<typename T>
std::string repr_index(const T& i) {
    if constexpr (std::is_same_v<T, Chapter>) {
        std::string base = std::format("ch {}", i.number);
        if (i.extra_number) {
            base += std::format(".{}", *i.extra_number);
        }
        if (i.sub_part) {
            base += std::format("#{}", *i.sub_part);
        }
        return base;
    } else if constexpr (std::is_same_v<T, Volume>) {
        return i.part ? std::format("v{}#{}", i.number, *i.part) :
                        std::format("v{}", i.number);
    } else if constexpr (std::is_same_v<T, Page>) {
        return i.part ? std::format("p{}#{}", i.number, *i.part) :
                        std::format("p{}", i.number);
    } else {
        static_assert(false);
    }
}
template<typename T>
std::string repr_range(const Range<T>& indices) {
    const auto& [cstart, cend] = indices;
    const std::string start = repr_index(cstart), end = repr_index(cend);
    return start != end ? std::format("{}-{}", start, end) : start;
}

void State::render_manga_details() {
    const std::size_t m_i = *selected_manga;
    const auto& manga = all_manga[m_i];

    ImGui::Text("%zu files, %zu chapters, %zu volumes", 
        manga.files.size(),
        manga.chapters.size(),
        manga.volumes.size()
    );

    if (ImGui::TreeNode("Content")) {
        std::size_t c_i = 0;
        for (const auto& coll : manga.volumes) {
            const std::tuple ident = std::make_tuple(m_i, c_i);
            const std::string repr = repr_range(coll.indices);
            highlight_text_block(std::format("{} [{} pages]", repr, coll.page_indices.size()),
                                 0, (std::size_t) -1, highlight_row_color, ident == reading);
            if (ImGui::IsItemClicked()) { reading = ident; open_chapter = true; }
            ++c_i;
        }
        for (const auto& coll : manga.chapters) {
            const std::tuple ident = std::make_tuple(m_i, c_i);
            const std::string repr = repr_range(coll.indices);
            highlight_text_block(std::format("{} [{} pages]", repr, coll.page_indices.size()),
                                 0, (std::size_t) -1, highlight_row_color, ident == reading);
            if (ImGui::IsItemClicked()) { reading = ident; open_chapter = true; }
            ++c_i;
        }

        ImGui::TreePop();
    }

    if (!manga.errors.empty() && ImGui::TreeNode("Errors")) {
        ImGui::Text("Total errors: %zu", manga.errors.size());
        for (const auto& [type, errs] : manga.errors) {
            std::string err_cat_name = std::format("{} errors of type \"{}\"", errs.size(), type);
            if (ImGui::TreeNode(err_cat_name.c_str())) {
                for (size_t i = 0; const glz::generic& err : errs) {
                    ImGui::PushID(i);

                    if (err.is_string()) {
                        ImGui::Text("%s", err.get<std::string>().c_str());
                    } else {
                        std::size_t len = err.size();
                        std::string node_name = err.is_object() ? 
                            std::format("Object with {} keys", len) :
                            std::format("Array with {} items", len);

                        if (ImGui::TreeNode(node_name.c_str())) {
                            std::string json;
                            glz::ex::write<glz::opts{.prettify = true}>(err, json);
                            ImGui::Text("%s", json.c_str());

                            ImGui::TreePop();
                        }
                    }
                    ImGui::PopID();
                    ++i;
                }
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
}


float aspect(const File& file) {
    return (float) file.extent[1] / file.extent[0];
}


bool State::render_manga_chapter() {
    static std::pair<std::size_t, std::size_t> load_bounds {};
    if (!reading) {
        return false;
    }

    const auto [manga_index, obj_index] = *reading;
    const auto& manga = all_manga[manga_index];
    const auto& indices = obj_index < manga.volumes.size() ? 
        manga.volumes[obj_index].page_indices : 
        manga.chapters[obj_index - manga.volumes.size()].page_indices;

    const ImVec2 window_size = ImGui::GetWindowSize();
    // std::println(" -- RENDER: ({}x{} @ <x={}, y={}>) -- [{}, {}] ⊂ [0, {}) -- ",
    //              window_size.x, window_size.y, window_pos.y, window_pos.y,
    //              load_bounds.first, load_bounds.second, 0, indices.size());
        
    loader.process_tasks();
    {
        float scrolled = 0;
        bool found = false;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const File& file = manga.files[i];
            const ImVec2 scale_to { window_size.x, window_size.x * aspect(file) };

            if (scrolled <= ImGui::GetScrollY()) {
                load_bounds.first = i;
            }
            scrolled += scale_to.y;
            if (scrolled > ImGui::GetScrollY() + window_size.y) {
                load_bounds.second = i;
                found = true;
                break;
            }
        }
        if (!found) load_bounds.second = indices.size() - 1;

        load_bounds = std::make_pair(
            load_bounds.first      >= 5                  ? load_bounds.first  - 5 : 0,
            load_bounds.second + 5 <= indices.size() - 1 ? load_bounds.second + 5 : indices.size() - 1
        );
        if (load_bounds.second > load_bounds.second + loader.cache_size()) {
            load_bounds.second = load_bounds.second + loader.cache_size();
        }
    }

    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t file_index = indices[i];
        const File& file = manga.files[file_index];

        const ImVec2 scale_to { window_size.x, window_size.x * aspect(file) };
        
        const bool load_image = load_bounds.first <= i && i <= load_bounds.second;
        const std::optional<unsigned> image = load_image ? loader.get_image_id(file.file) : std::nullopt;
        const bool is_actual_image = image.has_value();
        const unsigned image_id = image.value_or(loader.placeholder());

        ImGui::PushID(file.file.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (ImGui::ImageButton("", image_id, scale_to) && is_actual_image) {
            ImGuiImage::State im_state;
            im_state.textureSize = ImVec2(file.extent[0], file.extent[1]);
            inspecting_image = std::make_tuple(&file, std::any(std::move(im_state)));
            open_inspect = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopID();

        if (ImGui::BeginPopupContextItem()) {
            const std::string repr = file.pages ? repr_range(*file.pages) : "<unknown page>";
            const std::filesystem::path where(file.file);
            const std::filesystem::path archive_p = where.parent_path();
            const std::string name = where.filename().string();
            const std::string archive = archive_p.filename().string();
            const std::string collection = archive_p.parent_path().filename().string();

            ImGui::Text("Image #%zu - %s", i, repr.c_str());
            ImGui::Text("File: %s", name.c_str());
            ImGui::Text("Archive: %s/%s", collection.c_str(), archive.c_str());

            ImGui::EndPopup();
        }
    }


    if (open_inspect) {
        ImGui::OpenPopup("image viewer");
    }
    if (inspecting_image) {
        const auto state = *inspecting_image;

        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        float w = screen.x * 0.9f, h = screen.y * 0.9f;
        const float a = aspect(*std::get<0>(state));

        if (w * a < h) {
            h = w * a;
        } else {
            w = h / a;
        }
        if (center_next_window(w, h), ImGui::BeginPopup("image viewer")) {
            if (!render_image_inspect()) {
                inspecting_image = std::nullopt;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }


    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        return false;
    }
    return true;
}


bool State::render_image_inspect() {
    if (!reading || !inspecting_image) {
        return false;
    }
    
    auto& [image, type_erased_state] = *inspecting_image;
    auto* state = std::any_cast<ImGuiImage::State>(&type_erased_state);
    const std::optional<unsigned> id = loader.get_image_id(image->file);

    if (id) {
        const ImVec2 region = ImGui::GetContentRegionAvail();
        ImGuiImage::Zoomable(*id, region, state);
    } else {
        ImGui::Text("loading...");
    }
    return true;
}
