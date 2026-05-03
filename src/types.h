#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


using cmp_t = std::less<>;
// using cmp_t = std::less<std::string>;


struct BadArchiveError {
    std::string path, message;
    std::string tag {"BadArchiveError"}; 
};
struct BadArchiveEntryError {
    std::string path; int entry; std::string message;
    std::string tag {"BadArchiveEntryError"}; 
};
struct BadArchiveImageReadError {
    std::string path, filename, message;
    std::string tag {"BadArchiveImageReadError"}; 
};
struct MismatchedFieldError {
    std::string field, archive_path, filename, prefix, suffix, existing, replacement;
    std::string tag {"MismatchedFieldError"}; 
};
struct UnknownImagePositionError {
    std::string archive_path, filename, prefix, suffix;
    std::string tag {"UnknownImagePositionError"}; 
};
struct UnknownPageIndexError {
    std::string archive_path, filename, prefix, suffix;
    std::string tag {"UnknownPageIndexError"}; 
};
struct ZeroLengthPageRangeError {
    std::string archive_path, filename;
    std::string tag {"ZeroLengthPageRangeError"}; 
};
struct DiscriminatedChapterRangeError {
    std::string archive_path, filename;
    std::string tag {"DiscriminatedChapterRangeError"}; 
};
struct ZeroLengthChapterRangeError {
    std::string archive_path, filename;
    std::string tag {"ZeroLengthChapterRangeError"}; 
};
struct DuplicatedCollectionError {
    std::string manga, what; std::map<std::string, std::size_t> where;
    std::string tag {"DuplicatedCollectionError"}; 
};
struct ArchiveHasOverlappingPagesError {
    std::string archive_path, first, second;
    std::string tag {"ArchiveHasOverlappingPagesError"}; 
};
struct ArchiveHasMissingPagesError {
    std::string archive_path, first, second;
    std::string tag {"ArchiveHasMissingPagesError"}; 
};
struct CollectionHasOverlappingPagesError {
    std::string archive_path, manga, what, first, second;
    std::string tag {"CollectionHasOverlappingPagesError"}; 
};
struct CollectionHasMissingPagesError {
    std::string archive_path, manga, what, first, second;
    std::string tag {"CollectionHasMissingPagesError"}; 
};
struct EmptyArchiveError {
    std::string path;
    std::string tag {"EmptyArchiveError"}; 
};
struct ImageLoadFailedError {
    std::string path, filename, message;
    std::string tag {"ImageLoadFailedError"}; 
};


namespace err {
    constexpr static std::array names = {
        "BadArchiveError",
        "BadArchiveEntryError",
        "BadArchiveImageReadError",
        "MismatchedFieldError",
        "UnknownImagePositionError",
        "UnknownPageIndexError",
        "ZeroLengthPageRangeError",
        "DiscriminatedChapterRangeError",
        "ZeroLengthChapterRangeError",
        "DuplicatedCollectionError",
        "ArchiveHasOverlappingPagesError",
        "ArchiveHasMissingPagesError",
        "CollectionHasOverlappingPagesError",
        "CollectionHasMissingPagesError",
        "EmptyArchiveError",
        "ImageLoadFailedError"
    };
    constexpr static std::size_t count = names.size();
}


using Error = std::variant<
    BadArchiveError,
    BadArchiveEntryError,
    BadArchiveImageReadError,
    MismatchedFieldError,
    UnknownImagePositionError,
    UnknownPageIndexError,
    ZeroLengthPageRangeError,
    DiscriminatedChapterRangeError,
    ZeroLengthChapterRangeError,
    DuplicatedCollectionError,
    ArchiveHasOverlappingPagesError,
    ArchiveHasMissingPagesError,
    CollectionHasOverlappingPagesError,
    CollectionHasMissingPagesError,
    EmptyArchiveError,
    ImageLoadFailedError
>;


std::string format_error(const Error&); 
std::string_view get_error_tag(const Error&);

struct ErrorList {
    std::map<std::string, std::size_t, cmp_t> counts;
    std::vector<Error> items;

