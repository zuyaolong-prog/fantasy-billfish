#include "core/db/database.hpp"

#include <utility>

namespace bf::core::db {
namespace {

std::string to_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

} // namespace

Statement::Statement(SqliteRuntime* runtime, sqlite3_stmt* stmt) : runtime_(runtime), stmt_(stmt) {}

Statement::~Statement() {
    release();
}

Statement::Statement(Statement&& other) noexcept : runtime_(other.runtime_), stmt_(other.stmt_) {
    other.runtime_ = nullptr;
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        release();
        runtime_ = other.runtime_;
        stmt_ = other.stmt_;
        other.runtime_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

bool Statement::valid() const noexcept {
    return runtime_ != nullptr && stmt_ != nullptr;
}

int Statement::bind_text(const int idx, const std::string_view value) {
    if (!valid()) {
        return -1;
    }
    void* transient = reinterpret_cast<void*>(-1);
    return runtime_->bind_text(stmt_, idx, value.data(), static_cast<int>(value.size()), transient);
}

int Statement::bind_int64(const int idx, const std::int64_t value) {
    if (!valid()) {
        return -1;
    }
    return runtime_->bind_int64(stmt_, idx, value);
}

int Statement::bind_int(const int idx, const int value) {
    if (!valid()) {
        return -1;
    }
    return runtime_->bind_int(stmt_, idx, value);
}

int Statement::step() {
    if (!valid()) {
        return -1;
    }
    return runtime_->step(stmt_);
}

void Statement::reset() {
    if (!valid()) {
        return;
    }
    runtime_->reset(stmt_);
    runtime_->clear_bindings(stmt_);
}

std::string Statement::column_text(const int idx) const {
    if (!valid()) {
        return {};
    }
    const auto p = runtime_->column_text(stmt_, idx);
    if (p == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(p));
}

std::int64_t Statement::column_int64(const int idx) const {
    if (!valid()) {
        return 0;
    }
    return runtime_->column_int64(stmt_, idx);
}

int Statement::column_int(const int idx) const {
    if (!valid()) {
        return 0;
    }
    return runtime_->column_int(stmt_, idx);
}

void Statement::release() {
    if (runtime_ != nullptr && stmt_ != nullptr) {
        runtime_->finalize(stmt_);
        stmt_ = nullptr;
    }
}

Database::Database() : runtime_(&SqliteRuntime::instance()) {}

Database::~Database() {
    close();
}

bool Database::open(const std::filesystem::path& dbPath, std::string* error) {
    if (db_ != nullptr) {
        close();
    }

    if (!runtime_->load(error)) {
        return false;
    }

    const auto utf8Path = to_utf8(dbPath);
    constexpr int flags = kSqliteOpenReadWrite | kSqliteOpenCreate | kSqliteOpenUri | kSqliteOpenFullMutex;
    const int rc = runtime_->open_v2(utf8Path.c_str(), &db_, flags, nullptr);
    if (rc != kSqliteOk) {
        if (error != nullptr) {
            *error = "open sqlite failed";
            if (db_ != nullptr) {
                *error += ": " + last_error();
            }
        }
        return false;
    }
    return true;
}

void Database::close() {
    if (db_ != nullptr) {
        runtime_->close(db_);
        db_ = nullptr;
    }
}

bool Database::is_open() const noexcept {
    return db_ != nullptr;
}

bool Database::exec(const std::string_view sql, std::string* error) {
    if (db_ == nullptr) {
        if (error != nullptr) {
            *error = "database not opened";
        }
        return false;
    }

    int rc = runtime_->exec(db_, std::string(sql).c_str(), nullptr, nullptr, nullptr);
    if (rc != kSqliteOk) {
        if (error != nullptr) {
            *error = last_error();
        }
        return false;
    }
    return true;
}

bool Database::begin(std::string* error) {
    return exec("BEGIN", error);
}

bool Database::commit(std::string* error) {
    return exec("COMMIT", error);
}

bool Database::rollback(std::string* error) {
    return exec("ROLLBACK", error);
}

Statement Database::prepare(const std::string_view sql, std::string* error) {
    if (db_ == nullptr) {
        if (error != nullptr) {
            *error = "database not opened";
        }
        return {};
    }

    sqlite3_stmt* stmt{nullptr};
    const auto rc = runtime_->prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr);
    if (rc != kSqliteOk) {
        if (error != nullptr) {
            *error = last_error();
        }
        return {};
    }
    return Statement{runtime_, stmt};
}

std::int64_t Database::last_insert_rowid() const {
    if (db_ == nullptr) {
        return 0;
    }
    return runtime_->last_insert_rowid(db_);
}

int Database::changes() const {
    if (db_ == nullptr) {
        return 0;
    }
    return runtime_->changes(db_);
}

std::string Database::last_error() const {
    if (db_ == nullptr) {
        return "db is null";
    }
    const char* e = runtime_->errmsg(db_);
    return e == nullptr ? std::string{} : std::string(e);
}

sqlite3* Database::raw() const noexcept {
    return db_;
}

} // namespace bf::core::db
