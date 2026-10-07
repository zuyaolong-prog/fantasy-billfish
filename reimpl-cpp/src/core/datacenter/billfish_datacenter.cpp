#include "core/datacenter/billfish_datacenter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string_view>

#include "core/db/schema.hpp"
#include "core/util/fs_utils.hpp"
#include "core/util/text_utils.hpp"

namespace bf::core::datacenter {
namespace {

api::FileItem read_file_item(db::Statement& stmt) {
    api::FileItem item;
    item.id = stmt.column_int64(0);
    item.pid = stmt.column_int64(1);
    item.name = stmt.column_text(2);
    item.extName = stmt.column_text(3);
    item.path = stmt.column_text(4);
    item.size = stmt.column_int64(5);
    item.hash = stmt.column_text(6);
    item.isRecycle = stmt.column_int(7) != 0;
    return item;
}

} // namespace

api::GenericResponse BillfishDataCenter::createLibrary(const std::string& path, const std::string& name) {
    if (path.empty() || name.empty()) {
        return {.info = api::make_err(api::err::InvalidArgument, "path/name is empty")};
    }

    const auto root = std::filesystem::path(path);
    const auto bfDir = root / ".bf";
    try {
        std::filesystem::create_directories(bfDir / ".preview");
        std::filesystem::create_directories(bfDir / ".recycle");
        std::filesystem::create_directories(bfDir / ".temp");
        std::filesystem::create_directories(bfDir / ".ui_config");
    } catch (const std::exception& e) {
        return {.info = api::make_err(api::err::Io, e.what())};
    }

    std::string error;
    if (!bootstrap_main_db(bfDir, name, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!bootstrap_summary_db(bfDir, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok("library created")};
}

api::OpenLibraryResponse BillfishDataCenter::openLibrary(const std::string& path) {
    if (opened_) {
        return {.info = api::make_err(api::err::LibraryAlreadyOpen, "library already opened")};
    }

    const auto root = std::filesystem::path(path);
    const auto bfDir = root / ".bf";
    const auto mainDbPath = bfDir / "billfish.db";
    const auto summaryDbPath = bfDir / "summary_v2.db";

    if (!std::filesystem::exists(mainDbPath)) {
        return {.info = api::make_err(api::err::NotFound, "missing .bf/billfish.db")};
    }

    std::string error;
    if (!mainDb_.open(mainDbPath, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!summaryDb_.open(summaryDbPath, &error)) {
        mainDb_.close();
        return {.info = api::make_err(api::err::Database, error)};
    }

    if (!summaryDb_.exec(db::summary_schema_sql(), &error)) {
        mainDb_.close();
        summaryDb_.close();
        return {.info = api::make_err(api::err::Database, error)};
    }

    int libVersion = 40;
    int schemaVersion = 40;
    std::string libName = "Billfish Library";

    {
        auto stmt = mainDb_.prepare("SELECT value FROM library WHERE key='version'", &error);
        if (stmt.valid() && stmt.step() == db::kSqliteRow) {
            libVersion = std::stoi(stmt.column_text(0));
        }
    }
    {
        auto stmt = mainDb_.prepare("SELECT value FROM library WHERE key='name'", &error);
        if (stmt.valid() && stmt.step() == db::kSqliteRow) {
            libName = stmt.column_text(0);
        }
    }
    {
        auto stmt = mainDb_.prepare("SELECT value FROM app_meta WHERE key='schema_version'", &error);
        if (stmt.valid() && stmt.step() == db::kSqliteRow) {
            schemaVersion = std::stoi(stmt.column_text(0));
        }
    }

    if (schemaVersion < 41) {
        if (!mainDb_.exec(db::migration_40_to_41_sql(), &error)) {
            closeLibrary();
            return {.info = api::make_err(api::err::Database, error)};
        }
        schemaVersion = 41;
    }

    if (libVersion != 40 && libVersion != 41) {
        closeLibrary();
        return {.info = api::make_err(api::err::Version, "unsupported library version")};
    }

    opened_ = true;
    libraryRoot_ = root;
    bfDir_ = bfDir;

    api::LibraryInfo info;
    info.root = libraryRoot_.string();
    info.name = libName;
    info.version = libVersion;
    info.schemaVersion = schemaVersion;

    return {.info = api::make_ok(), .libInfo = info};
}

api::GenericResponse BillfishDataCenter::upgradeLibrary(
    const std::string& path,
    const std::int32_t fromVersion,
    const std::int32_t toVersion) {
    if (!(fromVersion == 40 && toVersion == 41)) {
        return {.info = api::make_err(api::err::Unsupported, "only 40->41 is implemented")};
    }

    const auto root = std::filesystem::path(path);
    const auto dbPath = root / ".bf" / "billfish.db";
    if (!std::filesystem::exists(dbPath)) {
        return {.info = api::make_err(api::err::NotFound, "missing .bf/billfish.db")};
    }

    db::Database db;
    std::string error;
    if (!db.open(dbPath, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!db.exec(db::migration_40_to_41_sql(), &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok("upgraded 40->41")};
}

api::GenericResponse BillfishDataCenter::closeLibrary() {
    mainDb_.close();
    summaryDb_.close();
    opened_ = false;
    libraryRoot_.clear();
    bfDir_.clear();
    return {.info = api::make_ok("library closed")};
}

api::ImportResponse BillfishDataCenter::importFiles(const api::ImportRequest& request) {
    api::ImportResponse out;
    if (!require_open(&out.info)) {
        return out;
    }
    if (request.entries.empty()) {
        out.info = api::make_err(api::err::InvalidArgument, "entries is empty");
        return out;
    }

    const auto files = collect_files(request.entries, request.recursive);
    if (files.empty()) {
        out.info = api::make_err(api::err::NotFound, "no files found");
        return out;
    }

    std::string error;
    if (!mainDb_.begin(&error)) {
        out.info = api::make_err(api::err::Database, error);
        return out;
    }

    for (const auto& src : files) {
        auto id = import_one(src, request, &error);
        if (id.has_value()) {
            out.importedIds.push_back(*id);
        } else {
            out.failed.push_back(src.string() + ": " + error);
        }
    }

    if (!mainDb_.commit(&error)) {
        mainDb_.rollback(nullptr);
        out.info = api::make_err(api::err::Database, error);
        return out;
    }

    if (!refresh_summary(out.importedIds, &error)) {
        out.info = api::make_err(api::err::Database, error);
        return out;
    }

    out.info = api::make_ok();
    return out;
}

api::GenericResponse BillfishDataCenter::reindex(const api::ReIndexRequest& request) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }

    std::vector<std::int64_t> ids = request.fileIds;
    std::string error;
    if (ids.empty()) {
        auto stmt = mainDb_.prepare("SELECT id FROM bf_file", &error);
        if (!stmt.valid()) {
            return {.info = api::make_err(api::err::Database, error)};
        }
        while (stmt.step() == db::kSqliteRow) {
            ids.push_back(stmt.column_int64(0));
        }
    }

    if (!refresh_summary(ids, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::move(const api::MoveRequest& request) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    if (request.fileIds.empty()) {
        return {.info = api::make_err(api::err::InvalidArgument, "fileIds is empty")};
    }

    std::string error;
    if (!mainDb_.begin(&error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }

    auto stmt = mainDb_.prepare("UPDATE bf_file SET pid=?, update_time=? WHERE id=?", &error);
    if (!stmt.valid()) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }

    const auto now = util::now_seconds();
    for (const auto id : request.fileIds) {
        stmt.reset();
        stmt.bind_int64(1, request.targetFolderId);
        stmt.bind_int64(2, now);
        stmt.bind_int64(3, id);
        if (stmt.step() != db::kSqliteDone) {
            mainDb_.rollback(nullptr);
            return {.info = api::make_err(api::err::Database, "move step failed")};
        }
    }

    if (!mainDb_.commit(&error)) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!refresh_summary(request.fileIds, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::removeOrRestore(const api::RemoveOrRestoreRequest& request) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    if (request.fileIds.empty()) {
        return {.info = api::make_err(api::err::InvalidArgument, "fileIds is empty")};
    }

    std::string action = request.action;
    std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (action != "remove" && action != "restore" && action != "purge") {
        return {.info = api::make_err(api::err::InvalidArgument, "action must be remove/restore/purge")};
    }

    std::string error;
    if (!mainDb_.begin(&error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }

    const auto now = util::now_seconds();

    if (action == "remove") {
        auto markStmt = mainDb_.prepare("UPDATE bf_file SET is_recycle=1, update_time=? WHERE id=?", &error);
        auto pathStmt = mainDb_.prepare("SELECT path FROM bf_file WHERE id=?", &error);
        auto recStmt = mainDb_.prepare(
            "INSERT INTO bf_recycle(entity_type, entity_id, original_path, deleted_at) VALUES('file', ?, ?, ?) "
            "ON CONFLICT(entity_type, entity_id) DO UPDATE SET original_path=excluded.original_path, deleted_at=excluded.deleted_at",
            &error);
        if (!markStmt.valid() || !pathStmt.valid() || !recStmt.valid()) {
            mainDb_.rollback(nullptr);
            return {.info = api::make_err(api::err::Database, error)};
        }

        for (const auto id : request.fileIds) {
            pathStmt.reset();
            pathStmt.bind_int64(1, id);
            std::string p;
            if (pathStmt.step() == db::kSqliteRow) {
                p = pathStmt.column_text(0);
            }

            markStmt.reset();
            markStmt.bind_int64(1, now);
            markStmt.bind_int64(2, id);
            if (markStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "remove update failed")};
            }

            recStmt.reset();
            recStmt.bind_int64(1, id);
            recStmt.bind_text(2, p);
            recStmt.bind_int64(3, now);
            if (recStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "recycle insert failed")};
            }
        }
    }

    if (action == "restore") {
        auto markStmt = mainDb_.prepare("UPDATE bf_file SET is_recycle=0, update_time=? WHERE id=?", &error);
        auto delStmt = mainDb_.prepare("DELETE FROM bf_recycle WHERE entity_type='file' AND entity_id=?", &error);
        if (!markStmt.valid() || !delStmt.valid()) {
            mainDb_.rollback(nullptr);
            return {.info = api::make_err(api::err::Database, error)};
        }

        for (const auto id : request.fileIds) {
            markStmt.reset();
            markStmt.bind_int64(1, now);
            markStmt.bind_int64(2, id);
            if (markStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "restore update failed")};
            }

            delStmt.reset();
            delStmt.bind_int64(1, id);
            if (delStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "recycle delete failed")};
            }
        }
    }

