#include "types.h"

#include <glaze/glaze.hpp>


template<>
struct glz::meta<Error> {
    static constexpr std::string_view tag = "tag";
    static constexpr auto ids = err::names;
};

template<>
struct glz::meta<ErrorList> {
    static constexpr auto value = object(
        "items", &ErrorList::items,
        "counts", &ErrorList::counts
    );
};

template<>
struct glz::meta<metadata_item_t> {
    static constexpr std::string_view tag = "type";
    static constexpr auto ids = std::array{
        "int", "double", "array_int", "array_double", "string"
    }; 
};

