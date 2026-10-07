#include "billfish_file_locator.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>
#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include <algorithm>

namespace billfish::trim {
namespace {

struct sqlite3;
struct sqlite3_stmt;

constexpr int kSqliteOk = 0;
constexpr int kSqliteRow = 100;
constexpr int kSqliteDone = 101;
constexpr int kSqliteOpenReadOnly = 0x00000001;

class SqliteApi final {
public:
    explicit SqliteApi(const QString &libraryPath)
        : library(libraryPath)
    {
    }

    bool load(QString *error)
    {
        if (!library.load()) {
            *error = QStringLiteral("Could not load sqlite3 library '%1': %2")
                         .arg(library.fileName(), library.errorString());
            return false;
        }
        return resolve("sqlite3_open_v2", &openV2, error)
            && resolve("sqlite3_close", &close, error)
            && resolve("sqlite3_errmsg", &errorMessage, error)
            && resolve("sqlite3_prepare_v2", &prepareV2, error)
            && resolve("sqlite3_bind_int64", &bindInt64, error)
            && resolve("sqlite3_step", &step, error)
            && resolve("sqlite3_finalize", &finalize, error)
            && resolve("sqlite3_column_text", &columnText, error)
            && resolve("sqlite3_column_int64", &columnInt64, error)
            && resolve("sqlite3_busy_timeout", &busyTimeout, error);
    }

    QLibrary library;
    int (*openV2)(const char *, sqlite3 **, int, const char *) = nullptr;
    int (*close)(sqlite3 *) = nullptr;
    const char *(*errorMessage)(sqlite3 *) = nullptr;
    int (*prepareV2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **) = nullptr;
    int (*bindInt64)(sqlite3_stmt *, int, qint64) = nullptr;
    int (*step)(sqlite3_stmt *) = nullptr;
    int (*finalize)(sqlite3_stmt *) = nullptr;
    const unsigned char *(*columnText)(sqlite3_stmt *, int) = nullptr;
    qint64 (*columnInt64)(sqlite3_stmt *, int) = nullptr;
    int (*busyTimeout)(sqlite3 *, int) = nullptr;

private:
    template<typename Function>
    bool resolve(const char *name, Function *function, QString *error)
    {
        *function = reinterpret_cast<Function>(library.resolve(name));
        if (*function) {
            return true;
        }
        *error = QStringLiteral("sqlite3 library '%1' is missing symbol %2.")
                     .arg(library.fileName(), QString::fromLatin1(name));
        return false;
    }
};

class Database final {
public:
    explicit Database(SqliteApi *api)
        : api_(api)
    {
    }

    ~Database()
    {
        if (handle_) {
            api_->close(handle_);
        }
    }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool openReadOnly(const QString &path, QString *error)
    {
        const QByteArray utf8Path = QFileInfo(path).absoluteFilePath().toUtf8();
        const int status = api_->openV2(utf8Path.constData(), &handle_, kSqliteOpenReadOnly, nullptr);
        if (status != kSqliteOk || !handle_) {
            *error = QStringLiteral("Could not open Billfish database '%1': %2")
                         .arg(path, statusMessage());
            return false;
        }
        api_->busyTimeout(handle_, 5'000);
        return true;
    }

    [[nodiscard]] sqlite3 *handle() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] QString statusMessage() const
    {
        if (!handle_) {
            return QStringLiteral("sqlite error");
        }
        const char *message = api_->errorMessage(handle_);
        return message ? QString::fromUtf8(message) : QStringLiteral("sqlite error");
    }

private:
    SqliteApi *api_;
    sqlite3 *handle_ = nullptr;
};

class Statement final {
public:
    Statement(SqliteApi *api, sqlite3_stmt *handle)
        : api_(api)
        , handle_(handle)
    {
    }

