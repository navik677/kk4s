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

// Visual novels on VNDB matching query, best first, with a non-explicit cover.
bool SearchVndbCovers(const std::string& query, std::vector<CoverCandidate>& out, std::string& err);

} // namespace net
} // namespace kk4s
