// kk4s network helpers: HTTPS via libcurl and the VNDB cover search used by
// the launcher. VNDB (https://vndb.org) is an open visual novel database with
// a free JSON API (https://api.vndb.org/kana), no key needed.
#include "kk4s_net.h"
#include "kk4s_platform.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif

namespace kk4s {
namespace net {

static size_t WriteCb(char* p, size_t size, size_t n, void* user) {
    std::string* out = static_cast<std::string*>(user);
    if (out->size() + size * n > (16u << 20)) return 0; // 16 MiB cap
    out->append(p, size * n);
    return size * n;
}

bool Request(const std::string& url, const std::string* post_json, std::string& out, std::string& err) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    out.clear();
    CURL* c = curl_easy_init();
    if (!c) {
        err = "curl_easy_init failed";
        return false;
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "kk4s/" KK4S_VERSION " (+https://github.com/navik677/kk4s)");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 40L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    struct curl_slist* headers = nullptr;
    if (post_json) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post_json->c_str());
    }
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (headers) curl_slist_free_all(headers);
    if (r != CURLE_OK) {
        err = curl_easy_strerror(r);
        return false;
    }
    if (code != 200) {
        err = "HTTP " + std::to_string(code);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// minimal JSON reader (enough for API responses)

namespace {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;                         // Array
    std::vector<std::pair<std::string, Json>> props; // Object

    const Json& operator[](const char* key) const {
        static const Json none;
        for (const auto& p : props)
            if (p.first == key) return p.second;
        return none;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool Parse(Json& out) {
        bool ok = Value(out, 0);
        Ws();
        return ok && i_ == s_.size();
    }

private:
    void Ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool Lit(const char* w) {
        size_t n = strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    static void Utf8(uint32_t cp, std::string& out) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(uint32_t& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool Str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    uint32_t cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        uint32_t lo;
                        if (!Hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    Utf8(cp, out);
                    break;
                }
                default: out += e; break;
            }
        }
        return false;
    }
    bool Value(Json& v, int depth) {
        if (depth > 64) return false;
        Ws();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') {
            v.type = Json::Object;
            ++i_;
            Ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (true) {
                Ws();
                std::string key;
                if (!Str(key)) return false;
                Ws();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                v.props.emplace_back(key, Json());
                if (!Value(v.props.back().second, depth + 1)) return false;
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.type = Json::Array;
            ++i_;
            Ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (true) {
                v.items.emplace_back();
                if (!Value(v.items.back(), depth + 1)) return false;
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = Json::String; return Str(v.str); }
        if (Lit("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (Lit("false")) { v.type = Json::Bool; return true; }
        if (Lit("null")) { v.type = Json::Null; return true; }
        char* end = nullptr;
        v.num = strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        v.type = Json::Number;
        i_ = end - s_.c_str();
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

std::string JsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += (char)c;
    }
    return out;
}

} // namespace

bool SearchVndbCovers(const std::string& query, std::vector<CoverCandidate>& out, std::string& err) {
    out.clear();
    std::string body = "{\"filters\":[\"search\",\"=\",\"" + JsonEscape(query) +
                       "\"],\"fields\":\"title,alttitle,released,image.url,image.thumbnail,image.sexual\","
                       "\"sort\":\"searchrank\",\"results\":12}";
    std::string resp;
    if (!Request("https://api.vndb.org/kana/vn", &body, resp, err)) return false;
    Json root;
    if (!Parser(resp).Parse(root)) {
        err = "bad JSON from VNDB";
        return false;
    }
    for (const Json& r : root["results"].items) {
        const Json& img = r["image"];
        if (img.type != Json::Object || img["url"].type != Json::String) continue;
        // Skip covers VNDB users voted as sexual (0 = safe, 1 = suggestive, 2 = explicit).
        if (img["sexual"].type == Json::Number && img["sexual"].num >= 1.0) continue;
        CoverCandidate c;
        c.title = r["title"].str;
        c.alt_title = r["alttitle"].str;
        c.released = r["released"].str;
        c.image_url = img["url"].str;
        c.thumb_url = img["thumbnail"].type == Json::String ? img["thumbnail"].str : c.image_url;
        out.push_back(c);
    }
    // VNDB's search is fuzzy ("Sharin" ranks "Sharing Is Caring" first): prefer
    // titles that contain the query as whole words, keep VNDB's order otherwise.
    auto lower = [](std::string s) {
        for (char& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    std::string q = lower(query);
    auto score = [&](const CoverCandidate& c) {
        int best = 0;
        for (const std::string* t : {&c.title, &c.alt_title}) {
            std::string s = lower(*t);
            for (size_t at = s.find(q); at != std::string::npos; at = s.find(q, at + 1)) {
                size_t end = at + q.size();
                bool left = at == 0 || !isalnum((unsigned char)s[at - 1]);
                bool right = end == s.size() || !isalnum((unsigned char)s[end]);
                if (left && right) best = std::max(best, at == 0 ? 2 : 1);
            }
        }
        return best;
    };
    std::stable_sort(out.begin(), out.end(),
                     [&](const CoverCandidate& a, const CoverCandidate& b) { return score(a) > score(b); });
    return true;
}

} // namespace net
} // namespace kk4s
