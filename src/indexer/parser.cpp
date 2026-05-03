#include "../types.h"

#include <zip.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <iostream>
#include <ranges>
#include <utility>
#include <filesystem>


namespace fs = std::filesystem;

std::vector<UnscannedManga> scan_library(std::string_view root) {
    std::map<std::string, std::vector<std::string>, std::less<>> found;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;

        const fs::path& path = entry.path();
        if (path.extension() == ".cbz") {
            found[path.parent_path().string()].emplace_back(path.string());
        }
    }

    std::vector<UnscannedManga> out;
    for (auto& [manga, archives] : found) {
        out.emplace_back(std::move(manga), std::move(archives));
    }
    return out;
}


std::vector<UnparsedManga> read_library(std::vector<UnscannedManga> db) {
    static constexpr std::array exts = {
        ".csv", ".mat", ".raw", ".v", ".vips", ".gif", 
        ".dz", ".szi", ".png", ".jpg", ".jpeg", ".jpe", 
        ".jfif", ".webp", ".tif", ".tiff", ".heic", ".heif", ".avif"
    };

    std::vector<UnparsedManga> out;
    for (auto& m : db) {
        auto& parsed = out.emplace_back(m.path);

        for (const std::string_view archive_path : m.archives) {
            auto& archive = parsed.archives.emplace_back(UnparsedManga::Archive { 
                std::string(archive_path), {}, {}, {}
            });

            int zip_error;
            zip_t* zip = zip_openwitherror(archive_path.data(), 0, 'r', &zip_error);

            if (zip_error) {
                parsed.errs.add(BadArchiveError{
                    .path = std::string(archive_path), .message = zip_strerror(zip_error) 
                }, archive.errors);
                continue;
            }
            
            int n_entries = zip_entries_total(zip);
            for (int i = 0; i < n_entries; ++i) {
                const int err = zip_entry_openbyindex(zip, i);
                if (err) {
                    parsed.errs.add(BadArchiveEntryError{
                        .path = std::string(archive_path), .entry = i, .message = zip_strerror(err) 
                    }, archive.errors);
                    continue;
                }
                const std::string_view name = zip_entry_name(zip);
                const bool is_dir = !!zip_entry_isdir(zip);
                if (is_dir) {
                    archive.ignored.emplace_back(name);
                    zip_entry_close(zip);
                    continue;
                }

                bool is_image = false;
                for (const std::string_view ext : exts) {
                    if (name.ends_with(ext)) {
                        is_image = true;
                        break;
                    }
                }
                if (is_image) {
                    const std::size_t size = zip_entry_size(zip);
                    const std::uint32_t crc32 = zip_entry_crc32(zip);
                    parsed.images.emplace_back(UnparsedManga::ArchivedImage {
                        std::string(archive_path), "", std::string(name), size, crc32
                    });
                    archive.images.emplace_back(parsed.images.size() - 1);
                } else {
                    archive.ignored.emplace_back(name);
                }
                zip_entry_close(zip);
            }

            zip_close(zip);

            const auto get_first_folder = [](const std::string_view path) {
                const std::size_t i = path.find_first_of("\\/");
                return i == std::string_view::npos ? std::string_view{} : path.substr(0, i);
            };
            std::string_view root;
            for (bool first = true; const std::size_t i : archive.images) {
                const std::string_view name = parsed.images[i].filename;

                if (std::exchange(first, false)) {
                    root = get_first_folder(name);
                    if (root.empty()) break;
                }
                if (root != get_first_folder(name)) {
                    root = std::string_view{};
                    break;
                }
            }
            for (const std::size_t i : archive.images) {
                auto& im = parsed.images[i];
                const std::size_t end = im.filename.size();
                std::size_t start = 0;
                if (!root.empty()) {
                    start += 1 + root.size();
                }
                if (const auto top = get_first_folder({im.filename.data() + start, end - start}); !top.empty()) {
                    start += top.size();
                } else if (start) {
                    --start;
                }
                im.top_level_folder = im.filename.substr(0, start);
            }
        }
    }
    return out;
}