    if (action == "purge") {
        auto queryStmt = mainDb_.prepare("SELECT path, import_mode FROM bf_file WHERE id=?", &error);
        auto delFileStmt = mainDb_.prepare("DELETE FROM bf_file WHERE id=?", &error);
        auto delRecStmt = mainDb_.prepare("DELETE FROM bf_recycle WHERE entity_type='file' AND entity_id=?", &error);
        if (!queryStmt.valid() || !delFileStmt.valid() || !delRecStmt.valid()) {
            mainDb_.rollback(nullptr);
            return {.info = api::make_err(api::err::Database, error)};
        }

        const auto imported = imported_root();
        for (const auto id : request.fileIds) {
            queryStmt.reset();
            queryStmt.bind_int64(1, id);
            std::filesystem::path p;
            int mode = 0;
            if (queryStmt.step() == db::kSqliteRow) {
                p = queryStmt.column_text(0);
                mode = queryStmt.column_int(1);
            }

            if ((mode == static_cast<int>(api::ImportMode::ImportCopy) ||
                 mode == static_cast<int>(api::ImportMode::ImportCut)) &&
                !p.empty()) {
                std::error_code ec;
                const auto weak = std::filesystem::weakly_canonical(p, ec);
                if (!ec && weak.native().rfind(imported.native(), 0) == 0) {
                    std::filesystem::remove(p, ec);
                }
            }

            delFileStmt.reset();
            delFileStmt.bind_int64(1, id);
            if (delFileStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "file row delete failed")};
            }

