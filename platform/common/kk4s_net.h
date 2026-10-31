// kk4s network helpers (libcurl): HTTPS requests and the VNDB cover search.
#pragma once

#include <string>
#include <vector>

namespace kk4s {
namespace net {

// GET (post_json == nullptr) or POST a JSON body. True on HTTP 200; the body
// is in out. err gets a short reason on failure.
bool Request(const std::string& url, const std::string* post_json, std::string& out, std::string& err);

struct CoverCandidate {
    std::string title;      // romanized title
    std::string alt_title;  // original script title (may be empty)
    std::string released;   // "YYYY-MM-DD" or ""
    std::string image_url;  // full cover
    std::string thumb_url;  // small preview
};

// Visual novels on VNDB matching query, best first (whole-word title matches
// before fuzzy ones). hide_explicit skips covers VNDB marks as sexual.
bool SearchVndbCovers(const std::string& query, bool hide_explicit, std::vector<CoverCandidate>& out,
                      std::string& err);

// Search words from a game title / folder name: no [tags], (notes), versions,
// language or "patch" words; separators become spaces.
// "Tsukihime Plus+Disc Russian" -> "Tsukihime Plus Disc".
std::string CleanQuery(const std::string& title);

// SearchVndbCovers on CleanQuery(title); with no results, retries with the
// last word dropped. used_query gets the query that produced the results.
bool SearchCoversForTitle(const std::string& title, bool hide_explicit, std::vector<CoverCandidate>& out,
                          std::string& used_query, std::string& err);

// True when the best match contains the query as whole words (safe to take
// without asking, e.g. "download all covers").
bool IsConfidentMatch(const std::string& query, const CoverCandidate& c);

} // namespace net
} // namespace kk4s
