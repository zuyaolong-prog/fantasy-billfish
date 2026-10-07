#pragma once

#include <string_view>
#include <tuple>
#include <vector>

namespace bf::core::db {

std::string_view main_schema_sql();
std::string_view summary_schema_sql();
std::string_view migration_40_to_41_sql();

const std::vector<std::tuple<std::string_view, std::string_view, int>>& type_seed();

} // namespace bf::core::db