            delRecStmt.reset();
            delRecStmt.bind_int64(1, id);
            if (delRecStmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "recycle row delete failed")};
            }
        }
    }

    if (!mainDb_.commit(&error)) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!refresh_summary(request.fileIds, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok()};
}

api::TagNewResponse BillfishDataCenter::tagNew(const std::string& name, const std::int64_t groupId, const std::int64_t parentId) {
    api::TagNewResponse out;
    if (!require_open(&out.info)) {
        return out;
    }
    if (name.empty()) {
        out.info = api::make_err(api::err::InvalidArgument, "tag name is empty");
        return out;
    }

    std::string error;
    auto stmt = mainDb_.prepare(
        "INSERT INTO bf_tag(name,group_id,parent_id,sort_index,create_time,update_time) VALUES(?,?,?,0,?,?)",
        &error);
    if (!stmt.valid()) {
        out.info = api::make_err(api::err::Database, error);
        return out;
    }
    const auto now = util::now_seconds();
    stmt.bind_text(1, name);
    stmt.bind_int64(2, groupId);
    stmt.bind_int64(3, parentId);
    stmt.bind_int64(4, now);
    stmt.bind_int64(5, now);
    if (stmt.step() != db::kSqliteDone) {
        out.info = api::make_err(api::err::Database, "tag insert failed");
        return out;
    }
    out.info = api::make_ok();
    out.tagId = mainDb_.last_insert_rowid();
    return out;
}