    ~Statement()
    {
        if (handle_) {
            api_->finalize(handle_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    [[nodiscard]] sqlite3_stmt *handle() const noexcept
    {
        return handle_;
    }

private:
    SqliteApi *api_;
    sqlite3_stmt *handle_;
};

bool prepare(
    SqliteApi &api,
    Database &database,
    const char *sql,
    sqlite3_stmt **statement,
    QString *error)
{
    if (api.prepareV2(database.handle(), sql, -1, statement, nullptr) != kSqliteOk) {
        *error = QStringLiteral("Billfish database query could not be prepared: %1")
                     .arg(database.statusMessage());
        return false;
    }
    return true;
}

bool readTableColumns(
    SqliteApi &api,
    Database &database,
    const char *pragmaSql,
    QSet<QString> *columns,
    QString *error)
{
    sqlite3_stmt *rawStatement = nullptr;
    if (!prepare(api, database, pragmaSql, &rawStatement, error)) {
        return false;
    }
    Statement statement(&api, rawStatement);
    columns->clear();
    while (true) {
        const int status = api.step(statement.handle());
        if (status == kSqliteDone) {
            break;
        }
        if (status != kSqliteRow) {
            *error = QStringLiteral("Could not inspect the Billfish database schema: %1")
                         .arg(database.statusMessage());
            return false;
        }
        const unsigned char *text = api.columnText(statement.handle(), 1);
        if (text) {
            columns->insert(QString::fromUtf8(reinterpret_cast<const char *>(text)).toCaseFolded());
        }
    }
    return true;
}

bool tableHasColumns(
    SqliteApi &api,
    Database &database,
    const char *pragmaSql,
    const QSet<QString> &requiredColumns,
    QString *error)
{
    QSet<QString> columns;
    if (!readTableColumns(api, database, pragmaSql, &columns, error)) {
        return false;
    }
    for (const QString &required : requiredColumns) {
        if (!columns.contains(required)) {
            *error = QStringLiteral("The Billfish database schema is missing required column '%1'.")
                         .arg(required);
            return false;
        }
    }
    return true;
}

struct NameAndParent {
    bool found = false;
    QString name;
    qint64 parentId = -1;
};

bool queryNameAndParent(
    SqliteApi &api,
    Database &database,
    const char *sql,
    qint64 id,
    NameAndParent *row,
    QString *error)
{
    sqlite3_stmt *rawStatement = nullptr;
    if (!prepare(api, database, sql, &rawStatement, error)) {
        return false;
    }
    Statement statement(&api, rawStatement);
    if (api.bindInt64(statement.handle(), 1, id) != kSqliteOk) {
        *error = QStringLiteral("Could not bind a Billfish file identifier: %1")
                     .arg(database.statusMessage());
        return false;
    }
    const int status = api.step(statement.handle());
    if (status == kSqliteDone) {
        return true;
    }
    if (status != kSqliteRow) {
        *error = QStringLiteral("Could not read the Billfish database: %1")
                     .arg(database.statusMessage());
        return false;
    }
    const unsigned char *name = api.columnText(statement.handle(), 0);
    row->found = true;
    row->name = name ? QString::fromUtf8(reinterpret_cast<const char *>(name)) : QString();
    row->parentId = api.columnInt64(statement.handle(), 1);
    return true;
}

bool queryOptionalVideoDuration(
    SqliteApi &api,
    Database &database,
    qint64 fileId,
    qint64 *durationMs,
    QString *error)
{
    *durationMs = -1;
    QSet<QString> columns;
    if (!readTableColumns(
            api,
            database,
            "PRAGMA table_info(\"bf_material_video\")",
            &columns,
            error)) {
        return false;
    }
    if (!columns.contains(QStringLiteral("file_id"))
        || !columns.contains(QStringLiteral("duration"))) {
        return true;
    }

    sqlite3_stmt *rawStatement = nullptr;
    if (!prepare(
            api,
            database,
            "SELECT duration FROM bf_material_video WHERE file_id = ?1 LIMIT 1",
            &rawStatement,
            error)) {
        return false;
    }
    Statement statement(&api, rawStatement);
    if (api.bindInt64(statement.handle(), 1, fileId) != kSqliteOk) {
        *error = QStringLiteral("Could not bind a Billfish video identifier: %1")
                     .arg(database.statusMessage());
        return false;
    }
    const int status = api.step(statement.handle());
    if (status == kSqliteDone) {
        return true;
    }
    if (status != kSqliteRow) {
        *error = QStringLiteral("Could not read Billfish video metadata: %1")
                     .arg(database.statusMessage());
        return false;
    }
    const qint64 value = api.columnInt64(statement.handle(), 0);
    if (value >= 0) {
        *durationMs = value;
    }
    return true;
}

bool isSafePathComponent(const QString &component)
{
    if (component.isEmpty() || component == QStringLiteral(".")
        || component == QStringLiteral("..") || component.contains(QLatin1Char('/'))
        || component.contains(QLatin1Char('\\')) || component.contains(QLatin1Char(':'))
        || component.contains(QChar::Null)) {
        return false;
    }
    return std::none_of(component.cbegin(), component.cend(), [](QChar character) {
        return character.unicode() < 0x20;
    });
}

QString normalizedExistingFile(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile() ? info.absoluteFilePath() : QString();
}

void appendUniqueRoot(QStringList *roots, const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) {
        return;
    }
    const QString root = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    const QFileInfo database(QDir(root).filePath(QStringLiteral(".bf/billfish.db")));
    if (!QFileInfo(root).isDir() || !database.exists() || !database.isFile()) {
        return;
    }
    const QString key = root.toCaseFolded();
    for (const QString &existing : std::as_const(*roots)) {
        if (existing.toCaseFolded() == key) {
            return;
        }
    }
    roots->append(root);
}

} // namespace

BillfishFileLocator::BillfishFileLocator(QString libraryRoot, QString sqliteLibraryPath)
    : libraryRoot_(libraryRoot.trimmed().isEmpty()
              ? QString()
              : QDir::cleanPath(QFileInfo(std::move(libraryRoot)).absoluteFilePath()))
    , sqliteLibraryPath_(std::move(sqliteLibraryPath))
{
    if (sqliteLibraryPath_.isEmpty()) {
        sqliteLibraryPath_ = findSqliteLibrary();
    } else {
        sqliteLibraryPath_ = normalizedExistingFile(sqliteLibraryPath_);
    }
}

QString BillfishFileLocator::libraryRoot() const
{
    return libraryRoot_;
}

QString BillfishFileLocator::databasePath() const
{
    return QDir(libraryRoot_).filePath(QStringLiteral(".bf/billfish.db"));
}

QString BillfishFileLocator::sqliteLibraryPath() const
{
    return sqliteLibraryPath_;
}

BillfishFileLookup BillfishFileLocator::locate(qint64 fileId) const
{
    BillfishFileLookup result;
    result.file.id = fileId;
    if (fileId <= 0) {
        result.error = QStringLiteral("A Billfish file identifier must be positive.");
        return result;
    }
    const QFileInfo rootInfo(libraryRoot_);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        result.error = QStringLiteral("The Billfish library root does not exist: %1").arg(libraryRoot_);
        return result;
    }
    const QString dbPath = databasePath();
    if (!QFileInfo(dbPath).isFile()) {
        result.error = QStringLiteral("The Billfish database does not exist: %1").arg(dbPath);
        return result;
    }
    if (sqliteLibraryPath_.isEmpty()) {
        result.error = QStringLiteral("sqlite3.dll was not found.");
        return result;
    }

