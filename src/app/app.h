#pragma once

#include "../types.h"

#include <iostream>
#include <format>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


#define _STRINGIFY(v) #v
#define STRINGIFY(v) _STRINGIFY(v)
#define _COMPILE_TIME_RES(root, path) root path
#define COMPILE_TIME_RES(path) _COMPILE_TIME_RES(STRINGIFY(COMPILE_DIR), path)


template<typename ...Args>
std::ostream& dbg(std::ostream&, std::format_string<Args...> fmt, Args&& ...args);
template<typename ...Args>
void dbg(std::format_string<Args...> fmt, Args&& ...args);


struct PersistentAppSettings {
    void start_new_frame();
    bool wants_new_settings() const;
    bool is_first_frame() const;
    bool has_pending_writes() const;

    std::string dump_to_ini();
    void read_line_ini(std::string_view);
    void notify_saved();

    std::optional<std::string_view> get(std::string_view) const;
    void set(std::string_view, std::string);

private:
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::seconds;
    static constexpr duration update_delay = duration(1);

    std::optional<clock::time_point> last_save;
    std::optional<clock::time_point> last_frame;
    std::optional<clock::time_point> frame_time;
    bool pending_write = false;

    std::map<std::string, std::string, std::less<>> data;
};


class AppRoot;


struct PendingAppUpdates {
    bool close;
    bool maximize;
    bool minimize;
    int w, h;
    int x, y;
};

struct AppData {
    int x, y;
    int w, h;
    bool maximized;
    bool minimized;
};


struct AppImageLoader {
#if defined(_WIN32) || defined(_WIN64)
    constexpr static std::string_view UNKNOWN_PLACEHOLDER = R"(images/loading.png)";
    constexpr static std::string_view ERROR_PLACEHOLDER = R"(images/bad_image.png)";
#else
    constexpr static std::string_view UNKNOWN_PLACEHOLDER = R"(images/loading.png)";
    constexpr static std::string_view ERROR_PLACEHOLDER = R"(images/bad_image.png)";
#endif

    struct ImageCache;
    std::unique_ptr<ImageCache> cache;

    AppImageLoader(std::string_view);
    ~AppImageLoader();

    void update_cache();
    void clear_cache();
    std::size_t cache_size() const;
    std::size_t opened_archives() const;
    std::size_t total_archives() const;

    bool render(AppRoot&);
    bool render_no_window(AppRoot&);

    bool render_empty(AppRoot&, float w, float h);
    bool render_image(AppRoot&, const File&, const FileInfo&, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
    void copy_image(const File&);
    // bool render_image_ctx(AppRoot&, const File&, );
    std::pair<bool, unsigned> get_image_id(const File&, const FileInfo&);
};


struct AppJSONExplorer {
    using clock = std::chrono::steady_clock;
    using time_point_t = std::chrono::time_point<clock>;

    constexpr static std::chrono::milliseconds min_update_ms{150};
#if defined(_WIN32) || defined(_WIN64)
    constexpr static std::string_view JQ_PATH = R"(jq.exe)";
#else
    constexpr static std::string_view JQ_PATH = R"(jq)";
#endif

    using result_t = std::pair<std::string, std::string>;

    struct JQWrapperBase;
    class JQWrapper {
        std::unique_ptr<JQWrapperBase> base;

    public:
        JQWrapper(std::string_view, std::string_view);
        JQWrapper(JQWrapper&&);
        ~JQWrapper();

        bool update(std::string_view);
        void set_compact(bool);
        const std::optional<result_t>& get_output();
    };

    std::string json;

    std::array<char, 1024> filter;
    JQWrapper jq;
    time_point_t last_update;
    bool compact = false;
    bool wrap = false;
    bool has_update = false;

    AppJSONExplorer(std::string_view, std::string);

    bool render_no_window(AppRoot&);
    bool render(AppRoot&, std::string_view id);
};


struct AppErrorFilter {
    bool present = false;
    std::string value {};

    bool apply(const Error& err);
    bool apply(const ErrorList& errs);
    void render_no_window(AppRoot&, const std::map<std::string, std::size_t, std::less<>>&, bool optional);
};


struct AppErrorList {
    std::string title;

    ErrorList errors;

    AppErrorFilter filter;

    AppErrorList(std::string title, const ErrorList& errors, const std::vector<std::size_t>& subset);

    bool as_json;

    bool render_no_window(AppRoot&);
    bool render(AppRoot&, std::string_view id);
};


struct AppChapterReader {
    MangaHeader manga;
    std::vector<File> files;
    std::vector<FileInfo> info;
    std::string title;

    struct ScrollingDisplay {
        float progress = 0;
        float total_distance = 0;
        float max_scroll = 0;
        std::pair<float, float> offsets {};
        std::pair<std::size_t, std::size_t> visible {};
    };
    struct PagedDisplay {
        std::ptrdiff_t page;
        static float width;
        static bool ltr;
    };
    struct Display {
        ScrollingDisplay scroll; 
        PagedDisplay paged;
        int index;
    } dis;

    void render_pages(AppRoot&, ScrollingDisplay&);
    void render_pages(AppRoot&, PagedDisplay&);
    bool render_page_counter(std::size_t page, float percentage);

    std::pair<float, float> render_single_page(AppRoot&, const std::size_t i, float width, float u0, float v0, float u1, float v1);

    void jump_to(std::size_t);
    void jump_to(float);

    bool render_no_window(AppRoot&);
    bool render(AppRoot&, std::string_view id);
};

struct AppImageInspect {
    MangaHeader manga;
    File file;
    FileInfo info;
    std::optional<AppErrorList> error_list;

