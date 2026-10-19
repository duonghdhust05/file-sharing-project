#ifndef FSP_AUTH_UTIL_H
#define FSP_AUTH_UTIL_H

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

#if defined(__linux__)
#include <sys/random.h>
#endif

// Helpers shared by the M2 auth modules: clock, random hex, flat-JSON read/write, atomic file save.
namespace FSP::Auth {

using Clock = std::function<std::time_t()>;
using JsonMap = std::map<std::string, std::string>;

inline std::time_t systemClock() {
    return std::time(nullptr);
}

/*
 * Fills `buf` with OS random bytes; throws on failure, never falls back to a weak source.
 * Linux: getrandom(2). Elsewhere: std::random_device.
 * ponytail: MinGW before GCC 9.2 has a deterministic random_device, use BCryptGenRandom there.
 */
inline void randomBytes(unsigned char* buf, size_t n) {
#if defined(__linux__)
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::getrandom(buf + got, n - got, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            throw std::runtime_error("getrandom failed");
        got += static_cast<size_t>(r);
    }
#else
    std::random_device rd;
    for (size_t i = 0; i < n; ++i)
        buf[i] = static_cast<unsigned char>(rd() & 0xFF);
#endif
}

inline std::string randomHex(size_t nBytes) {
    std::string raw(nBytes, '\0');
    randomBytes(reinterpret_cast<unsigned char*>(raw.data()), nBytes);
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(nBytes * 2);
    for (char c : raw) {
        auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4];
        out += digits[b & 0xF];
    }
    return out;
}

inline std::string jsonEscape(const std::string& s) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (char ch : s) {
        auto c = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20) {
            out += "\\u00";
            out += digits[c >> 4];
            out += digits[c & 0xF];
        } else {
            out += ch;
        }
    }
    return out;
}

inline bool parseU64(const std::string& s, uint64_t& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

/* Parses one flat JSON object (no nesting); numbers stay as text. nullopt if malformed. */
inline std::optional<JsonMap> jsonParseFlat(const std::string& s) {
    size_t i = 0;
    JsonMap out;
    auto ws = [&] {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
    };
    auto str = [&](std::string& o) -> bool {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i >= s.size())
                return false;
            char e = s[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    o += e;
                    break;
                case 'n':
                    o += '\n';
                    break;
                case 't':
                    o += '\t';
                    break;
                case 'r':
                    o += '\r';
                    break;
                case 'b':
                    o += '\b';
                    break;
                case 'f':
                    o += '\f';
                    break;
                case 'u': {
                    if (i + 4 > s.size())
                        return false;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')
                            cp |= h - '0';
                        else if (h >= 'a' && h <= 'f')
                            cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F')
                            cp |= h - 'A' + 10;
                        else
                            return false;
                    }
                    // ponytail: surrogate pairs are encoded unpaired, fine for usernames/passwords.
                    if (cp < 0x80) {
                        o += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        o += static_cast<char>(0xC0 | (cp >> 6));
                        o += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        o += static_cast<char>(0xE0 | (cp >> 12));
                        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        o += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    };

    ws();
    if (i >= s.size() || s[i++] != '{')
        return std::nullopt;
    ws();
    if (i < s.size() && s[i] == '}') {
        ++i;
    } else {
        while (true) {
            ws();
            std::string k, v;
            if (!str(k))
                return std::nullopt;
            ws();
            if (i >= s.size() || s[i++] != ':')
                return std::nullopt;
            ws();
            if (i < s.size() && s[i] == '"') {
                if (!str(v))
                    return std::nullopt;
            } else {
                size_t st = i;
                while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ' ' && s[i] != '\t' &&
                       s[i] != '\r' && s[i] != '\n')
                    ++i;
                v = s.substr(st, i - st);
                if (v.empty() || v[0] == '{' || v[0] == '[')
                    return std::nullopt;
            }
            out[k] = v;
            ws();
            if (i >= s.size())
                return std::nullopt;
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                break;
            }
            return std::nullopt;
        }
    }
    ws();
    if (i != s.size())
        return std::nullopt;
    return out;
}

// Field accessors for loading persisted files: throw so a corrupt DB never loads silently.
inline const std::string& jsonNeed(const JsonMap& m, const std::string& key) {
    auto it = m.find(key);
    if (it == m.end())
        throw std::runtime_error("missing field: " + key);
    return it->second;
}

inline uint64_t jsonNeedU64(const JsonMap& m, const std::string& key) {
    uint64_t v = 0;
    if (!parseU64(jsonNeed(m, key), v))
        throw std::runtime_error("bad number in field: " + key);
    return v;
}

// Writes to <path>.tmp then renames over <path>, so a crash never leaves a half-written DB.
inline bool writeFileAtomic(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path())
        fs::create_directories(p.parent_path(), ec);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << content;
        f.flush();
        if (!f)
            return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

}  // namespace FSP::Auth

#endif  // FSP_AUTH_UTIL_H
