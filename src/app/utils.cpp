#include "utils.h"
#include "colors.h"

#include <clip/clip.h>
#include <imgui/imgui.h>
#include <glaze/glaze.hpp>

#include <thread>
#include <bit>
#include <utility>
#include <format>


IDGuard::IDGuard(std::size_t id) { ImGui::PushID(id); }
IDGuard::IDGuard(std::string_view id) { ImGui::PushID(id.data()); }
IDGuard::IDGuard(const char* id) { ImGui::PushID(id); }
IDGuard::IDGuard(const void* id) { ImGui::PushID(id); }
IDGuard::~IDGuard() { ImGui::PopID(); }


void maximize_next_window() {
    auto& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
}


void highlight_text_block(const std::string_view text, std::size_t start, std::size_t end, std::uint32_t color, bool enabled) {
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

void copy_to_clipboard(std::string_view s) {
    clip::set_text(std::string(s));
}

void copy_to_clipboard(const std::size_t w, const std::size_t h, const unsigned char* p) {
    clip::image_spec spec;
    spec.width = w;
    spec.height = h;
    spec.bits_per_pixel = 32;
    spec.bytes_per_row = spec.width * spec.bits_per_pixel / CHAR_BIT;

    const bool le = std::endian::little == std::endian::native;
    // R G B A
    // le: 0xAABBGGRR
    // be: 0xRRGGBBAA
    spec.red_mask    = le ? 0x000000ff : 0xff000000;
    spec.green_mask  = le ? 0x0000ff00 : 0x00ff0000;
    spec.blue_mask   = le ? 0x00ff0000 : 0x0000ff00;
    spec.alpha_mask  = le ? 0xff000000 : 0x000000ff;
    spec.red_shift   = le ?  0 : 24;
    spec.green_shift = le ?  8 : 16;
    spec.blue_shift  = le ? 16 :  8;
    spec.alpha_shift = le ? 24 :  0;

    std::thread([spec, data=p] {
        clip::image img(data, spec);
        clip::set_image(img);
    }).detach();
}

bool render_copyable_text(std::string_view text, std::optional<std::uint32_t> color, bool wrapped) {
    bool copied = false;
    if (ImGui::BeginChild("##copy", {}, ImGuiChildFlags_AutoResizeY)) {
        if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
        if (wrapped) ImGui::PushTextWrapPos(0.0f);

        ImGui::TextUnformatted(text.data(), text.data() + text.size());

        if (wrapped) ImGui::PopTextWrapPos();
        if (color) ImGui::PopStyleColor();
        
        if (ImGui::BeginPopupContextWindow("##copyctx")) {
            if (ImGui::Button("Copy text?")) {
                copy_to_clipboard(text);
                ImGui::CloseCurrentPopup();

                copied = true;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    return copied;
}


std::string bytes_to_si_prefix(const std::size_t n) {
    constexpr static std::string_view prefix = "KMGTPEZYRQ";
    constexpr static std::size_t base = 1024;

    if (n < base) {
        return std::format("{} B", n);
    }

    double value = n;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        value /= base;
        if (value < base) {
            return std::format("{:.2f} {}iB", value, prefix[i]);
        }
    }
    std::unreachable();
}

void show_in_explorer(const std::string_view file, bool parent) {
    const std::string cmd = parent ? 
        std::format("explorer.exe /select,\"{}\"", file) :
        std::format("explorer.exe \"{}\"", file);

    std::system(cmd.c_str());
}


void set_window_size_by_settings(const PersistentAppSettings& settings, std::string_view key) {
    using size_struct_t = std::pair<float, float>;
    if (ImGui::IsWindowAppearing()) {  // settings.is_first_frame()) {
        const auto text = settings.get(key);
        const size_struct_t dims = text ? glz::read_json<size_struct_t>(*text).value_or(size_struct_t{}) : size_struct_t{};
        ImGui::SetWindowSize({dims.first, dims.second});
    }
}
void save_window_size_to_settings(PersistentAppSettings& settings, std::string_view key, bool force) {
    if (force || settings.wants_new_settings()) {
        ImVec2 dims = ImGui::GetWindowSize();
        settings.set(key, glz::write_json(std::make_pair(dims.x, dims.y)).value());
    }
}
