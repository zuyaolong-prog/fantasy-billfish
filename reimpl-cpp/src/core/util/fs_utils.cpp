#include "core/util/fs_utils.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace bf::core::util {

std::int64_t now_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string to_lower(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return value;
}

std::string extension_lower(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return to_lower(ext);
}

std::string file_hash_fnv1a(const std::filesystem::path& path) {
    constexpr std::uint64_t fnvOffset = 1469598103934665603ULL;
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }

    std::uint64_t hash = fnvOffset;
    std::array<char, 64 * 1024> buffer{};

    while (ifs.good()) {
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = ifs.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= fnvPrime;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::string random_hex(const std::size_t len) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(len);
    std::uniform_int_distribution<int> dist(0, 15);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kHex[static_cast<std::size_t>(dist(rng))]);
    }
    return out;
}

std::filesystem::path dedup_path(const std::filesystem::path& target) {
    if (!std::filesystem::exists(target)) {
        return target;
    }

    const auto stem = target.stem().string();
    const auto ext = target.extension().string();
    auto parent = target.parent_path();

    int idx = 1;
    while (true) {
        const auto candidate = parent / (stem + "_" + std::to_string(idx) + ext);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
        ++idx;
    }
}

} // namespace bf::core::util
