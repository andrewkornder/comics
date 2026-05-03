#include "app.h"

#include <glaze/glaze.hpp>

void PersistentAppSettings::start_new_frame() {
    last_frame = frame_time;
    frame_time = clock::now();
}

bool PersistentAppSettings::wants_new_settings() const {
    return last_save ? std::chrono::duration_cast<duration>(*frame_time - *last_save) > update_delay : true;
}

bool PersistentAppSettings::is_first_frame() const {
    return !last_frame;
}

bool PersistentAppSettings::has_pending_writes() const {
    return pending_write;
}

std::string PersistentAppSettings::dump_to_ini() {
    std::string buf = std::format("[PersistentAppSettings][All]");
    for (const auto& [name, text] : data) {
        buf.push_back('\n');
        buf.append(std::format("{}={}", name, glz::write_json(text).value()));
    }
    return buf;
}
void PersistentAppSettings::notify_saved() {
    last_save = frame_time;
}

void PersistentAppSettings::read_line_ini(std::string_view line) {
    const auto delim = line.find('=');
    if (delim == std::string_view::npos) return;

    std::string_view key = line.substr(0, delim);
    while (key.size() && key.back() == ' ') key.remove_suffix(1);
    set(key, glz::read_json<std::string>(line.substr(delim + 1)).value());
}

std::optional<std::string_view> PersistentAppSettings::get(std::string_view key) const {
    const auto it = data.find(key);
    return it == data.end() ? std::nullopt : std::optional(std::string_view{it->second});
}
void PersistentAppSettings::set(std::string_view key, std::string value) {
    const auto it = data.find(key);
    if (it == data.end()) {
        data.emplace(key, std::move(value));
    } else {
        it->second = std::move(value);
    }
    pending_write = true;
}
