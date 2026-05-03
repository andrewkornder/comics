#include <GL/glew.h>

#include "../app.h"
#include "../utils.h"
#include "image_loader.h"

#include <zip.h>

#include <imgui/imgui.h>

#include <filesystem>
#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <thread>

using namespace std::literals;

using file_ident_t = std::pair<std::string, std::string>;


struct ArchiveLoader {
    std::string archive;
    int upscale_factor = 1;

    std::queue<std::tuple<ArchiveLoader*, file_ident_t, Image>>* output;
    std::mutex* out_mutex;

    std::jthread worker;

    std::queue<file_ident_t> tasks;
    std::set<file_ident_t> task_set;
    std::mutex task_mutex;
    std::condition_variable task_cv;

    std::optional<file_ident_t> get_next_task(std::stop_token& token) {
        std::unique_lock lock(task_mutex);
        task_cv.wait(lock, [this, &token] { return token.stop_requested() || !tasks.empty(); });
        if (token.stop_requested()) {
            return std::nullopt;
        }
        
        file_ident_t task = std::move(tasks.front());
        tasks.pop();
        return task;
    }

    bool add_task(const file_ident_t value) {
        if (task_set.contains(value)) return false;
        task_set.emplace(value);

        if (!running()) {
            run();
        }
        std::unique_lock<std::mutex> lock(task_mutex);
        tasks.emplace(std::move(value));
        task_cv.notify_all();
        return true;
    }
    void ack_task(const file_ident_t& value) {
        task_set.erase(value);
    }

    bool running() const {
        return worker.joinable() && !worker.get_stop_token().stop_requested();
    }

    void pause() {
        if (running()) {
            worker.request_stop();
            task_cv.notify_all();
        }
    }
    void pause_and_wait_for() {
        if (worker.joinable()) {
            worker.request_stop();
            task_cv.notify_all();
            worker.join();
        }
    }

    void set_upscale(int up) {
        upscale_factor = up;
    }

    void worker_func(std::stop_token token) {
        const auto append = [this](file_ident_t key, const Image& image) {
            out_mutex->lock();
            output->emplace(this, std::move(key), image);
            out_mutex->unlock();
        };

        int zip_error = 0;

        zip_t* zip = zip_openwitherror(archive.c_str(), 0, 'r', &zip_error);
        if (zip_error) {
            dbg("zip error on opening archive: {} (path: {})",
                         zip_strerror(zip_error), archive);
        }

        std::unique_ptr<unsigned char[]> buf;
        std::size_t alloc = 0;

        while (!token.stop_requested()) {
            std::optional<file_ident_t> task = get_next_task(token);
            if (!task) break;

            if (zip_error) {
                append(std::move(*task), Image{ .w = -1, .h = -1 });
                continue;
            }

            std::size_t size;
            {
                if (const int err = zip_entry_open(zip, task->second.c_str())) {
                    dbg("zip error on opening entry: {} (entry: {})",
                                 zip_strerror(err), task->second);
                    append(std::move(*task), Image{ .w = -1, .h = -1 });
                    continue;
                }
                size = zip_entry_size(zip);
                if (size > alloc) {
                    const std::size_t resized = 3 * size / 2;
                    buf = std::make_unique<unsigned char[]>(resized);
                    alloc = resized;
                }

                zip_entry_noallocread(zip, buf.get(), alloc);
                zip_entry_close(zip);
            }

            if (token.stop_requested()) break;

            Image info = { .up = upscale_factor, .id = 0 };
            if (load_image_from_buffer(token, info, buf.get(), size)) {
                append(std::move(*task), info);
            }
        }
        zip_close(zip);
    }

    void run() {
        if (worker.joinable()) worker.join();

        worker = std::jthread([this](std::stop_token token) {
            worker_func(std::move(token));
        });
    }

    ~ArchiveLoader() {
        if (worker.joinable()) {
            worker.request_stop();
            task_cv.notify_all();
            worker.join();
        }
    }
};