api::GenericResponse BillfishDataCenter::tagUpdate(const std::int64_t tagId, const std::string& name) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }

    std::string error;
    auto stmt = mainDb_.prepare("UPDATE bf_tag SET name=?, update_time=? WHERE id=?", &error);
    if (!stmt.valid()) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    stmt.bind_text(1, name);
    stmt.bind_int64(2, util::now_seconds());
    stmt.bind_int64(3, tagId);
    if (stmt.step() != db::kSqliteDone) {
        return {.info = api::make_err(api::err::Database, "tag update failed")};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::tagRemove(const std::int64_t tagId) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    std::string error;
    auto stmt = mainDb_.prepare("DELETE FROM bf_tag WHERE id=?", &error);
    if (!stmt.valid()) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    stmt.bind_int64(1, tagId);
    if (stmt.step() != db::kSqliteDone) {
        return {.info = api::make_err(api::err::Database, "tag delete failed")};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::tagMove(const std::int64_t tagId, const std::int64_t groupId, const std::int64_t parentId) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    std::string error;
    auto stmt = mainDb_.prepare("UPDATE bf_tag SET group_id=?, parent_id=?, update_time=? WHERE id=?", &error);
    if (!stmt.valid()) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    stmt.bind_int64(1, groupId);
    stmt.bind_int64(2, parentId);
    stmt.bind_int64(3, util::now_seconds());
    stmt.bind_int64(4, tagId);
    if (stmt.step() != db::kSqliteDone) {
        return {.info = api::make_err(api::err::Database, "tag move failed")};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::tagAttach(const std::vector<std::int64_t>& fileIds, const std::vector<std::int64_t>& tagIds) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    if (fileIds.empty() || tagIds.empty()) {
        return {.info = api::make_err(api::err::InvalidArgument, "fileIds/tagIds is empty")};
    }

    std::string error;
    if (!mainDb_.begin(&error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }

    auto stmt = mainDb_.prepare("INSERT OR IGNORE INTO bf_file_tag(file_id,tag_id,create_time) VALUES(?,?,?)", &error);
    if (!stmt.valid()) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }

    const auto now = util::now_seconds();
    for (const auto fileId : fileIds) {
        for (const auto tagId : tagIds) {
            stmt.reset();
            stmt.bind_int64(1, fileId);
            stmt.bind_int64(2, tagId);
            stmt.bind_int64(3, now);
            if (stmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "tag attach failed")};
            }
        }
    }
    if (!mainDb_.commit(&error)) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!refresh_summary(fileIds, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok()};
}

