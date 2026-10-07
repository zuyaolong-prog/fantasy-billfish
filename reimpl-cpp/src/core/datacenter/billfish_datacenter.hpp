#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/api/types.hpp"
#include "core/db/database.hpp"

namespace bf::core::datacenter {

class BillfishDataCenter {
public:
    BillfishDataCenter() = default;
    ~BillfishDataCenter() = default;

    api::GenericResponse createLibrary(const std::string& path, const std::string& name);
    api::OpenLibraryResponse openLibrary(const std::string& path);
    api::GenericResponse upgradeLibrary(const std::string& path, std::int32_t fromVersion, std::int32_t toVersion);
    api::GenericResponse closeLibrary();

    api::ImportResponse importFiles(const api::ImportRequest& request);
    api::GenericResponse reindex(const api::ReIndexRequest& request);
    api::GenericResponse move(const api::MoveRequest& request);
    api::GenericResponse removeOrRestore(const api::RemoveOrRestoreRequest& request);

    api::TagNewResponse tagNew(const std::string& name, std::int64_t groupId = 0, std::int64_t parentId = 0);
    api::GenericResponse tagUpdate(std::int64_t tagId, const std::string& name);
    api::GenericResponse tagRemove(std::int64_t tagId);
    api::GenericResponse tagMove(std::int64_t tagId, std::int64_t groupId, std::int64_t parentId);
    api::GenericResponse tagAttach(const std::vector<std::int64_t>& fileIds, const std::vector<std::int64_t>& tagIds);
    api::GenericResponse tagDetach(const std::vector<std::int64_t>& fileIds, const std::vector<std::int64_t>& tagIds);

    api::SearchSummaryResponse searchSummary(const api::SearchSummaryRequest& request);
    api::DuplicationResponse scanDuplication(const api::ScanDuplicationRequest& request);
    api::ExportResponse exportFiles(const api::ExportRequest& request);

private:
    bool require_open(api::ResponseInfo* info) const;

    bool with_tx(std::function<bool(std::string*)> work, std::string* error);

    bool bootstrap_main_db(const std::filesystem::path& bfDir, const std::string& name, std::string* error);
    bool bootstrap_summary_db(const std::filesystem::path& bfDir, std::string* error);

    bool refresh_summary(const std::vector<std::int64_t>& fileIds, std::string* error);
    std::vector<std::filesystem::path> collect_files(const std::vector<std::string>& entries, bool recursive) const;
    std::optional<std::int64_t> import_one(const std::filesystem::path& src, const api::ImportRequest& request, std::string* error);
    std::filesystem::path imported_root() const;

    std::optional<api::FileItem> get_file_by_id(std::int64_t fileId);

    std::filesystem::path libraryRoot_;
    std::filesystem::path bfDir_;
    db::Database mainDb_;
    db::Database summaryDb_;
    bool opened_{false};
};

} // namespace bf::core::datacenter