    SqliteApi api(sqliteLibraryPath_);
    if (!api.load(&result.error)) {
        return result;
    }
    Database database(&api);
    if (!database.openReadOnly(dbPath, &result.error)) {
        return result;
    }
    if (!tableHasColumns(
            api,
            database,
            "PRAGMA table_info(\"bf_file\")",
            {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("pid")},
            &result.error)
        || !tableHasColumns(
            api,
            database,
            "PRAGMA table_info(\"bf_folder\")",
            {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("pid")},
            &result.error)) {
        return result;
    }

    NameAndParent file;
    if (!queryNameAndParent(
            api,
            database,
            "SELECT name, pid FROM bf_file WHERE id = ?1 LIMIT 1",
            fileId,
            &file,
            &result.error)) {
        return result;
    }
    if (!file.found) {
        result.error = QStringLiteral("Billfish file id %1 was not found.").arg(fileId);
        return result;
    }
    if (!isSafePathComponent(file.name)) {
        result.error = QStringLiteral("Billfish file id %1 has an unsafe file name.").arg(fileId);
        return result;
    }

    result.file.folderId = file.parentId;
    result.file.name = file.name;
    if (!queryOptionalVideoDuration(
            api,
            database,
            fileId,
            &result.file.durationMs,
            &result.error)) {
        return result;
    }
    QStringList components{file.name};
    QSet<qint64> visitedFolders;
    qint64 folderId = file.parentId;
    for (int depth = 0; folderId > 0; ++depth) {
        if (depth >= 256) {
            result.error = QStringLiteral("The Billfish folder hierarchy exceeds 256 levels.");
            return result;
        }
        if (visitedFolders.contains(folderId)) {
            result.error = QStringLiteral("The Billfish folder hierarchy contains a cycle at id %1.")
                               .arg(folderId);
            return result;
        }
        visitedFolders.insert(folderId);

        NameAndParent folder;
        if (!queryNameAndParent(
                api,
                database,
                "SELECT name, pid FROM bf_folder WHERE id = ?1 LIMIT 1",
                folderId,
                &folder,
                &result.error)) {
            return result;
        }
        if (!folder.found) {
            result.error = QStringLiteral("Billfish folder id %1 was not found.").arg(folderId);
            return result;
        }
        if (!isSafePathComponent(folder.name)) {
            result.error = QStringLiteral("Billfish folder id %1 has an unsafe name.").arg(folderId);
            return result;
        }
        components.prepend(folder.name);
        folderId = folder.parentId;
    }

