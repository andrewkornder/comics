#include "../types.h"

#include <ranges>
#include <format>
#include <string>


std::string format_error(const Error& data) {
    try {
        switch (data.index()) {
            case 0: { 
                const auto& [path, msg, tag] = std::get<BadArchiveError>(data);
                return std::format(
                    "Failed to open archive at {}\n"
                    "  Reason: {}",
                    path, msg
                );
            }
            case 1: { 
                const auto& [path, entry, msg, tag] = std::get<BadArchiveEntryError>(data);
                return std::format(
                    "Failed to open archive entry at {}\n"
                    "  Entry index: {}\n"
                    "  Reason:      {}",
                    path, entry, msg
                );
            }
            case 2: { 
                const auto& [path, file, msg, tag] = std::get<BadArchiveImageReadError>(data);
                return std::format(
                    "Failed to open archive entry to get image metadata at {}\n"
                    "  Entry:  {}\n"
                    "  Reason: {}",
                    path, file, msg
                );
            }
            case 3: {
                const auto& [field, archive, name, pre, suf, before, after, tag] = std::get<MismatchedFieldError>(data);
                return std::format(
                    "Parsing error: Mismatched data found in multiple places in file info\n"
                    "  File:    {}\n"
                    "  Archive: {}\n"
                    "  Field:   {} -> {} != {}",
                    name, archive, field, before, after
                );
            }
            case 4: {
                const auto& [archive, name, pre, suf, tag] = std::get<UnknownImagePositionError>(data);
                return std::format(
                    "Parsing error: Couldn't find chapter or volume information in image name\n"
                    "  File:    {}\n"
                    "  Archive: {}",
                    name, archive
                );
            }
            case 5: {
                const auto& [archive, name, pre, suf, tag] = std::get<UnknownPageIndexError>(data);
                return std::format(
                    "Parsing error: Couldn't find page information in image name\n"
                    "  File:    {}\n"
                    "  Archive: {}",
                    name, archive
                );
            }
            case 6: {
                const auto& [archive, name, tag] = std::get<ZeroLengthPageRangeError>(data);
                return std::format(
                    "Multi-page spread is mislabeled: The ending page number is less than the beginning page number\n"
                    "  File:    {}\n"
                    "  Archive: {}",
                    name, archive
                );
            }
            case 7: {
                const auto& [archive, name, tag] = std::get<DiscriminatedChapterRangeError>(data);
                return std::format(
                    "Parsing error: Page with a range of chapters in name cannot have discriminators (c01#2, c04x1, etc.)\n"
                    "               (this is because you can't reliably parse which chapters these correspond to- e.g. c01x2-c01#2)\n"
                    "  File:     {}\n"
                    "  Archive:  {}",
                    name, archive
                );
            }
            case 8: {
                const auto& [archive, name, tag] = std::get<ZeroLengthChapterRangeError>(data);
                return std::format(
                    "Page spanning multiple chapters is mislabeled: The ending chapter number is less than the beginning chapter number\n"
                    "  File:     {}\n"
                    "  Archive:  {}",
                    name, archive
                );
            }
            case 9: {
                const auto& [manga, what, where, tag] = std::get<DuplicatedCollectionError>(data);
                std::string seq;
                for (const auto& path : where | std::views::keys) {
                    seq += "\n  - ";
                    seq += path;
                }
                return std::format(
                    "Images labeled with this chapter or volume appeared in multiple archives (if intentional, this might not be an error)\n"
                    "  Manga:    {}\n"
                    "  What:     {}\n"
                    "  Archives: {}",
                    manga, what, seq
                );
            }
            case 10: {
                const auto& [archive, first, second, tag] = std::get<ArchiveHasOverlappingPagesError>(data);
                return std::format(
                    "Archive seems to have overlaps in page numbers (are there multiple pages with the same number?)\n"
                    "  Archive: {}\n"
                    "  Overlap: {} and {}",
                    archive, first, second
                );
            }
            case 11: {
                const auto& [archive, first, second, tag] = std::get<ArchiveHasMissingPagesError>(data);
                return std::format(
                    "Archive seems to have gaps between page numbers (are there missing pages?)\n"
                    "  Archive: {}\n"
                    "  Gap:     {} and {}",
                    archive, first, second
                );
            }
            case 12: {
                const auto& [archive, manga, what, first, second, tag] = std::get<CollectionHasOverlappingPagesError>(data);
                return std::format(
                    "Chapter/volume seems to have overlaps in page numbers (are there multiple pages with the same number?)\n"
                    "  Manga:   {}\n"
                    "  Archive: {}\n"
                    "  Label:   {}\n"
                    "  Overlap: {} and {}",
                    manga, archive, what, first, second
                );
            }
            case 13: {
                const auto& [archive, manga, what, first, second, tag] = std::get<CollectionHasMissingPagesError>(data);
                return std::format(
                    "Chapter/volume seems to have gaps between page numbers (are there missing pages?)\n"
                    "  Manga:   {}\n"
                    "  Archive: {}\n"
                    "  Label:   {}\n"
                    "  Gap:     {} and {}",
                    manga, archive, what, first, second
                );
            }
            case 14: {
                const auto& [where, tag] = std::get<EmptyArchiveError>(data);
                return std::format(
                    "Archive did not have any pages (or all of them failed to parse)\n"
                    "  Archive: {}",
                    where
                );
            }
            case 15: {
                const auto& [archive, name, message, tag] = std::get<ImageLoadFailedError>(data);
                return std::format(
                    "Image couldn't be read (file or archive possibly corrupted)\n"
                    "  File:    {}\n"
                    "  Archive: {}\n",
                    "  Message: {}",
                    name, archive, message
                );
            }
            default: {
                throw std::runtime_error("unknown error type");
            }
        }
    } catch (const std::bad_variant_access&) {
        return "error type isn't handled";
    } catch (const std::exception& err) {
        return err.what();
    }
}


std::string_view get_error_tag(const Error& err) {
    return err::names[err.index()];
}
