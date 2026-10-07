#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/db/sqlite_runtime.hpp"

namespace bf::core::db {

class Statement {
public:
    Statement() = default;
    Statement(SqliteRuntime* runtime, sqlite3_stmt* stmt);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;

    int bind_text(int idx, std::string_view value);
    int bind_int64(int idx, std::int64_t value);
    int bind_int(int idx, int value);

    int step();
    void reset();

    [[nodiscard]] std::string column_text(int idx) const;
    [[nodiscard]] std::int64_t column_int64(int idx) const;
    [[nodiscard]] int column_int(int idx) const;

private:
    void release();

    SqliteRuntime* runtime_{nullptr};
    sqlite3_stmt* stmt_{nullptr};
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::filesystem::path& dbPath, std::string* error);
    void close();

    [[nodiscard]] bool is_open() const noexcept;

    bool exec(std::string_view sql, std::string* error);
    bool begin(std::string* error);
    bool commit(std::string* error);
    bool rollback(std::string* error);

    Statement prepare(std::string_view sql, std::string* error);

    [[nodiscard]] std::int64_t last_insert_rowid() const;
    [[nodiscard]] int changes() const;
    [[nodiscard]] std::string last_error() const;

    [[nodiscard]] sqlite3* raw() const noexcept;

private:
    SqliteRuntime* runtime_{nullptr};
    sqlite3* db_{nullptr};
};

} // namespace bf::core::db