/*
chap_discs = r"[x\.#]"
chap_p = rf"(?:c(\d+)[\-~]c?(\d+)(?!{chap_discs}\d)|c(\d+(?:(?:{chap_discs}\d+)*|[A-C]))(?!\d))"
nchap_p = rf"(?:c?(\d+)[\-~]c?(\d+)(?!{chap_discs}\d)|c?(\d+(?:(?:{chap_discs}\d+)*|[A-C]))(?!\d))"

vol_pref = r"(?:[vV](?:ol\.?)?)"
volume_discs = r"[\.+]"
vol_p = rf"(?:{vol_pref}(\d+(?:{volume_discs}\d+)*))(?!\d)"

page_discs = r"[x\.#]"
page_tail = rf"(?:(?:{page_discs}\d+)(?!\d)(?!{page_discs}\d))"
page_pref = r"(?:page[_\-]?|p|i[_\-])"

spread_no_tail_p = rf"(\d+)[\-~]{page_pref}?(\d+)(?!\d)(?!{page_tail})"
page_or_spread_tail_p = rf"(\d+{page_tail}?)(?:[\-~]{page_pref}?(%s{page_tail}))?"
page_p = rf"(?:{spread_no_tail_p}|{page_or_spread_tail_p})"

just_page_p = rf"^{page_pref}?{page_p}\.[a-z]+$" % r"\3"
full_info_p = rf"{chap_p}(?: .*?(?:\({vol_p}\))?)? - {page_pref}{page_p}" % r"\7"
no_prefix_chap_p = rf"{nchap_p}(?: .*?\({vol_p}\)|.+?)? - {page_pref}{page_p}" % r"\7"

no_chap_p = rf"\(?{vol_p}\)? (?:- )?{page_pref}{page_p}" % r"\5"

foldered_p = rf"(?:(?:Vol(?:ume |\. ?)(\d+(?:{volume_discs}\d+)*)(?!\d)(?: |$))?(?:Ch(?:apter |\. ?){nchap_p})?)"

vol_page_p = rf"^{vol_p}_{page_pref}?{page_p}\.[a-z]+$" % r"\4"

comick_p = rf"^(\d+)-[a-zA-Z0-9\-_]{{13}}\.[a-z]+$"

cover_p = rf"^cover\.[a-z]+$"

hyphen = ".+?- "
nohyphen = ".+? "

for name, *p in (
    ("nchap_p", nchap_p), 
    ("vol_p", vol_p),
    ("just_page_p", just_page_p), 
    ("full_info_p", hyphen + full_info_p, hyphen + no_prefix_chap_p, nohyphen + full_info_p, nohyphen + no_prefix_chap_p),
    ("no_chap_p", hyphen + no_chap_p, nohyphen + no_chap_p), 
    ("foldered_p", foldered_p), 
    ("vol_page_p", vol_page_p),
    ("comick_p", comick_p),
    ("cover_p", cover_p)
):
    pats = [(
                "make_regexp(R\"pat(^%s)pat\")" % r.removeprefix('^')
            ) for r in p]
    print(f"static const auto {name} = std::make_tuple({', '.join(pats)});")
*/

static const auto make_regexp = [](const std::string_view pat) {
    int ec;
    std::size_t eo;

    auto p = pcre2_compile((const unsigned char*) pat.data(), pat.size(), PCRE2_UTF, &ec, &eo, NULL);
    if (p == NULL) {
        std::array<unsigned char, 256> msg;
        pcre2_get_error_message(ec, msg.data(), 256);
        std::cerr << "error compiling regexp: " << (const char*) msg.data() << '\n';
        std::cerr << "pattern: " << pat << '\n';
        std::cerr << "offset:  " << eo << '\n';
    }
    auto md = pcre2_match_data_create_from_pattern(p, NULL);
    return std::make_tuple(std::string(pat), p, md);
};