api::GenericResponse BillfishDataCenter::tagDetach(const std::vector<std::int64_t>& fileIds, const std::vector<std::int64_t>& tagIds) {
    api::ResponseInfo info;
    if (!require_open(&info)) {
        return {.info = info};
    }
    if (fileIds.empty() || tagIds.empty()) {
        return {.info = api::make_err(api::err::InvalidArgument, "fileIds/tagIds is empty")};
    }

    std::string error;
    if (!mainDb_.begin(&error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    auto stmt = mainDb_.prepare("DELETE FROM bf_file_tag WHERE file_id=? AND tag_id=?", &error);
    if (!stmt.valid()) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }
    for (const auto fileId : fileIds) {
        for (const auto tagId : tagIds) {
            stmt.reset();
            stmt.bind_int64(1, fileId);
            stmt.bind_int64(2, tagId);
            if (stmt.step() != db::kSqliteDone) {
                mainDb_.rollback(nullptr);
                return {.info = api::make_err(api::err::Database, "tag detach failed")};
            }
        }
    }
    if (!mainDb_.commit(&error)) {
        mainDb_.rollback(nullptr);
        return {.info = api::make_err(api::err::Database, error)};
    }
    if (!refresh_summary(fileIds, &error)) {
        return {.info = api::make_err(api::err::Database, error)};
    }
    return {.info = api::make_ok()};
}

api::SearchSummaryResponse BillfishDataCenter::searchSummary(const api::SearchSummaryRequest& request) {
    api::SearchSummaryResponse out;
    if (!require_open(&out.info)) {
        return out;
    }

    std::string error;
    if (!request.query.empty()) {
        auto stmt = summaryDb_.prepare("SELECT iid FROM bf_summary WHERE bf_summary MATCH ? LIMIT ? OFFSET ?", &error);
        if (!stmt.valid()) {
            out.info = api::make_err(api::err::Database, error);
            return out;
        }
        stmt.bind_text(1, util::normalize_for_search(request.query));
        stmt.bind_int(2, request.limit);
        stmt.bind_int(3, request.offset);
        while (stmt.step() == db::kSqliteRow) {
            auto item = get_file_by_id(stmt.column_int64(0));
            if (!item.has_value()) {
                continue;
            }
            if (!request.includeRecycle && item->isRecycle) {
                continue;
            }
            out.items.push_back(*item);
        }
        out.info = api::make_ok();
        return out;
    }

    auto stmt = mainDb_.prepare(
        request.includeRecycle
            ? "SELECT id,pid,name,ext_name,path,size,hash,is_recycle FROM bf_file ORDER BY update_time DESC LIMIT ? OFFSET ?"
            : "SELECT id,pid,name,ext_name,path,size,hash,is_recycle FROM bf_file WHERE is_recycle=0 ORDER BY update_time DESC LIMIT ? OFFSET ?",
        &error);
    if (!stmt.valid()) {
        out.info = api::make_err(api::err::Database, error);
        return out;
    }
    stmt.bind_int(1, request.limit);
    stmt.bind_int(2, request.offset);
    while (stmt.step() == db::kSqliteRow) {
        out.items.push_back(read_file_item(stmt));
    }

    out.info = api::make_ok();
    return out;
}

api::DuplicationResponse BillfishDataCenter::scanDuplication(const api::ScanDuplicationRequest& request) {
    (void)request;
    api::DuplicationResponse out;
    if (!require_open(&out.info)) {
        return out;
    }

    std::string error;
    auto stmt = mainDb_.prepare(
        "SELECT hash,size,COUNT(*) c FROM bf_file WHERE is_recycle=0 AND hash<>'' GROUP BY hash,size HAVING c>1 ORDER BY c DESC",
        &error);
    if (!stmt.valid()) {
        out.info = api::make_err(api::err::Database, error);
        return out;
    }

    while (stmt.step() == db::kSqliteRow) {
        api::DuplicationGroup group;
        group.hash = stmt.column_text(0);
        group.size = stmt.column_int64(1);

        auto d = mainDb_.prepare(
            "SELECT id,pid,name,ext_name,path,size,hash,is_recycle FROM bf_file WHERE hash=? AND size=? AND is_recycle=0",
            &error);
        if (!d.valid()) {
            out.info = api::make_err(api::err::Database, error);
            return out;
        }
        d.bind_text(1, group.hash);
        d.bind_int64(2, group.size);
        while (d.step() == db::kSqliteRow) {
            group.items.push_back(read_file_item(d));
        }
        out.groups.push_back(std::move(group));
    }

    out.info = api::make_ok();
    return out;
}

api::ExportResponse BillfishDataCenter::exportFiles(const api::ExportRequest& request) {
    api::ExportResponse out;
    if (!require_open(&out.info)) {
        return out;
    }
    if (request.fileIds.empty()) {
        out.info = api::make_err(api::err::InvalidArgument, "fileIds is empty");
        return out;
    }

    std::error_code ec;
    const auto destination = std::filesystem::path(request.destination);
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        out.info = api::make_err(api::err::Io, ec.message());
        return out;
    }

    std::vector<api::FileItem> files;
    files.reserve(request.fileIds.size());
    for (const auto id : request.fileIds) {
        auto item = get_file_by_id(id);
        if (item.has_value()) {
            files.push_back(*item);
        }
    }

    if (request.mode == "Folder" || request.mode == "folder") {
        int exported = 0;
        for (const auto& file : files) {
            std::filesystem::path src(file.path);
            if (!std::filesystem::exists(src)) {
                continue;
            }
            auto target = util::dedup_path(destination / src.filename());
            std::filesystem::copy_file(src, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                ++exported;
            }
        }
        out.info = api::make_ok();
        out.exported = exported;
        return out;
    }

    if (request.mode == "Packet" || request.mode == "packet") {
        const auto packetDir = destination / ("BillfishPack_" + std::to_string(util::now_seconds()) + ".billfishpack");
        std::filesystem::create_directories(packetDir / "files", ec);
        if (ec) {
            out.info = api::make_err(api::err::Io, ec.message());
            return out;
        }

        std::ofstream manifest(packetDir / "manifest.json", std::ios::binary);
        manifest << "{\n  \"version\": 1,\n  \"items\": [\n";

        int exported = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& file = files[i];
            std::filesystem::path src(file.path);
            if (!std::filesystem::exists(src)) {
                continue;
            }
            const auto targetName = std::to_string(file.id) + "_" + src.filename().string();
            const auto targetPath = packetDir / "files" / targetName;
            std::filesystem::copy_file(src, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                continue;
            }

            manifest << "    {\"id\": " << file.id << ", \"name\": \"" << file.name
                     << "\", \"path\": \"files/" << targetName << "\"}";
            if (i + 1 < files.size()) {
                manifest << ",";
            }
            manifest << "\n";
            ++exported;
        }
        manifest << "  ]\n}\n";

        out.info = api::make_ok();
        out.exported = exported;
        out.packetPath = packetDir.string();
        return out;
    }

    out.info = api::make_err(api::err::InvalidArgument, "mode must be Folder or Packet");
    return out;
}