    float u0 = 0, v0 = 0;
    float u1 = 1, v1 = 1;

    void update_image(float dx, float dy, float ds);

    bool render_no_window(AppRoot&);
    bool render(AppRoot&, std::string_view id);
};


struct AppMangaDetails {
    MangaHeader header;
    std::optional<Manga> data;
    std::vector<std::string> chapter_order;
    std::optional<const Manga::Collection*> pending_open;
    std::optional<AppErrorList> errors;
    std::optional<std::pair<const Manga::Collection*, AppErrorList>> opened_chapter;

    void render_chapter_details(AppRoot&, const std::string_view where, const Manga::Collection& coll);
    void render_no_window(AppRoot&);

    void update(AppRoot&);
};


struct AppMangaList {
    struct SearchResult {
        std::size_t index;
        std::size_t start, end;
        float score;
    };
    struct SearchBarBase;
    class SearchBar {
        std::unique_ptr<SearchBarBase> base;

    public:
        SearchBar();
        ~SearchBar();

        void add_candidate(std::string_view);
        bool render_no_window(AppRoot&);
        SearchResult at(std::size_t);
    };

    const std::vector<MangaHeader>* all_manga;
    SearchBar search;
    float min_score = 50.0f;

    std::optional<AppMangaDetails> selected;

    AppErrorFilter error_filter;
    
    explicit AppMangaList(const std::vector<MangaHeader>&);
    bool render_no_window(AppRoot&);

    void update(AppRoot&);
};


struct AppIndexLoader {
#if defined(_WIN32) || defined(_WIN64)
    constexpr static std::string_view FILE_DB_PATH = R"(data\info.zip)";
#else
    constexpr static std::string_view FILE_DB_PATH = R"(data/info.zip)";
#endif

    struct AppIndexLoaderBase;
    std::unique_ptr<AppIndexLoaderBase> base;

    AppIndexLoader(std::string_view);
    ~AppIndexLoader();

    void update_cache();
    std::optional<Manga> get_info_for_manga(const std::string_view path);
    std::optional<FileInfoList> get_file_info_for_manga(const std::string_view path);
    
    bool render_no_window(AppRoot&);
    bool render(AppRoot&);
};


struct AppMangaLoader {
#if defined(_WIN32) || defined(_WIN64)
    constexpr static std::string_view MANGA_DB_PATH = R"(data\manga.json)";
#else
    constexpr static std::string_view MANGA_DB_PATH = R"(data/manga.json)";
#endif

    std::string path;
    std::vector<MangaHeader> manga;
    std::map<std::string, std::size_t, std::less<>> error_counts;

    bool loading;
    std::size_t loaded, total;
    std::optional<std::jthread> loader;

    AppMangaLoader(std::string_view);
    ~AppMangaLoader();

    void reload();
    bool render_no_window(AppRoot&);
    bool is_finished();
};


struct GLFWwindow;

class AppRoot {
    constexpr static int MIN_WINDOW_W = 100, MIN_WINDOW_H = 100;
    constexpr static int INITIAL_W = 1800, INITIAL_H = 1000;
    constexpr static int VSYNC_INTERVAL = 1;

    constexpr static std::string_view TITLE = "Manga Reader";

#if defined(_WIN32) || defined(_WIN64)
    constexpr static std::array FONTS = {
        R"(fonts\JetBrainsMono-2.304\fonts\ttf\JetBrainsMono-Regular.ttf)",
        R"(fonts\Noto_Sans\static\NotoSans-Regular.ttf)"
    };
    constexpr static std::string_view ICON_FILE = R"(icon.png)";
#else
    constexpr static std::array FONTS = {
        R"(fonts/JetBrainsMono-2.304/fonts/ttf/JetBrainsMono-Regular.ttf)",
        R"(fonts/Noto_Sans/static/NotoSans-Regular.ttf)"
    };
    constexpr static std::string_view ICON_FILE = R"(images/icon.png)";
#endif

    std::string here;

    GLFWwindow* window;
    AppData state;

    std::vector<std::string> logs;

    bool show_cache = false;
    bool show_index = false;
    bool show_id_stack = false;
    bool show_metrics = false;
    bool show_style_editor = false;
    bool show_state = false;

public:
    PersistentAppSettings settings;

    AppMangaLoader database;

    std::optional<AppIndexLoader> file_index;
    std::optional<AppImageLoader> images;

    std::optional<AppMangaList> manga_list;
    std::map<std::string, AppChapterReader> reader;
    std::map<std::string, AppImageInspect> inspect;
    std::map<std::string, AppErrorList> errors;
    std::map<std::string, AppJSONExplorer> json_exp;

    std::string_view get_root_dir() const { return here; }

    template<typename ...Args>
    auto& log(std::format_string<std::type_identity_t<Args>...> fmt,
             Args&& ...args) {
        return logs.emplace_back(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename T>
    bool make_new_window(std::map<std::string, T>& group, std::string key, T win) {
        group.erase(key);
        return group.try_emplace(key, std::move(win)).second;
    }

private:
    bool render_no_window();

    void render_window_state(PendingAppUpdates&);
    void render_base_window(PendingAppUpdates&);
    void render(PendingAppUpdates&);
    void apply_updates(const PendingAppUpdates&);

public:
    AppRoot(std::string_view);
    ~AppRoot();

    int init();
    void mainloop();
};


template<typename ...Args>
std::ostream& dbg(std::ostream& os, std::format_string<Args...> fmt, Args&& ...args) {
    return os << std::format(fmt, std::forward<Args>(args)...) << '\n';
}
template<typename ...Args>
void dbg(std::format_string<Args...> fmt, Args&& ...args) {
    std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
}