static const auto nchap_p = std::make_tuple(make_regexp(R"pat(^(?:c?(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c?(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d)))pat"));
static const auto vol_p = std::make_tuple(make_regexp(R"pat(^(?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d))pat"));
static const auto just_page_p = std::make_tuple(make_regexp(R"pat(^(?:page[_\-]?|p|i[_\-])?(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\3(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?)\.[a-z]+$)pat"));
static const auto full_info_p = std::make_tuple(make_regexp(R"pat(^.+?- (?:c(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d))(?: .*?(?:\((?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\))?)? - (?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\7(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"), make_regexp(R"pat(^.+?- (?:c?(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c?(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d))(?: .*?\((?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\)|.+?)? - (?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\7(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"), make_regexp(R"pat(^.+? (?:c(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d))(?: .*?(?:\((?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\))?)? - (?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\7(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"), make_regexp(R"pat(^.+? (?:c?(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c?(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d))(?: .*?\((?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\)|.+?)? - (?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\7(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"));
static const auto no_chap_p = std::make_tuple(make_regexp(R"pat(^.+?- \(?(?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\)? (?:- )?(?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\5(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"), make_regexp(R"pat(^.+? \(?(?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)\)? (?:- )?(?:page[_\-]?|p|i[_\-])(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\5(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?))pat"));
static const auto foldered_p = std::make_tuple(make_regexp(R"pat(^(?:(?:Vol(?:ume |\. ?)(\d+(?:[\.+]\d+)*)(?!\d)(?: |$))?(?:Ch(?:apter |\. ?)(?:c?(\d+)[\-~]c?(\d+)(?![x\.#]\d)|c?(\d+(?:(?:[x\.#]\d+)*|[A-C]))(?!\d)))?))pat"));
static const auto vol_page_p = std::make_tuple(make_regexp(R"pat(^(?:(?:[vV](?:ol\.?)?)(\d+(?:[\.+]\d+)*))(?!\d)_(?:page[_\-]?|p|i[_\-])?(?:(\d+)[\-~](?:page[_\-]?|p|i[_\-])?(\d+)(?!\d)(?!(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d)))|(\d+(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))?)(?:[\-~](?:page[_\-]?|p|i[_\-])?(\4(?:(?:[x\.#]\d+)(?!\d)(?![x\.#]\d))))?)\.[a-z]+$)pat"));
static const auto comick_p = std::make_tuple(make_regexp(R"pat(^(\d+)-[a-zA-Z0-9\-_]{13}\.[a-z]+$)pat"));
static const auto cover_p = std::make_tuple(make_regexp(R"pat(^cover\.[a-z]+$)pat"));

template<typename ...Pats, typename ...T, std::size_t ...Is>
bool try_pattern_list_impl(const std::tuple<Pats...>& patterns, std::string_view text, std::tuple<T*...> args, std::index_sequence<Is...>) {
    return std::apply([&](const auto& ...pats) {
        return (([&](const auto& arg) {
            const auto& [pattern, pcre2_pat, md] = arg;
            if (pcre2_pat == nullptr) {
                return false;
            }
            if (auto result = pcre2_match(pcre2_pat, (const unsigned char*) text.data(), 
                        text.size(), 0, {}, md, NULL); result > 0) {
                (([&] {
                    std::size_t ml;
                    std::string* ptr = std::get<Is>(args);
                    ptr->clear();
                    if (!pcre2_substring_length_bynumber(md, Is + 1, &ml)) {
                        ptr->resize_and_overwrite(ml, [&](char* p, std::size_t cnt) {
                            cnt += 1;
                            if (pcre2_substring_copy_bynumber(md, Is + 1, (unsigned char*) p, &cnt)) {
                                cnt = 0;
                            }
                            return cnt;
                        });
                    }
                })(), ...);
                return true;
            } else if (result != PCRE2_ERROR_NOMATCH) {
                std::array<unsigned char, 256> msg;
                pcre2_get_error_message(result, &msg[0], msg.size());
                std::cout << "pcre2 error when matching \"" << text << "\" against:\n";
                std::cout << pattern << '\n';
                std::cout << (const char*) msg.data() << '\n';
            }
            return false;
        })(pats) || ...);
    }, patterns);
}

template<typename ...Pats, typename ...T>
bool try_pattern_list(const std::tuple<Pats...>& patterns, std::string_view text, T* ...args) {
    return try_pattern_list_impl(patterns, text, std::make_tuple(args...), std::make_index_sequence<sizeof...(T)>{});
}


