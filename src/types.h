#ifndef TYPES_H
#define TYPES_H

#include <glaze/glaze.hpp>

#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <vector>


template<typename IndexType>
struct Range {
    IndexType start, end;
};


using Errors = std::unordered_map<std::string, std::vector<glz::generic>>;


struct Page {
    std::int64_t number;
    std::optional<std::int64_t> part;
};

struct Chapter {
    std::int64_t number;
    std::optional<std::int64_t> sub_part, extra_number;
};

struct Volume {
    std::int64_t number;
    std::optional<std::int64_t> part;
};

struct File {
    std::string file, group, collection;

    std::array<std::size_t, 3> extent;

    std::optional<Range<Page>> pages;
    std::optional<Range<Chapter>> chapters;
    std::optional<Volume> volume;

    Errors errors;
};


template<typename T>
struct Collection {
    Range<T> indices;
    std::string location;
    Range<Page> pages;
    std::vector<std::size_t> page_indices;
};


struct Manga {
    std::string name;
    std::vector<File> files;
    std::vector<Collection<Chapter>> chapters;
    std::vector<Collection<Volume>> volumes;

    Errors errors;
};

#endif
