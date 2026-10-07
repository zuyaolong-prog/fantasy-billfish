#pragma once

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace bf::core::db {

constexpr int kSqliteOk = 0;
constexpr int kSqliteRow = 100;
constexpr int kSqliteDone = 101;

constexpr int kSqliteOpenReadOnly = 0x00000001;
constexpr int kSqliteOpenReadWrite = 0x00000002;
constexpr int kSqliteOpenCreate = 0x00000004;
constexpr int kSqliteOpenUri = 0x00000040;
constexpr int kSqliteOpenFullMutex = 0x00010000;

class SqliteRuntime {
public:
    static SqliteRuntime& instance();

    bool load(std::string* error);
    bool loaded() const noexcept;

    int open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs) const;
    int close(sqlite3* db) const;
    int exec(sqlite3* db, const char* sql, void* callback = nullptr, void* arg = nullptr, char** errmsg = nullptr) const;
    const char* errmsg(sqlite3* db) const;
    int prepare_v2(sqlite3* db, const char* sql, int nByte, sqlite3_stmt** stmt, const char** tail) const;
    int step(sqlite3_stmt* stmt) const;
    int finalize(sqlite3_stmt* stmt) const;
    int reset(sqlite3_stmt* stmt) const;
    int clear_bindings(sqlite3_stmt* stmt) const;

    int bind_text(sqlite3_stmt* stmt, int idx, const char* value, int n, void* dtor) const;
    int bind_int64(sqlite3_stmt* stmt, int idx, std::int64_t value) const;
    int bind_int(sqlite3_stmt* stmt, int idx, int value) const;

    const unsigned char* column_text(sqlite3_stmt* stmt, int iCol) const;
    std::int64_t column_int64(sqlite3_stmt* stmt, int iCol) const;
    int column_int(sqlite3_stmt* stmt, int iCol) const;
    int column_count(sqlite3_stmt* stmt) const;

    std::int64_t last_insert_rowid(sqlite3* db) const;
    int changes(sqlite3* db) const;

private:
    SqliteRuntime() = default;
    ~SqliteRuntime();

    SqliteRuntime(const SqliteRuntime&) = delete;
    SqliteRuntime& operator=(const SqliteRuntime&) = delete;

    bool bind_symbols(std::string* error);

#ifdef _WIN32
    void* module_{nullptr};
#else
    void* module_{nullptr};
#endif

    using OpenV2Fn = int (*)(const char*, sqlite3**, int, const char*);
    using CloseFn = int (*)(sqlite3*);
    using ExecFn = int (*)(sqlite3*, const char*, void*, void*, char**);
    using ErrMsgFn = const char* (*)(sqlite3*);
    using PrepareV2Fn = int (*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
    using StepFn = int (*)(sqlite3_stmt*);
    using FinalizeFn = int (*)(sqlite3_stmt*);
    using ResetFn = int (*)(sqlite3_stmt*);
    using ClearBindingsFn = int (*)(sqlite3_stmt*);
    using BindTextFn = int (*)(sqlite3_stmt*, int, const char*, int, void*);
    using BindInt64Fn = int (*)(sqlite3_stmt*, int, std::int64_t);
    using BindIntFn = int (*)(sqlite3_stmt*, int, int);
    using ColumnTextFn = const unsigned char* (*)(sqlite3_stmt*, int);
    using ColumnInt64Fn = std::int64_t (*)(sqlite3_stmt*, int);
    using ColumnIntFn = int (*)(sqlite3_stmt*, int);
    using ColumnCountFn = int (*)(sqlite3_stmt*);
    using LastInsertRowIdFn = std::int64_t (*)(sqlite3*);
    using ChangesFn = int (*)(sqlite3*);

    OpenV2Fn open_v2_{nullptr};
    CloseFn close_{nullptr};
    ExecFn exec_{nullptr};
    ErrMsgFn errmsg_{nullptr};
    PrepareV2Fn prepare_v2_{nullptr};
    StepFn step_{nullptr};
    FinalizeFn finalize_{nullptr};
    ResetFn reset_{nullptr};
    ClearBindingsFn clear_bindings_{nullptr};
    BindTextFn bind_text_{nullptr};
    BindInt64Fn bind_int64_{nullptr};
    BindIntFn bind_int_{nullptr};
    ColumnTextFn column_text_{nullptr};
    ColumnInt64Fn column_int64_{nullptr};
    ColumnIntFn column_int_{nullptr};
    ColumnCountFn column_count_{nullptr};
    LastInsertRowIdFn last_insert_rowid_{nullptr};
    ChangesFn changes_{nullptr};
};

} // namespace bf::core::db
