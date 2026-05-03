#include "../types.h"
#include "../types_glz.h"

#include <format>
#include <iostream>
#include <filesystem>
#include <type_traits>

#include <argparse/argparse.hpp>
#include <glaze/glaze.hpp>
#include <zip.h>


template<typename ...T>
struct staged_func_result {};

template<typename Func>
struct staged_func_result<Func> {
    using type = std::invoke_result_t<Func>;
};

template<typename T, typename Is>
struct strip_last_impl {
};

template<typename T, std::size_t ...I>
struct strip_last_impl<T, std::index_sequence<I...>> {
    using tup = std::tuple<std::tuple_element_t<I, T>...>;
};

template<typename ...T>
struct strip_last {
    using tup = typename strip_last_impl<std::tuple<T...>, std::make_index_sequence<sizeof...(T) - 1>>::tup;
};

template<typename ...T>
struct staged_func_result<std::tuple<T...>> {
    using type = typename staged_func_result<T...>::type;
};

template<typename ...T> requires (sizeof...(T) > 1)
struct staged_func_result<T...> {
    using tup = std::tuple<T...>;
    using last_stage = std::tuple_element_t<sizeof...(T) - 1, tup>;
    using type = std::invoke_result_t<last_stage, typename staged_func_result<
        typename strip_last<T...>::tup
    >::type>;
};


std::string format_elapsed(const std::chrono::duration<double>& elapsed) {
    namespace chr = std::chrono;
    if (elapsed < chr::seconds(1)) {
        return std::format("{:.1f}ms", 1000. * elapsed.count());
    }
    if (elapsed < chr::minutes(1)) {
        return std::format("{:.1f}s", elapsed.count());
    }
    
    int hours = (int) (elapsed.count() / 3600);
    int minutes = (int) (elapsed.count() / 60) % 60;
    double seconds = (int) elapsed.count() % 60;
    if (hours) {
        return std::format("{}h{:02d}m{:04.1f}s", hours, minutes, seconds);
    }
    return std::format("{}m{:04.1f}s", minutes, seconds);
}


template<typename ...Stages, std::size_t ...Is, typename T = staged_func_result<typename Stages::second_type...>::type>
T get_cached_pipeline_impl(bool clean, std::tuple<Stages...> stages, std::index_sequence<Is...>) {
    using clock = std::chrono::steady_clock;
    using dur_t = std::chrono::duration<double>;

    auto [cache, function] = std::move(std::get<sizeof...(Stages) - 1>(stages));

    if (!clean) {
        if (is_regular_file(cache)) {
            std::cout << "found cache at " << cache.string() << '\n';
            T obj;
            std::string buf;

            const auto start = clock::now();
            auto ec = glz::read_file_json(obj, cache.string(), buf);
            const auto elapsed = std::chrono::duration_cast<dur_t>(clock::now() - start);
            if (ec) {
                throw std::runtime_error(std::format("read error @ {}:\n{}", cache.string(), glz::format_error(ec, buf)));
            } else {
                std::cout << "read cache in " << format_elapsed(elapsed) << '\n';
            }
            return obj;
        }

        std::cout << "did not find cache at " << cache.string() << ", regenerating and creating file...\n";
    } else if (is_regular_file(cache)) {
        std::cout << "ignoring cache at " << cache.string() << ", --clean was specified\n";
    }

    dur_t elapsed;
    T obj;
    if constexpr (sizeof...(Is) == 0) {
        const auto start = clock::now();
        obj = function();
        elapsed = std::chrono::duration_cast<dur_t>(clock::now() - start);
    } else {
        auto prev = get_cached_pipeline(clean, std::move(std::get<Is>(stages))...);

        const auto start = clock::now();
        obj = function(std::move(prev));
        elapsed = std::chrono::duration_cast<dur_t>(clock::now() - start);
    }
    std::cout << "computed stage in " << format_elapsed(elapsed) << '\n';


    create_directories(cache.parent_path());
    const auto start = clock::now();
    if (glz::write_file_json(obj, cache.string(), std::string{})) {
        std::cerr << "write error: " << cache.string() << std::endl;
    } else {
        elapsed = std::chrono::duration_cast<dur_t>(clock::now() - start);
        std::cout << "created cache at " << cache.string() << " in " << format_elapsed(elapsed) << '\n';
    }
    return obj;
}