bool BillfishDataCenter::require_open(api::ResponseInfo* info) const {
    if (opened_) {
        return true;
    }
    if (info != nullptr) {
        *info = api::make_err(api::err::LibraryNotOpen, "library not open");
    }
    return false;
}

bool BillfishDataCenter::with_tx(std::function<bool(std::string*)> work, std::string* error) {
    if (!mainDb_.begin(error)) {
        return false;
    }
    if (!work(error)) {
        mainDb_.rollback(nullptr);
        return false;
    }
    if (!mainDb_.commit(error)) {
        mainDb_.rollback(nullptr);
        return false;
    }
    return true;
}

bool BillfishDataCenter::bootstrap_main_db(const std::filesystem::path& bfDir, const std::string& name, std::string* error) {
    db::Database db;
    if (!db.open(bfDir / "billfish.db", error)) {
        return false;
    }
    if (!db.exec(db::main_schema_sql(), error)) {
        return false;
    }
    if (!db.begin(error)) {
        return false;
    }

    const auto now = util::now_seconds();
    auto kv = db.prepare("INSERT OR REPLACE INTO library(key,value) VALUES(?,?)", error);
    if (!kv.valid()) {
        db.rollback(nullptr);
        return false;
    }
    kv.bind_text(1, "name");
    kv.bind_text(2, name);
    if (kv.step() != db::kSqliteDone) {
        db.rollback(nullptr);
        return false;
    }

    kv.reset();
    kv.bind_text(1, "version");
    kv.bind_text(2, "40");
    if (kv.step() != db::kSqliteDone) {
        db.rollback(nullptr);
        return false;
    }

    if (!db.exec("INSERT OR REPLACE INTO app_meta(key,value) VALUES('schema_version','41')", error)) {
        db.rollback(nullptr);
        return false;
    }

    auto root = db.prepare(
        "INSERT OR IGNORE INTO bf_folder(id,pid,name,path,create_time,update_time) VALUES(1,1,'root','/',?,?)", error);
    if (!root.valid()) {
        db.rollback(nullptr);
        return false;
    }
    root.bind_int64(1, now);
    root.bind_int64(2, now);
    if (root.step() != db::kSqliteDone) {
        db.rollback(nullptr);
        return false;
    }

    auto typeStmt = db.prepare("INSERT OR IGNORE INTO bf_type(ext_name,type_name,can_analyze) VALUES(?,?,?)", error);
    if (!typeStmt.valid()) {
        db.rollback(nullptr);
        return false;
    }
    for (const auto& [ext, typeName, canAnalyze] : db::type_seed()) {
        typeStmt.reset();
        typeStmt.bind_text(1, ext);
        typeStmt.bind_text(2, typeName);
        typeStmt.bind_int(3, canAnalyze);
        if (typeStmt.step() != db::kSqliteDone) {
            db.rollback(nullptr);
            return false;
        }
    }

    return db.commit(error);
}