enum class NonImageType {
    Loading,
    BadImage,
};

struct AppImageLoader::ImageCache {
    std::map<NonImageType, Image> placeholders;

    std::map<file_ident_t, Image, std::less<>> cache;
    std::set<file_ident_t, std::less<>> accessed;

    int upscale = 1;
    std::map<std::string, ArchiveLoader, std::less<>> workers;
    
    std::queue<std::tuple<ArchiveLoader*, file_ident_t, Image>> new_loads;
    std::mutex load_mutex;

    void delete_image(const Image& image) {
        if (image.w < 0) return;

        if (image.id) {
            glDeleteTextures(1, &image.id);
        }
        if (image.data) {
            free(image.data);
        }
        if (image.upscaled && image.upscaled != image.data) {
            free(image.upscaled);
        }
    }

    GLuint shader = 0;

    GLuint compile_shader() {
        auto compileShader = [](GLenum type, const char* source) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint logLength;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                std::vector<char> log(logLength);
                glGetShaderInfoLog(shader, logLength, nullptr, log.data());
                dbg("shader failed to compile: {}", log.data());
            }
            return shader;
        };

        const char* vertex_source = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aUV;
            layout (location = 2) in vec4 aColor;

            uniform mat4 ProjMtx;
            out vec2 Frag_UV;
            out vec4 Frag_Color;

            void main() {
                Frag_UV = aUV;
                Frag_Color = aColor;
                gl_Position = ProjMtx * vec4(aPos.xy, 0, 1);
            }
        )";
        const char* fragment_source = R"(
            #version 330 core
            in vec2 Frag_UV;
            in vec4 Frag_Color;
            out vec4 Out_Color;

            uniform sampler2D Texture;

            void main() {
                if (Frag_UV.x < 0.0 || Frag_UV.x > 1.0 || Frag_UV.y < 0.0 || Frag_UV.y > 1.0) {
                    Out_Color = vec4(0, 0, 0, 0);
                } else {
                    // Multiply by Frag_Color to respect ImGui's 'tint_col' parameter
                    Out_Color = texture(Texture, Frag_UV) * Frag_Color;
                }
            }
        )";
        GLuint vShader = compileShader(GL_VERTEX_SHADER, vertex_source);
        GLuint fShader = compileShader(GL_FRAGMENT_SHADER, fragment_source);

        GLuint program = glCreateProgram();
        glAttachShader(program, vShader);
        glAttachShader(program, fShader);
        glLinkProgram(program);

        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            GLint logLength;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(logLength);
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            dbg("shader program link failed: {}", log.data());
        }

        glDeleteShader(vShader);
        glDeleteShader(fShader);
        return program;
    }


    std::uint32_t create_texture_for_image(int w, int h, int bands, unsigned char* data) {
        if (shader == 0) {
            shader = compile_shader();
            assert(glGetError() == GL_NO_ERROR);
        }

        GLenum internal_storage, external_storage;
        bool has_swizzle = false;
        std::array<GLint, 4> swizzle_mask;
        switch (bands) {
            case 1: {
                internal_storage = GL_R8; external_storage = GL_RED;
                swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_ALPHA};
                has_swizzle = true;
                break;
            }
            case 2: {
                internal_storage = GL_RG8; external_storage = GL_RG;
                swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_GREEN};
                has_swizzle = true;
                break;
            }
            case 3: {
                internal_storage = GL_RGB8; external_storage = GL_RGB;
                break;
            }
            case 4: {
                internal_storage = GL_RGBA8; external_storage = GL_RGBA;
                break;
            }
            default: {
                dbg("unknown number of bands in image: {}", bands);
                return 0;
            }
        }

        std::uint32_t id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        if (has_swizzle) {
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask.data());
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_storage, w, h, 0, external_storage, GL_UNSIGNED_BYTE, data);

        glBindTexture(GL_TEXTURE_2D, 0);
        assert(glGetError() == GL_NO_ERROR);
        return id;
    }
    
    Image load_image_from_disk(std::filesystem::path path) {
        dbg("loading image from disk: {}", path.string());
        Image image {};
        load_image_from_file(image, path.string());
        image.id = create_texture_for_image(image.w, image.h, image.bands, image.data);

        image.upscaled = image.data;
        image.up = 1;
        return image;
    }

    ImageCache(std::string_view here) {
        init(here);
    }

    void init(const std::filesystem::path here) {
        placeholders = {
            { NonImageType::Loading, load_image_from_disk(here / UNKNOWN_PLACEHOLDER) },
            { NonImageType::BadImage, load_image_from_disk(here / ERROR_PLACEHOLDER) }
        };
    }
    ~ImageCache() {
        dbg("cleaning image cache: stopping workers");
        workers.clear();  // make sure no more writes to new_loads happen
        while (!new_loads.empty()) {
            auto& item = new_loads.front();
            delete_image(std::get<2>(item));
            new_loads.pop();
        }
        
        dbg("cleaning image cache: freeing textures");
        for (const auto& [_, image] : cache) {
            delete_image(image);
        }
        for (const auto& [_, image] : placeholders) {
            delete_image(image);
        }
    }

    auto clean_cache() {
        std::size_t stale = 0, paused = 0, new_archives = 0, new_access = 0;
        for (auto ptr = cache.begin(); ptr != cache.end(); ) {
            if (!accessed.contains(ptr->first)) {
                ++stale;
                delete_image(ptr->second);
                ptr = cache.erase(ptr);
            } else {
                ++ptr;
            }
        }

        std::set<std::string, std::less<>> archives_accessed;
        for (const file_ident_t& key : accessed) {
            archives_accessed.emplace(key.first);
        }
        for (auto& [archive_path, archive] : workers) {
            if (archive.running() && !archives_accessed.contains(archive_path)) {
                ++paused;
                archive.pause();
            }
        }
    
        std::vector<file_ident_t> to_load;
        for (auto ptr = accessed.begin(); ptr != accessed.end(); ) {
            if (cache.contains(*ptr)) {
                ptr = accessed.erase(ptr);
            } else {
                to_load.emplace_back(*ptr);
                ++ptr;
            }
        }
        std::sort(to_load.begin(), to_load.end());
        for (auto& key : to_load) {
            const auto [it, new_archive] = workers.try_emplace(key.first, 
                key.first, upscale, &new_loads, &load_mutex
            );
            new_access += it->second.add_task(std::move(key));

            new_archives += new_archive;
        }

        accessed.clear();
        return std::make_tuple(stale, paused, new_archives, new_access);
    }

    void maybe_add_new_image() {
        if (new_loads.empty()) {
            return;
        }

        load_mutex.lock();
        auto [archive, key, image] = std::move(new_loads.front());
        new_loads.pop();
        load_mutex.unlock();

        if (image.w > 0) {
            if (image.upscaled && image.upscaled != image.data) {
                image.id = create_texture_for_image(
                    image.w * image.up, image.h * image.up, image.bands,
                    image.upscaled
                );
                free(image.upscaled);
                image.upscaled = nullptr;
            } else {
                image.id = create_texture_for_image(
                    image.w, image.h, image.bands, image.data
                );
            }
        }

        cache.emplace(key, image);

        archive->ack_task(key);
    }

    void clear_cache() {
        accessed.clear();
        clean_cache();
    }

    void update_cache() {
        using clock = std::chrono::steady_clock;
        using time_ms = std::chrono::duration<double, std::milli>;

        const auto start = clock::now();
        const auto cc = clean_cache();
        const auto elapsed = time_ms(clock::now() - start);
        if (elapsed > 5ms || (
                std::get<0>(cc) != 0 ||
                std::get<1>(cc) != 0 ||
                std::get<2>(cc) != 0 ||
                std::get<3>(cc) != 0
            )) {
            // dbg("cleaned cache in {:.1f}ms = {}", elapsed.count(), cc);
        }

        const auto second_now = clock::now();
        maybe_add_new_image();
        const auto add_time = time_ms(clock::now() - second_now);
        if (add_time > 5ms) {
            dbg("added new images in {:.1f}ms", add_time.count());
        }
    }

    std::pair<bool, Image> get(const File& file, const FileInfo& info) {
        file_ident_t key(file.archive_path, file.filename);

        accessed.emplace(key);
        if (info.width == (std::size_t) -1) {
            return { true, placeholders.at(NonImageType::BadImage) };
        }
        const auto it = cache.find(key);
        if (it == cache.end()) {
            return { false, placeholders.at(NonImageType::Loading) };
        }
        if (it->second.w <= 0 || it->second.id == 0) {
            return { true, placeholders.at(NonImageType::BadImage) };
        }
        return { true, it->second };
    }

    bool render(AppRoot& root) {
        bool is_open = true;
        if (ImGui::Begin("Image Cache", &is_open)) {
            render_no_window(root);
        }
        ImGui::End();
        return is_open;
    }

    bool render_no_window(AppRoot& root) {
        if (ImGui::Button("Clear cache")) {
            clear_cache();
        }

        ImGui::SameLine();
        if (ImGui::SliderInt("Upscale Factor", &upscale, 1, 4)) {
            std::set<std::string, std::less<>> running;
            for (auto& [name, archive] : workers) {
                if (archive.running()) {
                    running.emplace(name);
                    archive.pause_and_wait_for();
                }
                archive.set_upscale(upscale);
            }
            accessed.clear();
            clean_cache();
            for (const auto& name : running) {
                workers.at(name).run();
            }
        }

        if (ImGui::TreeNode(std::format("{} image(s) accessed this frame###access", accessed.size()).c_str())) {
            for (const file_ident_t& key : accessed) {
                const auto text = std::format("{} @ {}", key.first, key.second);
                ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode(std::format("{} image(s) loaded###loaded", cache.size()).c_str())) {
            for (const auto& [key, image] : cache) {
                const auto text = std::format("id={}: {}x{} (x{}) - {} @ {}", 
                    image.id, image.w, image.h, image.up, key.first, key.second
                );
                ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Image(image.id, ImVec2(150.0f * image.w / image.h, 150.0f));
                    ImGui::EndTooltip();
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Archives")) {
            std::size_t unloaded = 0;
            for (const auto& [name, archive] : workers) {
                if (!archive.running()) {
                    ++unloaded;
                    continue;
                }

                ImGui::TextUnformatted(name.c_str(), name.c_str() + name.size());
            }
            if (unloaded) {
                ImGui::Text("%zu archive(s) unloaded", unloaded);
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Static Images")) {
            ImGui::Text("%zu static images loaded", placeholders.size());
            for (const auto& [type, image] : placeholders) {
                std::string_view name;
                switch (type) {
                    case NonImageType::Loading: name = "loading"; break;
                    case NonImageType::BadImage: name = "bad image"; break;
                }
                const auto text = std::format("\"{}\" (id={}): {}x{} (x{})", 
                    name, image.id, image.w, image.h, image.up
                );
                ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Image(image.id, {150.0f * image.h / image.w, 150});
                    ImGui::EndTooltip();
                }
            }

            ImGui::TreePop();
        }
        return true;
    }

    void add_imgui_image(int id, float w, float h, float u0, float v0, float u1, float v1) {
        const auto pos = ImGui::GetCursorScreenPos();

        ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
            const auto shader = * (GLuint*) cmd->UserCallbackData;
            glUseProgram(shader);
            glBindSampler(0, 0);

            float L = ImGui::GetIO().DisplaySize.x * 0.0f;
            float R = ImGui::GetIO().DisplaySize.x;
            float T = ImGui::GetIO().DisplaySize.y * 0.0f;
            float B = ImGui::GetIO().DisplaySize.y;

            const float ortho_projection[4][4] = {
                { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
                { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
                { 0.0f,         0.0f,        -1.0f,   0.0f },
                { (R+L)/(L-R), (T+B)/(B-T),   0.0f,   1.0f },
            };

            GLint proj_loc = glGetUniformLocation(shader, "ProjMtx");
            if (proj_loc != -1) {
                glUniformMatrix4fv(proj_loc, 1, GL_FALSE, &ortho_projection[0][0]);
            }
        }, &shader);
        ImGui::GetWindowDrawList()->AddImage(id, pos, {pos.x + w, pos.y + h}, {u0, v0}, {u1, v1});
        ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    bool render_empty(AppRoot& root, float w, float h) {
        const Image& image = placeholders.at(NonImageType::Loading);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
        add_imgui_image(image.id, w, h, 0, 0, 1, 1);
        ImGui::PopStyleVar();
        return true;
    }

    bool render_image(AppRoot& root, const File& file, const FileInfo& info, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        const auto [loaded, image] = get(file, info);

        ImGui::PushID(file.archive_path.c_str());
        ImGui::PushID(file.filename.c_str());

        const auto cursor = ImGui::GetCursorPos();
        ImGui::SetNextItemAllowOverlap();
        add_imgui_image(image.id, w, h, u0, v0, u1, v1);
        ImGui::SetCursorPos(cursor);
        ImGui::InvisibleButton("##inner_page_inv_btn", {w, h});

        ImGui::PopID();
        ImGui::PopID();
        return true;
    }

    void copy_image(const File& file) {
        file_ident_t key(file.archive_path, file.filename);
        accessed.emplace(key);

        const auto it = cache.find(key);
        if (it == cache.end() || it->second.w <= 0 || it->second.id == 0) {
            return;
        }

        const auto& im = it->second;
        copy_to_clipboard(im.w, im.h, im.data);
    }

    std::pair<bool, unsigned> get_image_id(const File& file, const FileInfo& info) {
        const auto [loaded, image] = get(file, info);
        return { loaded, image.id };
    }

    std::size_t cache_size() const {
        return cache.size();
    }
    std::size_t opened_archives() const {
        std::size_t cnt = 0;
        for (const auto& [_, archive] : workers) {
            cnt += archive.running();
        }
        return cnt;
    }
    std::size_t total_archives() const {
        return workers.size();
    }
};


AppImageLoader::AppImageLoader(std::string_view here) {
    cache = std::make_unique<ImageCache>(here);
}
AppImageLoader::~AppImageLoader() {}

void AppImageLoader::clear_cache() {
    cache->clear_cache();
}
void AppImageLoader::update_cache() {
    cache->update_cache();
}

bool AppImageLoader::render(AppRoot& root) {
    return cache->render(root);
}
bool AppImageLoader::render_no_window(AppRoot& root){
    return cache->render_no_window(root);
}
bool AppImageLoader::render_empty(AppRoot& root, float w, float h) {
    return cache->render_empty(root, w, h);
}
bool AppImageLoader::render_image(AppRoot& root, const File& file, const FileInfo& info, float w, float h, float u0, float v0, float u1, float v1) {
    return cache->render_image(root, file, info, w, h,
                               u0, v0, u1, v1);
}
std::pair<bool, unsigned> AppImageLoader::get_image_id(const File& file, const FileInfo& info) {
    return cache->get_image_id(file, info);
}

void AppImageLoader::copy_image(const File& file) {
    return cache->copy_image(file);
}

std::size_t AppImageLoader::cache_size() const {
    return cache->cache_size();
}
std::size_t AppImageLoader::opened_archives() const {
    return cache->opened_archives();
}
std::size_t AppImageLoader::total_archives() const {
    return cache->total_archives();
}
