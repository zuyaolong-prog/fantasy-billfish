#include "core/db/sqlite_runtime.hpp"

#include <array>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bf::core::db {
namespace {

template <typename T>
T load_symbol(void* module, const char* name) {
#ifdef _WIN32
    const auto addr = GetProcAddress(static_cast<HMODULE>(module), name);
#else
    const auto addr = dlsym(module, name);
#endif
    return reinterpret_cast<T>(addr);
}

} // namespace

SqliteRuntime& SqliteRuntime::instance() {
    static SqliteRuntime rt;
    return rt;
}

SqliteRuntime::~SqliteRuntime() {
#ifdef _WIN32
    if (module_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
    }
#else
    if (module_ != nullptr) {
        dlclose(module_);
        module_ = nullptr;
    }
#endif
}

bool SqliteRuntime::load(std::string* error) {
    if (module_ != nullptr) {
        return true;
    }

#ifdef _WIN32
    HMODULE mod = LoadLibraryA("sqlite3.dll");
    if (mod == nullptr) {
        if (error != nullptr) {
            *error = "failed to load sqlite3.dll";
        }
        return false;
    }
    module_ = mod;
#else
    void* mod = dlopen("libsqlite3.so", RTLD_NOW);
    if (mod == nullptr) {
        if (error != nullptr) {
            *error = "failed to load libsqlite3.so";
        }
        return false;
    }
    module_ = mod;
#endif

    if (!bind_symbols(error)) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(module_));
#else
        dlclose(module_);
#endif
        module_ = nullptr;
        return false;
    }
    return true;
}

bool SqliteRuntime::loaded() const noexcept {
    return module_ != nullptr;
}

bool SqliteRuntime::bind_symbols(std::string* error) {
    open_v2_ = load_symbol<OpenV2Fn>(module_, "sqlite3_open_v2");
    close_ = load_symbol<CloseFn>(module_, "sqlite3_close");
    exec_ = load_symbol<ExecFn>(module_, "sqlite3_exec");
    errmsg_ = load_symbol<ErrMsgFn>(module_, "sqlite3_errmsg");
    prepare_v2_ = load_symbol<PrepareV2Fn>(module_, "sqlite3_prepare_v2");
    step_ = load_symbol<StepFn>(module_, "sqlite3_step");
    finalize_ = load_symbol<FinalizeFn>(module_, "sqlite3_finalize");
    reset_ = load_symbol<ResetFn>(module_, "sqlite3_reset");
    clear_bindings_ = load_symbol<ClearBindingsFn>(module_, "sqlite3_clear_bindings");
    bind_text_ = load_symbol<BindTextFn>(module_, "sqlite3_bind_text");
    bind_int64_ = load_symbol<BindInt64Fn>(module_, "sqlite3_bind_int64");
    bind_int_ = load_symbol<BindIntFn>(module_, "sqlite3_bind_int");
    column_text_ = load_symbol<ColumnTextFn>(module_, "sqlite3_column_text");
    column_int64_ = load_symbol<ColumnInt64Fn>(module_, "sqlite3_column_int64");
    column_int_ = load_symbol<ColumnIntFn>(module_, "sqlite3_column_int");
    column_count_ = load_symbol<ColumnCountFn>(module_, "sqlite3_column_count");
    last_insert_rowid_ = load_symbol<LastInsertRowIdFn>(module_, "sqlite3_last_insert_rowid");
    changes_ = load_symbol<ChangesFn>(module_, "sqlite3_changes");

    const bool ok = open_v2_ != nullptr && close_ != nullptr && exec_ != nullptr && errmsg_ != nullptr &&
                    prepare_v2_ != nullptr && step_ != nullptr && finalize_ != nullptr && reset_ != nullptr &&
                    clear_bindings_ != nullptr && bind_text_ != nullptr && bind_int64_ != nullptr &&
                    bind_int_ != nullptr && column_text_ != nullptr && column_int64_ != nullptr &&
                    column_int_ != nullptr && column_count_ != nullptr && last_insert_rowid_ != nullptr &&
                    changes_ != nullptr;
    if (!ok && error != nullptr) {
        *error = "sqlite3 symbols are incomplete";
    }
    return ok;
}

int SqliteRuntime::open_v2(const char* filename, sqlite3** ppDb, const int flags, const char* zVfs) const {
    return open_v2_(filename, ppDb, flags, zVfs);
}

int SqliteRuntime::close(sqlite3* db) const {
    return close_(db);
}

int SqliteRuntime::exec(sqlite3* db, const char* sql, void* callback, void* arg, char** errmsg) const {
    return exec_(db, sql, callback, arg, errmsg);
}

const char* SqliteRuntime::errmsg(sqlite3* db) const {
    return errmsg_(db);
}

int SqliteRuntime::prepare_v2(sqlite3* db, const char* sql, const int nByte, sqlite3_stmt** stmt, const char** tail) const {
    return prepare_v2_(db, sql, nByte, stmt, tail);
}

int SqliteRuntime::step(sqlite3_stmt* stmt) const {
    return step_(stmt);
}

int SqliteRuntime::finalize(sqlite3_stmt* stmt) const {
    return finalize_(stmt);
}

int SqliteRuntime::reset(sqlite3_stmt* stmt) const {
    return reset_(stmt);
}

int SqliteRuntime::clear_bindings(sqlite3_stmt* stmt) const {
    return clear_bindings_(stmt);
}

int SqliteRuntime::bind_text(sqlite3_stmt* stmt, const int idx, const char* value, const int n, void* dtor) const {
    return bind_text_(stmt, idx, value, n, dtor);
}

int SqliteRuntime::bind_int64(sqlite3_stmt* stmt, const int idx, const std::int64_t value) const {
    return bind_int64_(stmt, idx, value);
}

int SqliteRuntime::bind_int(sqlite3_stmt* stmt, const int idx, const int value) const {
    return bind_int_(stmt, idx, value);
}

const unsigned char* SqliteRuntime::column_text(sqlite3_stmt* stmt, const int iCol) const {
    return column_text_(stmt, iCol);
}

std::int64_t SqliteRuntime::column_int64(sqlite3_stmt* stmt, const int iCol) const {
    return column_int64_(stmt, iCol);
}

int SqliteRuntime::column_int(sqlite3_stmt* stmt, const int iCol) const {
    return column_int_(stmt, iCol);
}

int SqliteRuntime::column_count(sqlite3_stmt* stmt) const {
    return column_count_(stmt);
}

std::int64_t SqliteRuntime::last_insert_rowid(sqlite3* db) const {
    return last_insert_rowid_(db);
}

int SqliteRuntime::changes(sqlite3* db) const {
    return changes_(db);
}

} // namespace bf::core::db
