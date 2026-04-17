#include "state.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <zip.h>
#include <GL/glew.h>

#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


struct Archive {
    std::unique_ptr<unsigned char[]> buf {};
    std::size_t alloc = 0;

    zip_t* zip {};

    void init(std::string_view archive) {
        zip = zip_open(archive.data(), 0, 'r');
    }

    Image do_load(std::string path, std::string_view file) {
        assert(zip);

        // std::println(" --------------------- zip entry --------------------- ");
        // std::println("opening archive entry \"{}\"",  file);

        zip_entry_open(zip, file.data());
        
        const std::size_t size = zip_entry_size(zip);
        if (size > alloc) {
            const std::size_t resized = 3 * size / 2;
            // std::println("resizing internal buffer: {} -> {}", alloc, resized);

            buf = std::make_unique<unsigned char[]>(resized);
            alloc = resized;
        }

        zip_entry_noallocread(zip, buf.get(), alloc);
        // std::println("read archive entry: {} bytes", size);

        zip_entry_close(zip);

        // std::println("reading image from memory...");
        ImageInfo info;
        info.file = std::move(path);
        unsigned char* data = stbi_load_from_memory(
            buf.get(), size, 
            &info.width, &info.height, 
            &info.channels, 0
        );
        return Image { 
            .info = info,
            .data = data,
            .id = 0
        };
    }

    void close() {
        zip_close(zip);
    }
};

class ImageLoaderBase {
    constexpr static std::size_t maximum_archives = 3;
    constexpr static std::size_t maximum_images = 20;

    unsigned placeholder_image {};

    std::vector<std::string> archive_order;
    std::map<std::string, Archive, std::less<>> archives;  // only modified by the child thread
    
    std::vector<std::string> image_order;
    std::map<std::string, Image, std::less<>> images;  // only modified by the owning thread
    
    // modified by both
    std::vector<Image> finished_tasks;
    std::mutex finished_task_mutex;

    std::set<std::string, std::less<>> task_set;  // only modified or read by owning thread

    // modified by both
    std::queue<std::string> tasks;
    std::mutex task_mutex;
    std::condition_variable task_cv;

    std::jthread loader_thread;

    void loader_loop(std::stop_token token) {
        while (!token.stop_requested()) {
            std::unique_lock lock(task_mutex);
            task_cv.wait(lock, [this, &token] { return token.stop_requested() || !tasks.empty(); });
            if (token.stop_requested()) {
                break;
            }
            
            std::string path = std::move(tasks.front());
            tasks.pop();

            const auto fp = std::filesystem::path(path);

            const std::string name = fp.filename().string();
            const std::string archive = fp.parent_path().string();

            // std::println("starting task: loading \"{}\" from \"{}\"", name, archive);

            auto archive_ptr = archives.find(archive);
            if (archive_ptr == archives.end()) {
                if (archives.size() >= maximum_archives) {
                    const auto first = std::move(archive_order[0]);
                    std::println("reusing zip archive: \"{}\" as \"{}\"", name, archive);

                    archive_order.erase(archive_order.cbegin());
                    archive_order.emplace_back(archive);
                    
                    const auto it = archives.find(first);
                    it->second.close();
                    archive_ptr = archives.emplace(archive, std::move(it->second)).first;
                    archives.erase(it);
                } else {
                    std::println("creating new zip archive: \"{}\"", archive);
                    archive_order.emplace_back(archive);
                    archive_ptr = archives.emplace(archive, Archive{}).first;
                }
                archive_ptr->second.init(archive);
            }
            
            const auto finished = archive_ptr->second.do_load(std::move(path), name);
            {
                finished_task_mutex.lock();
                finished_tasks.emplace_back(std::move(finished));
                finished_task_mutex.unlock();
            }
        }
    }

    unsigned create_texture_for_image(ImageInfo& info, unsigned char* data) {
        unsigned id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        unsigned format;
        switch (info.channels) {
            case 1: {
                format = GL_LUMINANCE;
                break;
            }
            case 2: {
                format = GL_RG;
                break;
            }
            case 3: {
                format = GL_RGB;
                break;
            }
            case 4: {
                format = GL_RGBA;
                break;
            }
            default: {
                std::println(std::cerr, "unknown # of channels in image: {} @ \"{}\"", info.channels, info.file);
                format = GL_RGBA;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, format, GL_UNSIGNED_BYTE, data);
        return id;
    }
    
    void deleteImage(const Image& image) {
        // std::println("freeing gpu texture: {}", image.id);
        glDeleteTextures(1, &image.id);
    }

public:
    unsigned placeholder() {
        return placeholder_image;
    }

    void process_tasks() {
        finished_task_mutex.lock();
        
        for (Image& image : finished_tasks) {
            image.id = create_texture_for_image(image.info, image.data);
            // std::println("added \"{}\" to GPU memory: {}", image.info.file, image.id);

            stbi_image_free(image.data);
            image.data = nullptr;

            const auto& image_file = image.info.file;
            image_order.emplace_back(image_file);
            images.emplace(image_file, image);
            task_set.erase(image_file);

            if (image_order.size() > maximum_images) {
                // std::println("removing oldest cache entry: {}", image_order[0]);
                deleteImage(images.at(image_order[0]));
                images.erase(image_order[0]);
                image_order.erase(image_order.cbegin());
            }
        }
        finished_tasks.clear();

        finished_task_mutex.unlock();
    }

    std::optional<unsigned> get_image_id(std::string_view file) {
        const auto image = get_image(file);
        return image ? std::optional(image->id) : std::nullopt;
    }

    std::optional<Image> get_image(std::string_view file) {
        const auto it = images.find(file);
        if (it != images.end()) {
            return it->second;
        }

        // std::println("image not found: keys:");
        // for (const auto& [key, _] : images) {
        //     std::println(" > {}", key);
        // }
        if (!task_set.contains(file)) {
            std::unique_lock<std::mutex> lock(task_mutex);
            tasks.emplace(file);
            task_set.emplace(file);
            task_cv.notify_all();
        }

        return std::nullopt;
    }

    ImageLoaderBase() {
        {
            constexpr std::string_view placeholder_file = "unknown.png";

            ImageInfo image;
            unsigned char* data = stbi_load(
                placeholder_file.data(), 
                &image.width, &image.height, 
                &image.channels, 0
            );

            placeholder_image = create_texture_for_image(image, data);
        }

        loader_thread = std::jthread([this] (std::stop_token token) { 
            loader_loop(std::move(token));
        });
    }

    ~ImageLoaderBase() {
        if (loader_thread.joinable()) {
            loader_thread.request_stop();
            task_cv.notify_all();
            loader_thread.join();
        }

        for (const auto& [_, image] : images) {
            deleteImage(image);
        }

        for (auto& [_, archive] : archives) {
            archive.close();
        }
    }

    std::size_t cache_size() {
        return maximum_images;
    }
};


ImageLoader::ImageLoader() {
    base = std::make_unique<ImageLoaderBase>();
}
ImageLoader::~ImageLoader() {}

void ImageLoader::process_tasks() {
    base->process_tasks();
}
std::optional<unsigned> ImageLoader::get_image_id(std::string_view file) {
    return base->get_image_id(file);
}
unsigned ImageLoader::placeholder() {
    return base->placeholder();
}
std::size_t ImageLoader::cache_size() {
    return base->cache_size();
}