ParsedManga::Image parse(ParsedManga& manga, UnparsedManga::ArchivedImage& im, 
        const std::string_view prefix, const std::string_view suffix,
        const std::string_view fpre, const std::string_view fsuf
    ) {
    std::vector<std::size_t> errors;

    std::string_view collection_name = im.archive_path;
    collection_name.remove_prefix(prefix.size());
    collection_name.remove_suffix(suffix.size());
    while (!collection_name.empty() && collection_name.front() == ' ') {
        collection_name.remove_prefix(1);
    }

    std::string_view basename = im.filename;
    if (const auto sep = basename.find_last_of("\\/"); sep != std::string_view::npos) {
        basename.remove_prefix(sep + 1);
    }

    std::optional<Range<Page>> page;
    std::optional<Range<Chapter>> chapters;
    std::optional<Volume> volume;

    std::array<std::string, 4> page_parts;
    std::array<std::string, 3> chapter_parts;
    std::array<std::string, 1> volume_parts;

    const auto check_and_set_field = [&] (const std::string_view field, auto& exist, auto&& replace) {
        if (!exist) exist = replace;
        else if (exist != replace) {
            manga.errs.add(MismatchedFieldError {
                std::string(field), std::string(im.archive_path), std::string(im.filename), 
                std::string(prefix), std::string(suffix), exist->repr(), replace.repr()
            }, errors);
        }
    };
    const auto parse_pages = [&] {
        if (page_parts[0].empty()) {
            check_and_set_field("page", page, Range<Page>::parse(page_parts[2], page_parts[3]));
        } else {
            check_and_set_field("page", page, Range<Page>::parse(page_parts[0], page_parts[1]));
        }
        for (auto& p : page_parts) p.clear();
    };
    const auto parse_chapters = [&] {
        if (chapter_parts[0].empty()) {
            check_and_set_field("chapter", chapters, Range<Chapter>::parse(chapter_parts[2]));
        } else {
            check_and_set_field("chapter", chapters, Range<Chapter>::parse(chapter_parts[0], chapter_parts[1]));
        }
        for (auto& p : chapter_parts) p.clear();
    };
    const auto parse_volume = [&] {
        check_and_set_field("volume", volume, Volume::parse(volume_parts[0]));
        for (auto& p : volume_parts) p.clear();
    };

#define PAGE_ARGS &page_parts[0], &page_parts[1], &page_parts[2], &page_parts[3]
#define CHAP_ARGS &chapter_parts[0], &chapter_parts[1], &chapter_parts[2]
#define VOL_ARGS &volume_parts[0]

    if (try_pattern_list(nchap_p, collection_name, CHAP_ARGS)) {
        parse_chapters();
    }
    if (try_pattern_list(vol_p, collection_name, VOL_ARGS)) {
        parse_volume();
    }

    std::string_view folder_prefix = fpre;
    while (true) {
        std::string_view inner_folder = im.top_level_folder;
        inner_folder.remove_prefix(folder_prefix.size());

        if (try_pattern_list(foldered_p, inner_folder, VOL_ARGS, CHAP_ARGS)) {
            if (!chapter_parts[0].empty()) parse_chapters();
            if (!volume_parts[0].empty()) parse_volume();
            break;
        }

        if (folder_prefix.empty()) break;

        if (folder_prefix.size() > 1 && std::string_view{"\\/"}.contains(folder_prefix.back())) {
            folder_prefix.remove_suffix(1);
        }
        const std::size_t i = folder_prefix.find_last_of("\\/");
        folder_prefix = folder_prefix.substr(0, i == std::string_view::npos ? 0 : i + 1);
    }

    if (try_pattern_list(just_page_p, basename, PAGE_ARGS)) {
        parse_pages();
    } else if (try_pattern_list(vol_page_p, basename, VOL_ARGS, PAGE_ARGS)) {
        parse_volume();
        parse_pages();
    } else if (try_pattern_list(full_info_p, basename, CHAP_ARGS, VOL_ARGS, PAGE_ARGS)) {
        parse_chapters();
        if (!volume_parts[0].empty()) parse_volume();
        parse_pages();
    } else if (try_pattern_list(no_chap_p, basename, VOL_ARGS, PAGE_ARGS)) {
        parse_volume();
        parse_pages();
    } else if (try_pattern_list(comick_p, basename, &page_parts[2])) {
        parse_pages();
    } else if (try_pattern_list(cover_p, basename)) {
        check_and_set_field("page", page, Range<Page>::single(Page{ static_cast<std::size_t>(-1) }));
    }

    if (!volume && !chapters) {
        manga.errs.add(UnknownImagePositionError {
            std::string(im.archive_path), std::string(im.filename), std::string(prefix), std::string(suffix)
        }, errors);
    }
    if (!page) {
        manga.errs.add(UnknownPageIndexError {
            std::string(im.archive_path), std::string(im.filename), std::string(prefix), std::string(suffix)
        }, errors);
    }

    return ParsedManga::Image {
        std::move(im.archive_path),
        std::move(im.top_level_folder),
        std::move(im.filename),
        im.file_size,
        im.crc32,

        Position { std::move(chapters), std::move(volume) },
        std::move(page),
        std::move(errors),
    };
}

std::string_view get_common_prefix(std::string_view a, std::string_view b) {
    const std::size_t m = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < m; ++i) {
        if (a[i] != b[i]) {
            return {a.data(), i};
        }
    }
    return {a.data(), m};
}
std::string_view get_common_suffix(std::string_view a, std::string_view b) {
    const std::size_t m = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < m; ++i) {
        if (a[a.size() - i - 1] != b[b.size() - i - 1]) {
            return {a.data() + a.size() - i, i};
        }
    }
    return {a.data() + a.size() - m, m};
}


