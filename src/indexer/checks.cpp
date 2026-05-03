#include "../types.h"

#include <ranges>
#include <algorithm>


bool is_consecutive(const Page& a, const Page& b) {
    if (!b.part) {
        return a.number + 1 == b.number;
    }
    return a.number == b.number && (!a.part || *a.part + 1 == *b.part);
}


void check_for_errors(ParsedManga& manga) {
    const auto check_if_consecutive_pages = [&]<typename A, typename B>(A, B, auto& archive, const auto& ...info) {
        for (std::size_t i = 1; i < archive.images.size(); ++i) {
            auto& first = manga.images[archive.images[i - 1]];
            auto& second = manga.images[archive.images[i]];
            auto& a = first.pages;
            auto& b = second.pages;
            if (!a || !b || first.top_level_folder != second.top_level_folder) continue;

            if (a->is_single() != b->is_single() && a->end == b->start) {
                continue;
            }
            if (a->end >= b->start) {
                manga.errs.add(A {
                    info..., first.filename, second.filename
                }, archive.errors);
            } else if (!is_consecutive(a->end, b->start)) {
                manga.errs.add(B {
                    info..., first.filename, second.filename
                }, archive.errors);
            }
        }
    };

    for (auto& [archive_path, archive] : manga.archives) {
        auto& images = archive.images;
        if (!images.size()) {
            manga.errs.add(EmptyArchiveError {archive.path}, archive.errors);
        }
        check_if_consecutive_pages(
            ArchiveHasOverlappingPagesError{}, ArchiveHasMissingPagesError{},
            archive, std::string(archive_path)
        );
    }

    for (auto& [ckey, archives] : manga.collections) {
        if (archives.size() != 1) {
            manga.errs.add(DuplicatedCollectionError {
                manga.path, ckey, 
                archives | 
                    std::views::transform([](auto& p) { return std::make_pair(p.first, p.second.images.size()); }) |
                    std::ranges::to<std::map<std::string, std::size_t>>()
            });
        }

        for (auto& [archive_path, coll] : archives) {
            check_if_consecutive_pages(
                CollectionHasOverlappingPagesError{}, CollectionHasMissingPagesError{},
                coll, std::string(archive_path), manga.path, ckey
            );
        }
    }
}

void check_for_errors(std::vector<ParsedManga>& db) {
    for (auto& m : db) {
        check_for_errors(m);
    }
}
