#include "../types.h"

#include <re2/re2.h>


Page Page::parse(std::string_view repr) {
    const static RE2 pat(R"(p?(\d+)(?:\D(\d+))?)");
    Page p;
    RE2::FullMatch(repr, pat, &p.number, &p.part);
    return p;
}
std::string Page::repr() const {
    if (number == static_cast<std::size_t>(-1)) {
        return "cover";
    }
    if (part) {
        return std::format("p{:03d}x{}", number, *part);
    }
    return std::format("p{:03d}", number);
}

Chapter Chapter::parse(std::string_view repr) {
    const static RE2 pat0(R"(c?(\d+)([A-Z]))");
    const static RE2 pat1(R"(c?(\d+))");
    const static RE2 pat2(R"(.+?#(\d+))");
    const static RE2 pat3(R"(.+?[\.x](\d+))");
    Chapter c;
    if (std::string part; RE2::FullMatch(repr, pat0, &c.number, &part)) {
        c.sub_part = part[0] - 'A' + 1;
    } else {
        RE2::PartialMatch(repr, pat1, &c.number);
        RE2::PartialMatch(repr, pat2, &c.sub_part);
        RE2::PartialMatch(repr, pat3, &c.extra_number);
    }
    return c;
}
std::string Chapter::repr() const {
    std::string out = std::format("c{:03d}", number);
    if (extra_number) {
        out += std::format(".{}", *extra_number);
    }
    if (sub_part) {
        out += std::format("#{}", *sub_part);
    }
    return out;
}

Volume Volume::parse(std::string_view repr) {
    const static RE2 pat(R"(v?(\d+)(?:\D(\d+))?)");
    Volume v;
    RE2::FullMatch(repr, pat, &v.number, &v.part);
    return v;
}
std::string Volume::repr() const {
    if (part) {
        return std::format("v{:02d}.{}", number, *part);
    }
    return std::format("v{:03d}", number);
}


template<typename T>
Range<T> Range<T>::single(T start) {
    return { start, start };
}

template<typename T>
Range<T> Range<T>::parse(std::string_view first, std::string_view second) {
    if (second.empty()) {
        return parse(first);
    }
    return { T::parse(first), T::parse(second) };
}
template<typename T>
Range<T> Range<T>::parse(std::string_view first) {
    T start = T::parse(first);
    return { start, start };
}
template<typename T>
std::string Range<T>::repr() const {
    if (start == end) {
        return start.repr();
    }
    return std::format("{}-{}", start.repr(), end.repr());
}

std::string Position::repr() const {
    if (chapters && volume) {
        return std::format("{} {}", volume->repr(), chapters->repr());
    }
    if (chapters) {
        return chapters->repr();
    }
    if (volume) {
        return volume->repr();
    }
    return "unknown";
}

template struct Range<Page>;
template struct Range<Chapter>;
template struct Range<Volume>;