std::vector<ParsedManga> parse_library(std::vector<UnparsedManga> db) {
    std::vector<ParsedManga> out;
    for (auto& m : db) {
        const std::size_t name_start = m.path.find_last_of("\\/");
        const std::string name = name_start == std::string::npos ? m.path : m.path.substr(name_start + 1);
        auto& parsed = out.emplace_back(ParsedManga {
            std::move(m.path), name,
            {}, {}, {},
            std::move(m.errs)
        });
        
        std::string prefix, suffix;
        for (bool first = true; auto& archive : m.archives) {
            if (std::exchange(first, false)) {
                prefix = archive.path;
                suffix = archive.path;
            } else {
                prefix = get_common_prefix(prefix, archive.path);
                suffix = get_common_suffix(suffix, archive.path);
            }

            std::string name_copy = archive.path;
            parsed.archives.emplace(std::move(name_copy), ParsedManga::Archive {
                std::move(archive.path), std::move(archive.images), 
                std::move(archive.ignored), std::move(archive.errors)
            });
        }
        
        if (m.archives.size() == 1) {
            prefix = "";
        } else if (const auto last_boundary = prefix.rfind(' '); last_boundary != std::string_view::npos) {
            prefix = prefix.substr(0, last_boundary + 1);
        } else {
            prefix = "";
        }

        parsed.images.resize(m.images.size());
        for (auto& [_, archive] : parsed.archives) {
            std::string fpre, fsuf; 
            for (bool first = true; const std::size_t i : archive.images) {
                auto& im = m.images[i];
                if (std::exchange(first, false)) {
                    fpre = im.top_level_folder;
                    fsuf = im.top_level_folder;
                } else {
                    fpre = get_common_prefix(fpre, im.top_level_folder);
                    fsuf = get_common_suffix(fsuf, im.top_level_folder);
                }
            }
            
            if (fpre == fsuf) {
                fsuf = "";
            } else if (const auto last_boundary = fpre.find_last_of(" \\/"); last_boundary != std::string_view::npos) {
                fpre = fpre.substr(0, last_boundary + 1);
            } else {
                fpre = "";
            }

            for (const std::size_t i : archive.images) {
                auto image = parse(parsed, m.images[i], prefix, suffix, fpre, fsuf);

#define FILE_ERROR_ARGS std::string(image.archive_path), std::string(image.filename)
                if (image.pages && image.pages->start > image.pages->end) {
                    parsed.errs.add(ZeroLengthPageRangeError{ FILE_ERROR_ARGS }, image.errors);
                    image.pages = std::nullopt;
                }

                const auto& pos = image.position;
                if (pos.chapters && !pos.chapters->is_single()) {
                    const auto& c0 = pos.chapters->start;
                    const auto& c1 = pos.chapters->end;
                    if (c0.sub_part || c0.extra_number) {
                        parsed.errs.add(DiscriminatedChapterRangeError{ FILE_ERROR_ARGS }, image.errors);
                    } else if (c0 > c1) {
                        parsed.errs.add(ZeroLengthChapterRangeError{ FILE_ERROR_ARGS }, image.errors);
                        image.position.chapters = std::nullopt;
                    }
                }

                const auto ckey = image.position.repr();
                auto& inner_map = parsed.collections[ckey];
                if (const auto it = inner_map.find(image.archive_path); it == inner_map.end()) {
                    inner_map.emplace(image.archive_path, ParsedManga::Collection{image.position, {}, {}});
                }
                inner_map[image.archive_path].images.emplace_back(i);
                parsed.images[i] = std::move(image);
            }
        }

        const auto cmp_page_unknown_last = [&parsed](const std::size_t i, const std::size_t j) {
            const auto& a = parsed.images[i].pages;
            const auto& b = parsed.images[j].pages;
            if (a && b) return *a < *b;
            return a.has_value();
        };
        for (auto& [archive_path, archive] : parsed.archives) {
            std::ranges::sort(archive.images, [&](const std::size_t i, const std::size_t j) {
                const std::string_view a = parsed.images[i].top_level_folder;
                const std::string_view b = parsed.images[j].top_level_folder;
                if (a != b) {
                    return a < b;
                }
                return cmp_page_unknown_last(i, j);
            });
        }
        for (auto& [ckey, archives] : parsed.collections) {
            for (auto& [archive_path, coll] : archives) {
                std::ranges::sort(coll.images, cmp_page_unknown_last);
            }
        }
    }
    return out;
}
