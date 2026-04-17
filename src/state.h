#ifndef STATE_H
#define STATE_H
#include <any>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <regex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <vector>

#include "types.h"


class Filterer;

class ImageLoaderBase;
class ImageLoader {
    std::unique_ptr<ImageLoaderBase> base;

public:
    unsigned placeholder();
    void process_tasks();
    std::optional<unsigned> get_image_id(std::string_view file);
    std::size_t cache_size();

    ImageLoader();
    ~ImageLoader();
};

struct ImageInfo {
    std::string file;
    int width, height, channels;
};

struct Image {
    ImageInfo info;
    unsigned char* data;
    unsigned id;
};


class State {
    std::vector<Manga> all_manga;

    std::string manga_source_path;
    std::vector<Manga> newly_parsed_manga;
    std::mutex manga_mutex;
    std::atomic<bool> has_new_manga;
    std::jthread manga_parser;

    std::unique_ptr<Filterer> filter;

    ImageLoader loader;

    std::optional<std::size_t> selected_manga;

    std::optional<std::tuple<std::size_t, std::size_t>> reading;

    std::optional<std::tuple<const File*, std::any>> inspecting_image;

    bool open_chapter, open_inspect;

public:
    int load(std::string_view path);
    void render();

    State();
    ~State();

private:
    void render_manga_list();

    void render_manga_details();

    bool render_manga_chapter();

    bool render_image_inspect();
};

#endif
