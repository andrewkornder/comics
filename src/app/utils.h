#pragma once

#include "../types.h"
#include "app.h"

#include <string_view>
#include <optional>
#include <cstdint>
#include <string>

struct IDGuard {
    IDGuard(std::size_t id);
    IDGuard(std::string_view id);
    IDGuard(const char* id);
    IDGuard(const void* id);
    ~IDGuard();
};


void maximize_next_window();
void highlight_text_block(const std::string_view text, std::size_t start, std::size_t end, std::uint32_t color, bool enabled = true);

void copy_to_clipboard(std::string_view);
void copy_to_clipboard(std::size_t w, std::size_t h, const unsigned char* p);
bool render_copyable_text(std::string_view, std::optional<std::uint32_t> color = std::nullopt, bool wrapped = false);

std::string bytes_to_si_prefix(std::size_t);

void show_in_explorer(const std::string_view file, bool parent);

void set_window_size_by_settings(const PersistentAppSettings& settings, std::string_view key);
void save_window_size_to_settings(PersistentAppSettings& settings, std::string_view key, bool force = false);
