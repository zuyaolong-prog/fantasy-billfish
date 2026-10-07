#include "core/util/text_utils.hpp"

#include <cctype>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace bf::core::util {
namespace {

bool is_cjk_codepoint(const char32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF);
}

std::vector<char32_t> utf8_to_u32(const std::string& s) {
    std::vector<char32_t> out;
    out.reserve(s.size());

    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            ++i;
            continue;
        }
        if ((c >> 5U) == 0x6U && i + 1 < s.size()) {
            char32_t cp = ((c & 0x1FU) << 6U) | (static_cast<unsigned char>(s[i + 1]) & 0x3FU);
            out.push_back(cp);
            i += 2;
            continue;
        }
        if ((c >> 4U) == 0xEU && i + 2 < s.size()) {
            char32_t cp = ((c & 0x0FU) << 12U) |
                          ((static_cast<unsigned char>(s[i + 1]) & 0x3FU) << 6U) |
                          (static_cast<unsigned char>(s[i + 2]) & 0x3FU);
            out.push_back(cp);
            i += 3;
            continue;
        }
        if ((c >> 3U) == 0x1EU && i + 3 < s.size()) {
            char32_t cp = ((c & 0x07U) << 18U) |
                          ((static_cast<unsigned char>(s[i + 1]) & 0x3FU) << 12U) |
                          ((static_cast<unsigned char>(s[i + 2]) & 0x3FU) << 6U) |
                          (static_cast<unsigned char>(s[i + 3]) & 0x3FU);
            out.push_back(cp);
            i += 4;
            continue;
        }

        // Skip malformed byte.
        ++i;
    }
    return out;
}

std::string u32_to_utf8(const std::vector<char32_t>& in) {
    std::string out;
    for (const auto cp : in) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6U) & 0x1FU)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12U) & 0x0FU)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18U) & 0x07U)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
        }
    }
    return out;
}

} // namespace

std::string normalize_for_search(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (unsigned char c : text) {
        if (std::isalnum(c) != 0) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    const auto cps = utf8_to_u32(text);
    std::vector<char32_t> run;
    for (auto cp : cps) {
        if (is_cjk_codepoint(cp)) {
            run.push_back(cp);
        } else {
            if (!run.empty()) {
                if (run.size() == 1) {
                    tokens.push_back(u32_to_utf8(run));
                } else {
                    for (std::size_t i = 0; i + 1 < run.size(); ++i) {
                        std::vector<char32_t> bi{run[i], run[i + 1]};
                        tokens.push_back(u32_to_utf8(bi));
                    }
                }
                run.clear();
            }
        }
    }
    if (!run.empty()) {
        if (run.size() == 1) {
            tokens.push_back(u32_to_utf8(run));
        } else {
            for (std::size_t i = 0; i + 1 < run.size(); ++i) {
                std::vector<char32_t> bi{run[i], run[i + 1]};
                tokens.push_back(u32_to_utf8(bi));
            }
        }
    }

    std::unordered_set<std::string> seen;
    std::ostringstream oss;
    bool first = true;
    for (const auto& token : tokens) {
        if (token.empty() || seen.contains(token)) {
            continue;
        }
        seen.insert(token);
        if (!first) {
            oss << ' ';
        }
        oss << token;
        first = false;
    }

    return oss.str();
}

} // namespace bf::core::util