bool BillfishDataCenter::bootstrap_summary_db(const std::filesystem::path& bfDir, std::string* error) {
    db::Database db;
    if (!db.open(bfDir / "summary_v2.db", error)) {
        return false;
    }
    if (!db.exec(db::summary_schema_sql(), error)) {
        return false;
    }
    if (!db.exec("INSERT OR REPLACE INTO summary_meta(key,value) VALUES('schema_version','41')", error)) {
        return false;
    }
    return true;
}

bool BillfishDataCenter::refresh_summary(const std::vector<std::int64_t>& fileIds, std::string* error) {
    if (fileIds.empty()) {
        return true;
    }
    if (!summaryDb_.begin(error)) {
        return false;
    }

    auto delStmt = summaryDb_.prepare("DELETE FROM bf_summary WHERE iid=?", error);
    auto insStmt = summaryDb_.prepare(
        "INSERT INTO bf_summary(iid,name,ext_name,tags,folder_name,comment,note) VALUES(?,?,?,?,?,?,?)", error);
    auto qStmt = mainDb_.prepare(
        "SELECT f.id,f.name,f.ext_name,f.is_recycle,COALESCE(fd.name,''),COALESCE(u.comment,''),COALESCE(u.note,'') "
        "FROM bf_file f "
        "LEFT JOIN bf_folder fd ON fd.id=f.pid "
        "LEFT JOIN bf_material_userdata u ON u.file_id=f.id "
        "WHERE f.id=?",
        error);
    auto tStmt = mainDb_.prepare(
        "SELECT t.name FROM bf_file_tag ft JOIN bf_tag t ON t.id=ft.tag_id WHERE ft.file_id=? ORDER BY t.name", error);

    if (!delStmt.valid() || !insStmt.valid() || !qStmt.valid() || !tStmt.valid()) {
        summaryDb_.rollback(nullptr);
        return false;
    }

    for (const auto id : fileIds) {
        qStmt.reset();
        qStmt.bind_int64(1, id);
        if (qStmt.step() != db::kSqliteRow) {
            delStmt.reset();
            delStmt.bind_int64(1, id);
            if (delStmt.step() != db::kSqliteDone) {
                summaryDb_.rollback(nullptr);
                return false;
            }
            continue;
        }

        const auto recycle = qStmt.column_int(3);
        delStmt.reset();
        delStmt.bind_int64(1, id);
        if (delStmt.step() != db::kSqliteDone) {
            summaryDb_.rollback(nullptr);
            return false;
        }
        if (recycle != 0) {
            continue;
        }

        tStmt.reset();
        tStmt.bind_int64(1, id);
        std::string tags;
        bool first = true;
        while (tStmt.step() == db::kSqliteRow) {
            if (!first) {
                tags.push_back(' ');
            }
            tags += tStmt.column_text(0);
            first = false;
        }

        insStmt.reset();
        insStmt.bind_int64(1, id);
        insStmt.bind_text(2, util::normalize_for_search(qStmt.column_text(1)));
        insStmt.bind_text(3, util::normalize_for_search(qStmt.column_text(2)));
        insStmt.bind_text(4, util::normalize_for_search(tags));
        insStmt.bind_text(5, util::normalize_for_search(qStmt.column_text(4)));
        insStmt.bind_text(6, util::normalize_for_search(qStmt.column_text(5)));
        insStmt.bind_text(7, util::normalize_for_search(qStmt.column_text(6)));
        if (insStmt.step() != db::kSqliteDone) {
            summaryDb_.rollback(nullptr);
            return false;
        }
    }

    return summaryDb_.commit(error);
}

