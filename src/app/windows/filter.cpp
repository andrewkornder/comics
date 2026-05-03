#include "../app.h"

#include <rapidfuzz/fuzz.hpp>

#include <imgui/imgui.h>

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <cstdlib>


struct SearchItem {
    std::string sanitized_text;
    std::vector<std::size_t> offsets;
    
    bool is_kept(const unsigned char c) {
        return c >= 0x80 || std::isalnum(c) || c == '_' || c == '-';
    }
    bool is_space(const unsigned char c) {
        return c < 0x80 && std::isspace(c);
    }

    explicit SearchItem(std::string_view text, bool end_boundary = true) {
        char last_c = -1;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = std::tolower(text[i]);
            const bool ws = is_space(c);
            if (i > 0 && ws && is_space(text[i - 1])) {
                continue;
            }

            if (!ws && !is_kept(c)) {
                continue;
            }
            if (last_c == -1 || (is_kept(c) ^ is_kept(last_c))) {
                offsets.push_back(i);
                sanitized_text.push_back('|');
            }
            last_c = c;

            offsets.push_back(i);
            sanitized_text.push_back(c);
        }
        if (end_boundary) {
            offsets.push_back(text.size());
            sanitized_text.push_back('|');
        }

        offsets.push_back(text.size());
    }
    SearchItem() = default;
};


struct AppMangaList::SearchBarBase {
    std::vector<SearchItem> candidates;
    std::vector<SearchResult> results;

    std::array<char, 256> filter_buf;
    SearchItem filter {};


    void reevaluate() {
        results.clear();
        if (!filter.sanitized_text.size()) {
            return;
        }

        for (std::size_t i = 0; const auto& choice : candidates) {
            const auto res = rapidfuzz::fuzz::partial_ratio_alignment(filter.sanitized_text, choice.sanitized_text);
            results.emplace_back(SearchResult{
                .index = i,
                .start = choice.offsets[res.dest_start],
                .end = choice.offsets[res.dest_end],
                .score = (float) res.score,
            });
            ++i;
        }
        
        std::ranges::sort(results, std::less{}, [query_length=filter.offsets.back()] (const SearchResult& res) {
            const std::ptrdiff_t match_length = (std::ptrdiff_t) (res.start - res.end);
            return std::make_tuple(
                -res.score, 
                std::abs(match_length - (std::ptrdiff_t) query_length),
                res.start
            );
        });
    }

    void add_candidate(std::string_view text) {
        candidates.emplace_back(text);
    }

    bool render_no_window(AppRoot&) {
        if (ImGui::InputText("##search", filter_buf.data(), filter_buf.size())) {
            filter = SearchItem(filter_buf.data(), false);
            reevaluate();
        }
        return true;
    }
    SearchResult at(std::size_t i) {
        if (results.empty()) return SearchResult { .index = i, .start = 0, .end = 0, .score = -1 };
        return results[i];
    }
};


AppMangaList::SearchBar::SearchBar() {
    base = std::make_unique<SearchBarBase>();
}
AppMangaList::SearchBar::~SearchBar() {}

void AppMangaList::SearchBar::add_candidate(std::string_view text) {
    return base ->add_candidate(text);
}
bool AppMangaList::SearchBar::render_no_window(AppRoot& root) {
    return base->render_no_window(root);
}
AppMangaList::SearchResult AppMangaList::SearchBar::at(const std::size_t i) {
    return base->at(i);
}
