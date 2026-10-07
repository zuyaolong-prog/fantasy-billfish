#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bf::core::api {

enum class ImportMode : std::int32_t {
    ImportNone = 0,
    ImportCopy = 1,
    ImportCut = 2,
    ImportIndex = 3,
};

struct ResponseInfo {
    std::int32_t code{0};
    std::string message{"ok"};

    [[nodiscard]] bool ok() const noexcept { return code == 0; }
};

struct LibraryInfo {
    std::string root;
    std::string name;
    std::int32_t version{40};
    std::int32_t schemaVersion{41};
};

struct FileItem {
    std::int64_t id{0};
    std::int64_t pid{0};
    std::string name;
    std::string extName;
    std::string path;
    std::int64_t size{0};
    std::string hash;
    bool isRecycle{false};
};

struct TagItem {
    std::int64_t id{0};
    std::string name;
    std::int64_t groupId{0};
    std::int64_t parentId{0};
};

struct ImportRequest {
    std::vector<std::string> entries;
    ImportMode mode{ImportMode::ImportIndex};
    std::int64_t targetFolderId{1};
    bool recursive{true};
};

struct ReIndexRequest {
    std::vector<std::int64_t> fileIds;
};

struct MoveRequest {
    std::vector<std::int64_t> fileIds;
    std::int64_t targetFolderId{1};
};

struct RemoveOrRestoreRequest {
    std::vector<std::int64_t> fileIds;
    std::string action; // remove / restore / purge
};

struct SearchSummaryRequest {
    std::string query;
    std::int32_t limit{100};
    std::int32_t offset{0};
    bool includeRecycle{false};
};

struct ScanDuplicationRequest {
    std::vector<std::int64_t> folderIds;
};

struct ExportRequest {
    std::string mode; // Folder / Packet
    std::vector<std::int64_t> fileIds;
    std::string destination;
};

struct ImportResponse {
    ResponseInfo info;
    std::vector<std::int64_t> importedIds;
    std::vector<std::string> failed;
};

struct SearchSummaryResponse {
    ResponseInfo info;
    std::vector<FileItem> items;
};

struct GenericResponse {
    ResponseInfo info;
};

struct OpenLibraryResponse {
    ResponseInfo info;
    std::optional<LibraryInfo> libInfo;
};

struct TagNewResponse {
    ResponseInfo info;
    std::int64_t tagId{0};
};

struct DuplicationGroup {
    std::string hash;
    std::int64_t size{0};
    std::vector<FileItem> items;
};

struct DuplicationResponse {
    ResponseInfo info;
    std::vector<DuplicationGroup> groups;
};

struct ExportResponse {
    ResponseInfo info;
    std::int32_t exported{0};
    std::string packetPath;
};

namespace err {
inline constexpr std::int32_t Ok = 0;
inline constexpr std::int32_t InvalidArgument = 1001;
inline constexpr std::int32_t LibraryNotOpen = 1002;
inline constexpr std::int32_t LibraryAlreadyOpen = 1003;
inline constexpr std::int32_t Database = 1004;
inline constexpr std::int32_t NotFound = 1005;
inline constexpr std::int32_t Io = 1006;
inline constexpr std::int32_t Unsupported = 1007;
inline constexpr std::int32_t Version = 1008;
inline constexpr std::int32_t Conflict = 1009;
} // namespace err

ResponseInfo make_ok(std::string message = "ok");
ResponseInfo make_err(std::int32_t code, std::string message);

} // namespace bf::core::api