    template<typename ...T>
    std::size_t add(Error err, T& ...children) {
        std::visit([this](const auto& e) {
            counts[e.tag] += 1;
        }, err);
        counts[""] += 1;

        items.emplace_back(std::move(err));
        const std::size_t n = items.size() - 1;
        (children.emplace_back(n), ...);
        return n;
    }
};


struct Page {
    std::size_t number;
    std::optional<std::size_t> part;

    static Page parse(std::string_view);
    std::string repr() const;

    bool operator==(const Page&) const = default;
    std::strong_ordering operator<=>(const Page&) const;
};

struct Chapter {
    std::size_t number;
    std::optional<std::size_t> sub_part, extra_number;

    static Chapter parse(std::string_view);
    std::string repr() const;
    bool operator==(const Chapter&) const = default;
    std::strong_ordering operator<=>(const Chapter&) const;
};

struct Volume {
    std::size_t number;
    std::optional<std::size_t> part;

    static Volume parse(std::string_view);
    std::string repr() const;
    bool operator==(const Volume&) const = default;
    std::strong_ordering operator<=>(const Volume&) const;
};

template<typename T>
struct Range {
    T start, end;

    static Range<T> single(T);
    static Range<T> parse(std::string_view, std::string_view);
    static Range<T> parse(std::string_view);
    std::string repr() const;
    bool operator==(const Range&) const = default;
    std::strong_ordering operator<=>(const Range&) const;

    bool is_single() const { return start == end; }
};

struct Position {
    std::optional<Range<Chapter>> chapters;
    std::optional<Volume> volume;

    std::string repr() const;
    bool operator==(const Position&) const = default;
    std::strong_ordering operator<=>(const Position&) const;
};


struct MetadataBlob {
    std::vector<unsigned char> bytes;
};

using metadata_item_t = std::variant<
    int, 
    double,
    std::vector<int>,
    std::vector<double>,
    std::string
>;


struct UnscannedManga {
    std::string path;
    std::vector<std::string> archives;
};


struct UnparsedManga {
    struct ArchivedImage {
        std::string archive_path;
        std::string top_level_folder;
        std::string filename;

        std::size_t file_size;
        std::uint32_t crc32;
    };
    struct Archive {
        std::string path;

        std::vector<std::size_t> images;
        std::vector<std::string> ignored;
        std::vector<std::size_t> errors;
    };

    std::string path;
    std::vector<ArchivedImage> images;
    std::vector<Archive> archives;
    ErrorList errs;
};


struct ParsedManga {
    struct Image {
        std::string archive_path;
        std::string top_level_folder;
        std::string filename;

        std::size_t file_size;
        std::uint32_t crc32;

        Position position;
        std::optional<Range<Page>> pages;

        std::vector<std::size_t> errors;
    };
    struct ImageData {
        std::size_t width, height, bands;
        std::map<std::string, metadata_item_t, cmp_t> metadata;
    };
    struct Archive {
        std::string path;

        std::vector<std::size_t> images;
        std::vector<std::string> ignored;
        std::vector<std::size_t> errors;
    };
    struct Collection {
        Position position;

        std::vector<std::size_t> images;
        std::vector<std::size_t> errors;
    };

    std::string path, name;
    std::vector<Image> images;
    std::map<std::string, Archive, cmp_t> archives;
    std::map<std::string, 
        std::map<std::string, Collection, cmp_t>,
    cmp_t> collections;

    ErrorList errs;
};


std::vector<UnscannedManga> scan_library(std::string_view root);
std::vector<UnparsedManga> read_library(std::vector<UnscannedManga> db);
std::vector<ParsedManga> parse_library(std::vector<UnparsedManga> db);

void check_for_errors(std::vector<ParsedManga>& db);

std::vector<ParsedManga::ImageData> get_image_data(ParsedManga& db);

void vips_init(std::string_view argv0);

struct MangaHeader {
    std::string path;
    std::string name;
    ErrorList errs;
};
using Manga = ParsedManga;
using File = ParsedManga::Image;
using FileInfo = ParsedManga::ImageData;
using FileInfoList = std::vector<ParsedManga::ImageData>;