std::vector<std::filesystem::path> BillfishDataCenter::collect_files(const std::vector<std::string>& entries, const bool recursive) const {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : entries) {
        std::error_code ec;
        const auto p = std::filesystem::path(entry);
        if (!std::filesystem::exists(p, ec) || ec) {
            continue;
        }
        if (std::filesystem::is_regular_file(p, ec) && !ec) {
            files.push_back(p);
            continue;
        }
        if (!std::filesystem::is_directory(p, ec) || ec) {
            continue;
        }

        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(p, ec);
                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (ec) {
                    break;
                }
                if (std::filesystem::is_regular_file(*it, ec) && !ec) {
                    files.push_back(it->path());
                }
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(p, ec); it != std::filesystem::directory_iterator(); ++it) {
                if (ec) {
                    break;
                }
                if (std::filesystem::is_regular_file(*it, ec) && !ec) {
                    files.push_back(it->path());
                }
            }
        }
    }
    return files;
}

std::optional<std::int64_t> BillfishDataCenter::import_one(
    const std::filesystem::path& src,
    const api::ImportRequest& request,
    std::string* error) {
    std::error_code ec;
    std::filesystem::path finalPath = src;

    if (request.mode == api::ImportMode::ImportCopy || request.mode == api::ImportMode::ImportCut) {
        const auto imported = imported_root();
        std::filesystem::create_directories(imported, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return std::nullopt;
        }
        finalPath = imported / (util::random_hex(12) + src.extension().string());
        if (request.mode == api::ImportMode::ImportCopy) {
            std::filesystem::copy_file(src, finalPath, std::filesystem::copy_options::overwrite_existing, ec);
        } else {
            std::filesystem::rename(src, finalPath, ec);
        }
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return std::nullopt;
        }
    }

    const auto size = static_cast<std::int64_t>(std::filesystem::file_size(finalPath, ec));
    if (ec) {
        if (error != nullptr) {
            *error = ec.message();
        }
        return std::nullopt;
    }
    const auto now = util::now_seconds();

    auto insert = mainDb_.prepare(
        "INSERT INTO bf_file(pid,name,ext_name,path,size,hash,ctime,mtime,import_mode,is_index,is_recycle,create_time,update_time) "
        "VALUES(?,?,?,?,?,?,?,?,?,1,0,?,?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "size=excluded.size,hash=excluded.hash,mtime=excluded.mtime,update_time=excluded.update_time,is_recycle=0",
        error);
    if (!insert.valid()) {
        return std::nullopt;
    }
    insert.bind_int64(1, request.targetFolderId);
    insert.bind_text(2, finalPath.filename().string());
    insert.bind_text(3, util::extension_lower(finalPath));
    insert.bind_text(4, finalPath.string());
    insert.bind_int64(5, size);
    insert.bind_text(6, util::file_hash_fnv1a(finalPath));
    insert.bind_int64(7, now);
    insert.bind_int64(8, now);
    insert.bind_int(9, static_cast<int>(request.mode));
    insert.bind_int64(10, now);
    insert.bind_int64(11, now);
    if (insert.step() != db::kSqliteDone) {
        if (error != nullptr) {
            *error = "insert bf_file failed";
        }
        return std::nullopt;
    }

    auto query = mainDb_.prepare("SELECT id FROM bf_file WHERE path=?", error);
    if (!query.valid()) {
        return std::nullopt;
    }
    query.bind_text(1, finalPath.string());
    if (query.step() != db::kSqliteRow) {
        if (error != nullptr) {
            *error = "query imported id failed";
        }
        return std::nullopt;
    }
    const auto fileId = query.column_int64(0);

    auto userData = mainDb_.prepare("INSERT OR IGNORE INTO bf_material_userdata(file_id,comment,note) VALUES(?, '', '')", error);
    if (!userData.valid()) {
        return std::nullopt;
    }
    userData.bind_int64(1, fileId);
    if (userData.step() != db::kSqliteDone) {
        if (error != nullptr) {
            *error = "insert bf_material_userdata failed";
        }
        return std::nullopt;
    }
    return fileId;
}

std::filesystem::path BillfishDataCenter::imported_root() const {
    return libraryRoot_ / "_imported";
}

std::optional<api::FileItem> BillfishDataCenter::get_file_by_id(const std::int64_t fileId) {
    std::string error;
    auto stmt = mainDb_.prepare("SELECT id,pid,name,ext_name,path,size,hash,is_recycle FROM bf_file WHERE id=?", &error);
    if (!stmt.valid()) {
        return std::nullopt;
    }
    stmt.bind_int64(1, fileId);
    if (stmt.step() != db::kSqliteRow) {
        return std::nullopt;
    }
    return read_file_item(stmt);
}

} // namespace bf::core::datacenter