template<typename ...Stages, typename T = staged_func_result<typename Stages::second_type...>::type>
T get_cached_pipeline(bool clean, Stages ...stages) {
    return get_cached_pipeline_impl(clean, std::make_tuple(std::forward<Stages>(stages)...), std::make_index_sequence<sizeof...(Stages) - 1>{});
}


auto get_all_images(std::filesystem::path out, std::vector<ParsedManga> data) {
    {
        int zip_error;
        zip_t* archive = zip_openwitherror(
            (out / "info.zip").string().c_str(), 
            ZIP_DEFAULT_COMPRESSION_LEVEL, 'w', &zip_error
        );
        assert(!zip_error);
        /* info.zip
         *   info/
         *     01.json - ParsedManga{}
         *     02.json - ParsedManga{}
         *     ...
         *   images/
         *     01.json - std::vector<ImageData>
         *     02.json - std::vector<ImageData>
         *     ...
         *   info      - std::map<std::string (manga path), std::tuple<std::string, std::string> (info/XX.json, images/XX.json)>
         */
        
        std::map<std::string, std::tuple<std::string, std::string>> directory;
        for (std::size_t i = 0; i < data.size(); ++i) {
            std::cout << std::format("{} / {} | {}\r", i, data.size(), data[i].name);

            const std::string im_dest = std::format("images/{:06d}.json", i);
            const std::string info_dest = std::format("info/{:06d}.json", i);
            {
                const auto images = get_image_data(data[i]);
                const std::string text = glz::write_json(images).value();

                zip_entry_open(archive, im_dest.c_str());
                zip_entry_write(archive, text.c_str(), text.size());
                zip_entry_close(archive);
            }
            {
                const std::string text = glz::write_json(data[i]).value();

                zip_entry_open(archive, info_dest.c_str());
                zip_entry_write(archive, text.c_str(), text.size());
                zip_entry_close(archive);
            }

            directory[data[i].path] = std::make_tuple(info_dest, im_dest);
        }
        {
            const std::string text = glz::write_json(directory).value();
            zip_entry_open(archive, "info.json");
            zip_entry_write(archive, text.c_str(), text.size());
            zip_entry_close(archive);
        }
        zip_close(archive);
    }

    auto min_info = data | std::views::transform([](const auto& info) {
        return MangaHeader {info.path, info.name, info.errs};
    });

    return std::ranges::to<std::vector<MangaHeader>>(min_info);
}


int main(int argc, char** argv) {
    vips_init(argv[0]);

    argparse::ArgumentParser parser;
    parser.add_argument("root").help("the library root path to be scanned");
    parser.add_argument("-c", "--clean").help("rebuild cache from scratch")
        .flag();
    parser.add_argument("-o", "--out").help("where to put the created files")
        .required();

    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    std::string root = parser.get<std::string>("root");
    const std::string out_string = parser.get<std::string>("--out");

    root = std::filesystem::absolute(root).string();
    const auto out = std::filesystem::absolute(out_string);
#if defined(_WIN32) || defined(_WIN64)
    root = R"(\\?\)" + root;
#endif

    get_cached_pipeline(
        parser.get<bool>("--clean"),
        std::make_pair(out / "scan.json", [&] { return scan_library(root); }),
        std::make_pair(out / "read.json", read_library),
        std::make_pair(out / "parsed.json", parse_library),
        std::make_pair(out / "parsed_checked.json", [](auto parsed) { 
            check_for_errors(parsed);
            std::map<std::string, std::size_t, std::less<>> counts;
            for (auto& m : parsed) {
                for (auto& e : m.errs.items) {
                    counts[std::string(get_error_tag(e))] += 1;
                }
            }

            std::string cr;
            (void) glz::write<glz::opts{ .prettify = true }>(counts, cr);
            std::cout << cr << '\n';
            return parsed;
        }),
        std::make_pair(out / "manga.json", [out](auto p) {
            return get_all_images(out, std::move(p));
        })
    );
}
