#include "../app.h"
#include "../utils.h"

#include "../../types_glz.h"

#include <zip.h>
#include <glaze/glaze.hpp>
#include <imgui/imgui.h>


struct AppIndexLoader::AppIndexLoaderBase {
    zip_t* db;
    std::map<std::string, std::tuple<std::string, std::string>> directory;
    std::unique_ptr<unsigned char[]> buf = nullptr;
    std::size_t bufsize = 0;

    using finished_t = std::variant<Manga, FileInfoList>;

    std::optional<std::pair<std::string, finished_t>> pending;
    std::optional<std::pair<bool, std::string>> next_task;
    std::atomic<bool> worker_finished = true;
    std::jthread worker;

    std::map<std::pair<std::size_t, std::string>, finished_t, std::less<>> finished;

    std::size_t load_into_buf(const std::string_view file) {
        if (db == nullptr) {
            return 0;
        }
        zip_entry_open(db, file.data());
        if (const int err = zip_entry_open(db, file.data())) {
            dbg(std::cerr, "zip error on opening entry: {} (entry: {})",
                         zip_strerror(err), file);
            return 0;
        }

        const std::size_t size = zip_entry_size(db);
        if (1 + size > bufsize) {
            const std::size_t resized = 3 * size / 2;
            buf = std::make_unique<unsigned char[]>(resized);
            bufsize = resized;
        }

        zip_entry_noallocread(db, buf.get(), bufsize);
        zip_entry_close(db);

        buf[size] = 0;
        return size;
    }

    template<typename T>
    bool load(const std::string_view file, T& value) {
        dbg("opening index file: {}", file);
        const std::size_t size = load_into_buf(file);
        if (size == 0) return false;

        const std::string_view text {(char*) buf.get(), size};

        auto err = glz::read_json(value, text);
        if (err) {
            dbg(std::cerr, "failed to read index file: {}", file);
            dbg(std::cerr, "{}", glz::format_error(err, text));
            return false;
        }
        return true;
    }

    AppIndexLoaderBase(const std::string_view path) {
        std::filesystem::path where = path;
        where = where / FILE_DB_PATH;

        int zip_error;
        db = zip_openwitherror(where.string().c_str(), 0, 'r', &zip_error);
        if (zip_error) {
            dbg(std::cerr, "zip error on opening archive: {} (path: {})",
                         zip_strerror(zip_error), path);
            db = nullptr;
        } else {
            load("info.json", directory);
        }
    }

    bool render_no_window(AppRoot& root) {
        if (worker_finished || !worker.joinable()) {
            ImGui::TextUnformatted("worker: not active");
        } else {
            ImGui::TextUnformatted("worker: active");
        }

        ImGui::Text("archive buffer: %s", bytes_to_si_prefix(bufsize).c_str());
        return true;
    }

    bool render(AppRoot& root) {
        bool is_open = true;
        if (ImGui::Begin("DB", &is_open)) {
            render_no_window(root);
        }
        ImGui::End();
        return is_open;
    }

    void try_add_task(bool load_info, const std::string_view name) {
        if (!next_task && worker_finished) next_task.emplace(load_info, name);
    }
    
    void update_cache() {
        if (db == nullptr || directory.empty()) {
            return;  // bad data
        }
        if (!worker_finished) {
            return;  // still working
        }
        if (pending) {
            const auto [key, value] = *std::move(pending);
            pending = std::nullopt;

            const auto idx = value.index();
            finished.emplace(std::make_pair(idx, std::move(key)), std::move(value));
        }

        if (next_task) {
            if (finished.find(*next_task) == finished.end()) {
                worker_finished = false;

                worker = std::jthread([this](std::stop_token token, const std::pair<bool, std::string> task) {
                    const auto& [load_info, name] = task;

                    const auto& path = directory[name];
                    if (load_info) {
                        FileInfoList info;
                        if (load(std::get<1>(path), info)) {
                            pending.emplace(std::move(name), std::move(info));
                        }
                    } else {
                        Manga manga;
                        if (load(std::get<0>(path), manga)) {
                            pending.emplace(std::move(name), std::move(manga));
                        }
                    }

                    worker_finished = true;
                }, *std::move(next_task));
            }
            next_task = std::nullopt;
        }
    }

    std::optional<Manga> get_info_for_manga(const std::string_view path) {
        const auto it = finished.find(std::make_pair(0, path));
        if (it == finished.end()) {
            try_add_task(false, path);
            return std::nullopt;
        }
        auto value = std::move(it->second);
        finished.erase(it);
        return std::get<Manga>(value);
    }

    std::optional<FileInfoList> get_file_info_for_manga(const std::string_view path) {
        const auto it = finished.find(std::make_pair(1, path));
        if (it == finished.end()) {
            try_add_task(true, path);
            return std::nullopt;
        }
        auto value = std::move(it->second);
        finished.erase(it);
        return std::get<FileInfoList>(value);
    }
};



AppIndexLoader::AppIndexLoader(const std::string_view db) {
    base = std::make_unique<AppIndexLoaderBase>(db);
}
AppIndexLoader::~AppIndexLoader() {}

void AppIndexLoader::update_cache() {
    return base->update_cache();
}

std::optional<Manga> AppIndexLoader::get_info_for_manga(const std::string_view path) {
    return base->get_info_for_manga(path);
}
std::optional<FileInfoList> AppIndexLoader::get_file_info_for_manga(const std::string_view path) {
    return base->get_file_info_for_manga(path);
}

bool AppIndexLoader::render_no_window(AppRoot& root) {
    return base->render_no_window(root);
}
bool AppIndexLoader::render(AppRoot& root) {
    return base->render(root);
}