    result.file.relativePath = components.join(QLatin1Char('/'));
    result.file.absolutePath = QDir(libraryRoot_).filePath(result.file.relativePath);
    result.file.exists = QFileInfo(result.file.absolutePath).isFile();
    result.success = true;
    return result;
}

QString BillfishFileLocator::defaultRecentLibraryConfigPath()
{
    QString roaming = qEnvironmentVariable("APPDATA");
    if (roaming.isEmpty()) {
        roaming = QDir::home().filePath(QStringLiteral("AppData/Roaming"));
    }
    return QDir(roaming).filePath(QStringLiteral("Billfish/config/recent_lib.ini"));
}

QStringList BillfishFileLocator::discoverLibraryRoots(const QString &recentLibraryConfigPath)
{
    const QString configPath = recentLibraryConfigPath.isEmpty()
        ? defaultRecentLibraryConfigPath()
        : recentLibraryConfigPath;
    if (!QFileInfo(configPath).isFile()) {
        return {};
    }

    QSettings settings(configPath, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings.setIniCodec("UTF-8");
#endif
    QStringList roots;
    appendUniqueRoot(
        &roots,
        settings.value(QStringLiteral("LoadDefaultLibType/LastLoadLibPath")).toString());
    appendUniqueRoot(
        &roots,
        settings.value(QStringLiteral("LoadDefaultLibType/CustomSettingLibPath")).toString());
    for (const QString &key : settings.allKeys()) {
        if (key.startsWith(QStringLiteral("ResourceLibsVector/"))
            && key.endsWith(QStringLiteral("/Path"), Qt::CaseInsensitive)) {
            appendUniqueRoot(&roots, settings.value(key).toString());
        }
    }
    return roots;
}

QString BillfishFileLocator::findSqliteLibrary(
    const QString &applicationDirectory,
    const QString &explicitLibraryPath)
{
#ifdef Q_OS_WIN
    const QString libraryName = QStringLiteral("sqlite3.dll");
#elif defined(Q_OS_MACOS)
    const QString libraryName = QStringLiteral("libsqlite3.dylib");
#else
    const QString libraryName = QStringLiteral("libsqlite3.so");
#endif

    QString explicitPath = normalizedExistingFile(explicitLibraryPath);
    if (!explicitPath.isEmpty()) {
        return explicitPath;
    }
    const QString environmentPath = normalizedExistingFile(
        qEnvironmentVariable("BILLFISH_SQLITE3"));
    if (!environmentPath.isEmpty()) {
        return environmentPath;
    }

    QStringList directories;
    if (!applicationDirectory.isEmpty()) {
        directories.append(applicationDirectory);
    }
    if (QCoreApplication::instance()) {
        directories.append(QCoreApplication::applicationDirPath());
    }
    for (const QString &directory : std::as_const(directories)) {
        const QString candidate = normalizedExistingFile(QDir(directory).filePath(libraryName));
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    return QStandardPaths::findExecutable(libraryName);
}

} // namespace billfish::trim
