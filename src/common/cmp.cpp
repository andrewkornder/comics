#include "../types.h"

#include <compare>


template<typename T>
std::strong_ordering cmp_optionals(const std::optional<T>& a, const std::optional<T>& b) {
    if (a) {
        return b ? *a <=> *b : std::strong_ordering::greater;
    }
    return b ? std::strong_ordering::less : std::strong_ordering::equivalent;
}



std::strong_ordering Page::operator<=>(const Page& other) const {
    if (number != other.number) {
        return number <=> other.number;
    }
    return cmp_optionals(part, other.part);
}

std::strong_ordering Chapter::operator<=>(const Chapter& other) const {
    if (number != other.number) {
        return number <=> other.number;
    }
    if (extra_number != other.extra_number) {
        return cmp_optionals(extra_number, other.extra_number);
    }
    return cmp_optionals(sub_part, other.sub_part);
}

std::strong_ordering Volume::operator<=>(const Volume& other) const {
    if (number != other.number) {
        return number <=> other.number;
    }
    return cmp_optionals(part, other.part);
}

template<typename T>
std::strong_ordering Range<T>::operator<=>(const Range& other) const {
    return start <=> other.start;
}

template struct Range<Page>;
template struct Range<Chapter>;
template struct Range<Volume>;

std::strong_ordering Position::operator<=>(const Position& other) const {
    if (!volume && !chapters) {
        return other.volume || other.chapters ? std::strong_ordering::less : std::strong_ordering::equal;
    }
    if (!other.volume && !other.chapters) {
        return std::strong_ordering::greater;
    }
    if (chapters.has_value() != other.chapters.has_value()) {
        return chapters ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (chapters == other.chapters) {
        if (volume) {
            return other.volume ? *volume <=> *other.volume : std::strong_ordering::less;
        }
        return other.volume ? std::strong_ordering::greater : std::strong_ordering::equal;
    }
    return *chapters <=> *other.chapters;
}
