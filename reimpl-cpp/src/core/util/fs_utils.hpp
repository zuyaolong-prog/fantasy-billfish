#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace bf::core::util {

std::int64_t now_seconds();
std::string to_lower(std::string value);
std::string extension_lower(const std::filesystem::path& path);
std::string file_hash_fnv1a(const std::filesystem::path& path);
std::string random_hex(std::size_t len);
std::filesystem::path dedup_path(const std::filesystem::path& target);

} // namespace bf::core::util
