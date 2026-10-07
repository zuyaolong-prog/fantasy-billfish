#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

namespace billfish::trim {

struct BillfishFileRecord {
    qint64 id = -1;
    qint64 folderId = -1;
    QString name;
    QString relativePath;
    QString absolutePath;
    // Billfish bf_material_video.duration is milliseconds. Older schemas and
    // non-video rows leave this at -1 so callers can fall back to ffprobe.
    qint64 durationMs = -1;
    bool exists = false;
};

struct BillfishFileLookup {
    bool success = false;
    BillfishFileRecord file;
    QString error;
};

class BillfishFileLocator final {
public:
    BillfishFileLocator(QString libraryRoot, QString sqliteLibraryPath = {});

    [[nodiscard]] QString libraryRoot() const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] QString sqliteLibraryPath() const;
    [[nodiscard]] BillfishFileLookup locate(qint64 fileId) const;

    [[nodiscard]] static QString defaultRecentLibraryConfigPath();
    [[nodiscard]] static QStringList discoverLibraryRoots(const QString &recentLibraryConfigPath = {});
    [[nodiscard]] static QString findSqliteLibrary(
        const QString &applicationDirectory = {},
        const QString &explicitLibraryPath = {});

private:
    QString libraryRoot_;
    QString sqliteLibraryPath_;
};

} // namespace billfish::trim
