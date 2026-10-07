#include "clip_cache.hpp"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QLockFile>
#include <QtCore/QSaveFile>

#include <algorithm>
#include <memory>

namespace billfish::trim {
namespace {

constexpr int kManifestSchemaVersion = 1;

QString absoluteCleanPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString fingerprintPath(const QFileInfo &info)
{
    QString path = info.canonicalFilePath();
    if (path.isEmpty()) {
        path = info.absoluteFilePath();
    }
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return QDir::cleanPath(path);
}

QString modeName(TrimMode mode)
{
    return mode == TrimMode::FastCopy
        ? QStringLiteral("fast-copy")
        : QStringLiteral("accurate-reencode");
}

QString effectiveOutputExtension(const TrimRequest &request)
{
    QString extension = QFileInfo(request.outputPath).suffix().toCaseFolded();
    if (extension.isEmpty()) {
        extension = QFileInfo(request.inputPath).suffix().toCaseFolded();
    }
    return extension.isEmpty() ? QStringLiteral("mp4") : extension;
}

QString secondsToken(const qint64 milliseconds)
{
    QString value = QString::number(
        static_cast<double>(std::max<qint64>(0, milliseconds)) / 1000.0,
        'f', 3);
    while (value.endsWith(QLatin1Char('0'))) {
        value.chop(1);
    }
    if (value.endsWith(QLatin1Char('.'))) {
        value.chop(1);
    }
    return value + QLatin1Char('s');
}

QString readableClipFileName(const TrimRequest &request, const QString &extension)
{
    QString baseName = QFileInfo(request.inputPath).completeBaseName().trimmed();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("clip");
    }
    return QStringLiteral("%1_%2-%3.%4")
        .arg(baseName,
             secondsToken(request.range.startMs),
             secondsToken(request.range.endMs),
             extension);
}

bool parseMode(const QString &name, TrimMode *mode)
{
    if (name == QStringLiteral("fast-copy")) {
        *mode = TrimMode::FastCopy;
        return true;
    }
    if (name == QStringLiteral("accurate-reencode")) {
        *mode = TrimMode::AccurateReencode;
        return true;
    }
    return false;
}

QString int64String(qint64 value)
{
    return QString::number(value);
}

bool jsonInt64(const QJsonObject &object, const QString &name, qint64 *value)
{
    const QJsonValue json = object.value(name);
    bool ok = false;
    if (json.isString()) {
        *value = json.toString().toLongLong(&ok);
    } else if (json.isDouble()) {
        *value = static_cast<qint64>(json.toDouble());
        ok = true;
    }
    return ok;
}

QJsonObject entryToJson(const ClipCacheEntry &entry)
{
    return {
        {QStringLiteral("key"), entry.key},
        {QStringLiteral("inputPath"), entry.inputPath},
        {QStringLiteral("outputPath"), entry.outputPath},
        {QStringLiteral("inputSize"), int64String(entry.inputSize)},
        {QStringLiteral("inputModifiedMs"), int64String(entry.inputModifiedMs)},
        {QStringLiteral("outputSize"), int64String(entry.outputSize)},
        {QStringLiteral("startMs"), int64String(entry.range.startMs)},
        {QStringLiteral("endMs"), int64String(entry.range.endMs)},
        {QStringLiteral("mode"), modeName(entry.mode)},
        {QStringLiteral("createdUtcMs"), int64String(entry.createdUtcMs)},
        {QStringLiteral("lastAccessUtcMs"), int64String(entry.lastAccessUtcMs)},
    };
}

bool entryFromJson(const QJsonValue &value, ClipCacheEntry *entry, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("The clip cache contains a non-object entry.");
        return false;
    }
    const QJsonObject object = value.toObject();
    entry->key = object.value(QStringLiteral("key")).toString();
    entry->inputPath = object.value(QStringLiteral("inputPath")).toString();
    entry->outputPath = object.value(QStringLiteral("outputPath")).toString();
    if (entry->key.isEmpty() || entry->inputPath.isEmpty() || entry->outputPath.isEmpty()
        || !jsonInt64(object, QStringLiteral("inputSize"), &entry->inputSize)
        || !jsonInt64(object, QStringLiteral("inputModifiedMs"), &entry->inputModifiedMs)
        || !jsonInt64(object, QStringLiteral("outputSize"), &entry->outputSize)
        || !jsonInt64(object, QStringLiteral("startMs"), &entry->range.startMs)
        || !jsonInt64(object, QStringLiteral("endMs"), &entry->range.endMs)
        || !jsonInt64(object, QStringLiteral("createdUtcMs"), &entry->createdUtcMs)
        || !jsonInt64(object, QStringLiteral("lastAccessUtcMs"), &entry->lastAccessUtcMs)
        || !parseMode(object.value(QStringLiteral("mode")).toString(), &entry->mode)
        || !entry->range.isValid()) {
        *error = QStringLiteral("The clip cache contains an invalid entry.");
        return false;
    }
    return true;
}

bool ensureRoot(const QString &root, QString *error)
{
    if (root.trimmed().isEmpty()) {
        *error = QStringLiteral("The clip cache root is empty.");
        return false;
    }
    if (!QDir().mkpath(root)) {
        *error = QStringLiteral("Could not create the clip cache root: %1").arg(root);
        return false;
    }
    const QFileInfo info(root);
    if (!info.exists() || !info.isDir() || !info.isWritable()) {
        *error = QStringLiteral("The clip cache root is not a writable directory: %1").arg(root);
        return false;
    }
    return true;
}

std::unique_ptr<QLockFile> lockCache(const QString &root, QString *error)
{
    if (!ensureRoot(root, error)) {
        return {};
    }
    auto lock = std::make_unique<QLockFile>(QDir(root).filePath(QStringLiteral(".clip-cache.lock")));
    lock->setStaleLockTime(30'000);
    if (!lock->tryLock(10'000)) {
        *error = QStringLiteral("Could not acquire the clip cache lock: %1")
                     .arg(lock->error() == QLockFile::LockFailedError
                              ? QStringLiteral("already locked")
                              : QStringLiteral("I/O error"));
        return {};
    }
    return lock;
}

bool loadEntries(const QString &manifestPath, QVector<ClipCacheEntry> *entries, QString *error)
{
    entries->clear();
    QFile file(manifestPath);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not read the clip cache manifest: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("The clip cache manifest is invalid JSON: %1")
                     .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != kManifestSchemaVersion) {
        *error = QStringLiteral("The clip cache manifest has an unsupported schema version.");
        return false;
    }
    const QJsonValue entriesValue = root.value(QStringLiteral("entries"));
    if (!entriesValue.isArray()) {
        *error = QStringLiteral("The clip cache manifest does not contain an entries array.");
        return false;
    }

    const QJsonArray array = entriesValue.toArray();
    entries->reserve(array.size());
    for (const QJsonValue &value : array) {
        ClipCacheEntry entry;
        if (!entryFromJson(value, &entry, error)) {
            return false;
        }
        entries->append(entry);
    }
    return true;
}

bool saveEntries(const QString &manifestPath, const QVector<ClipCacheEntry> &entries, QString *error)
{
    QJsonArray array;
    for (const ClipCacheEntry &entry : entries) {
        array.append(entryToJson(entry));
    }
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kManifestSchemaVersion},
        {QStringLiteral("entries"), array},
    };

    QSaveFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Could not write the clip cache manifest: %1").arg(file.errorString());
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        *error = QStringLiteral("Could not fully write the clip cache manifest: %1")
                     .arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = QStringLiteral("Could not atomically commit the clip cache manifest: %1")
                     .arg(file.errorString());
        return false;
    }
    return true;
}

bool isManagedClip(const QString &root, const QString &path)
{
    QString managedRoot = QDir::fromNativeSeparators(
        absoluteCleanPath(QDir(root).filePath(QStringLiteral("clips"))));
    QString candidate = QDir::fromNativeSeparators(absoluteCleanPath(path));
#ifdef Q_OS_WIN
    managedRoot = managedRoot.toCaseFolded();
    candidate = candidate.toCaseFolded();
#endif
    if (candidate == managedRoot) {
        return false;
    }
    return candidate.startsWith(managedRoot + QLatin1Char('/'));
}

void removeManagedOutput(const QString &root, const ClipCacheEntry &entry)
{
    if (isManagedClip(root, entry.outputPath)) {
        const QString parentPath = QFileInfo(entry.outputPath).absolutePath();
        QFile::remove(entry.outputPath);
        const QString clipsRoot = absoluteCleanPath(
            QDir(root).filePath(QStringLiteral("clips")));
        QDir parent(parentPath);
        if (absoluteCleanPath(parentPath) != clipsRoot &&
            parent.isEmpty(QDir::NoDotAndDotDot | QDir::AllEntries)) {
            QDir().rmdir(parentPath);
        }
    }
}

} // namespace

ClipCache::ClipCache(QString rootDirectory)
    : rootDirectory_(absoluteCleanPath(std::move(rootDirectory)))
{
}

QString ClipCache::rootDirectory() const
{
    return rootDirectory_;
}

QString ClipCache::manifestPath() const
{
    return QDir(rootDirectory_).filePath(QStringLiteral("manifest.json"));
}

QString ClipCache::keyFor(const TrimRequest &request) const
{
    const QFileInfo inputInfo(request.inputPath);
    QByteArray serialized;
    QDataStream stream(&serialized, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << QStringLiteral("billfish-video-trim-cache-key-v1")
           << fingerprintPath(inputInfo)
           << static_cast<qint64>(inputInfo.exists() ? inputInfo.size() : -1)
           << static_cast<qint64>(inputInfo.exists() ? inputInfo.lastModified().toMSecsSinceEpoch() : -1)
           << request.range.startMs
           << request.range.endMs
           << static_cast<qint32>(request.mode)
           << request.preserveMetadata
           << request.videoCodec
           << request.audioCodec
           << static_cast<qint32>(request.videoCrf)
           << request.encoderPreset
           << static_cast<qint32>(request.audioBitrateKbps)
           << request.extraOutputArguments
           << effectiveOutputExtension(request);
    return QString::fromLatin1(QCryptographicHash::hash(serialized, QCryptographicHash::Sha256).toHex());
}

QString ClipCache::outputPathFor(const TrimRequest &request) const
{
    if (rootDirectory_.isEmpty()) {
        return {};
    }
    const QString extension = effectiveOutputExtension(request);
    return QDir(rootDirectory_).filePath(
        QStringLiteral("clips/%1/%2")
            .arg(keyFor(request), readableClipFileName(request, extension)));
}

ClipCacheLookup ClipCache::lookup(const TrimRequest &request, bool touch) const
{
    ClipCacheLookup lookup;
    auto lock = lockCache(rootDirectory_, &lookup.error);
    if (!lock) {
        return lookup;
    }

    QVector<ClipCacheEntry> allEntries;
    if (!loadEntries(manifestPath(), &allEntries, &lookup.error)) {
        return lookup;
    }
    const QString key = keyFor(request);
    const auto found = std::find_if(allEntries.begin(), allEntries.end(), [&](const ClipCacheEntry &entry) {
        return entry.key == key;
    });
    if (found == allEntries.end()) {
        return lookup;
    }

    const QString requestedOutput = absoluteCleanPath(request.outputPath);
    if (!requestedOutput.isEmpty() &&
        isManagedClip(rootDirectory_, found->outputPath) &&
        isManagedClip(rootDirectory_, requestedOutput) &&
        QString::compare(absoluteCleanPath(found->outputPath), requestedOutput,
#ifdef Q_OS_WIN
                         Qt::CaseInsensitive
#else
                         Qt::CaseSensitive
#endif
                         ) != 0) {
        // Cache keys from earlier builds are still valid, but their hash-only
        // filenames are not. Invalidate that managed entry so the next copy
        // recreates it with the readable source-and-seconds filename.
        const ClipCacheEntry legacyEntry = *found;
        allEntries.erase(found);
        if (!saveEntries(manifestPath(), allEntries, &lookup.error)) {
            return lookup;
        }
        removeManagedOutput(rootDirectory_, legacyEntry);
        return lookup;
    }

    const QFileInfo outputInfo(found->outputPath);
    if (!outputInfo.exists() || !outputInfo.isFile() || outputInfo.size() <= 0
        || outputInfo.size() != found->outputSize) {
        const ClipCacheEntry staleEntry = *found;
        allEntries.erase(found);
        if (!saveEntries(manifestPath(), allEntries, &lookup.error)) {
            return lookup;
        }
        removeManagedOutput(rootDirectory_, staleEntry);
        return lookup;
    }

    if (touch) {
        found->lastAccessUtcMs = QDateTime::currentMSecsSinceEpoch();
        if (!saveEntries(manifestPath(), allEntries, &lookup.error)) {
            return lookup;
        }
    }
    lookup.hit = true;
    lookup.entry = *found;
    return lookup;
}

bool ClipCache::record(const TrimRequest &request, const QString &outputPath, QString *error) const
{
    QString localError;
    QString &reportedError = error ? *error : localError;
    reportedError.clear();
    const QFileInfo inputInfo(request.inputPath);
    const QFileInfo outputInfo(outputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        reportedError = QStringLiteral("Cannot cache a clip whose input is missing: %1")
                            .arg(request.inputPath);
        return false;
    }
    if (!request.range.isValid()) {
        reportedError = QStringLiteral("Cannot cache a clip with an invalid range.");
        return false;
    }
    if (!outputInfo.exists() || !outputInfo.isFile() || outputInfo.size() <= 0) {
        reportedError = QStringLiteral("Cannot cache a missing or empty output: %1").arg(outputPath);
        return false;
    }

    auto lock = lockCache(rootDirectory_, &reportedError);
    if (!lock) {
        return false;
    }
    QVector<ClipCacheEntry> allEntries;
    if (!loadEntries(manifestPath(), &allEntries, &reportedError)) {
        return false;
    }

    const QString key = keyFor(request);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ClipCacheEntry entry;
    entry.key = key;
    entry.inputPath = fingerprintPath(inputInfo);
    entry.outputPath = outputInfo.absoluteFilePath();
    entry.inputSize = inputInfo.size();
    entry.inputModifiedMs = inputInfo.lastModified().toMSecsSinceEpoch();
    entry.outputSize = outputInfo.size();
    entry.range = request.range;
    entry.mode = request.mode;
    entry.createdUtcMs = now;
    entry.lastAccessUtcMs = now;

    const auto found = std::find_if(allEntries.begin(), allEntries.end(), [&](const ClipCacheEntry &candidate) {
        return candidate.key == key;
    });
    ClipCacheEntry supersededEntry;
    bool hasSupersededEntry = false;
    if (found == allEntries.end()) {
        allEntries.append(entry);
    } else {
        if (absoluteCleanPath(found->outputPath) != absoluteCleanPath(entry.outputPath)) {
            supersededEntry = *found;
            hasSupersededEntry = true;
        }
        entry.createdUtcMs = found->createdUtcMs;
        *found = entry;
    }
    if (!saveEntries(manifestPath(), allEntries, &reportedError)) {
        return false;
    }
    if (hasSupersededEntry) {
        removeManagedOutput(rootDirectory_, supersededEntry);
    }
    return true;
}

QVector<ClipCacheEntry> ClipCache::entries(QString *error) const
{
    QString localError;
    QString &reportedError = error ? *error : localError;
    reportedError.clear();
    auto lock = lockCache(rootDirectory_, &reportedError);
    if (!lock) {
        return {};
    }
    QVector<ClipCacheEntry> result;
    if (!loadEntries(manifestPath(), &result, &reportedError)) {
        return {};
    }
    return result;
}

ClipCachePruneResult ClipCache::prune(
    qint64 maximumBytes,
    int maximumEntries,
    const QSet<QString> &protectedKeys) const
{
    ClipCachePruneResult result;
    if (maximumBytes < 0 || maximumEntries < 0) {
        result.error = QStringLiteral("Clip cache limits must not be negative.");
        return result;
    }
    auto lock = lockCache(rootDirectory_, &result.error);
    if (!lock) {
        return result;
    }
    QVector<ClipCacheEntry> allEntries;
    if (!loadEntries(manifestPath(), &allEntries, &result.error)) {
        return result;
    }

    std::sort(allEntries.begin(), allEntries.end(), [](const ClipCacheEntry &lhs, const ClipCacheEntry &rhs) {
        if (lhs.lastAccessUtcMs != rhs.lastAccessUtcMs) {
            return lhs.lastAccessUtcMs < rhs.lastAccessUtcMs;
        }
        return lhs.createdUtcMs < rhs.createdUtcMs;
    });
    qint64 totalBytes = 0;
    for (const ClipCacheEntry &entry : allEntries) {
        totalBytes += std::max<qint64>(0, entry.outputSize);
    }

    QVector<ClipCacheEntry> removed;
    while (!allEntries.isEmpty()
           && (totalBytes > maximumBytes || allEntries.size() > maximumEntries)) {
        const auto removable = std::find_if(
            allEntries.begin(),
            allEntries.end(),
            [&](const ClipCacheEntry &entry) { return !protectedKeys.contains(entry.key); });
        if (removable == allEntries.end()) {
            break;
        }
        const ClipCacheEntry entry = *removable;
        allEntries.erase(removable);
        removed.append(entry);
        const qint64 bytes = std::max<qint64>(0, entry.outputSize);
        totalBytes -= bytes;
        result.removedBytes += bytes;
        ++result.removedEntries;
    }
    if (!saveEntries(manifestPath(), allEntries, &result.error)) {
        return result;
    }
    for (const ClipCacheEntry &entry : std::as_const(removed)) {
        removeManagedOutput(rootDirectory_, entry);
    }
    result.limitsSatisfied = totalBytes <= maximumBytes
        && allEntries.size() <= maximumEntries;
    result.success = true;
    return result;
}

bool ClipCache::remove(const QString &key, QString *error) const
{
    QString localError;
    QString &reportedError = error ? *error : localError;
    reportedError.clear();
    auto lock = lockCache(rootDirectory_, &reportedError);
    if (!lock) {
        return false;
    }
    QVector<ClipCacheEntry> allEntries;
    if (!loadEntries(manifestPath(), &allEntries, &reportedError)) {
        return false;
    }
    const auto found = std::find_if(allEntries.begin(), allEntries.end(), [&](const ClipCacheEntry &entry) {
        return entry.key == key;
    });
    if (found == allEntries.end()) {
        return true;
    }
    const ClipCacheEntry removed = *found;
    allEntries.erase(found);
    if (!saveEntries(manifestPath(), allEntries, &reportedError)) {
        return false;
    }
    removeManagedOutput(rootDirectory_, removed);
    return true;
}

bool ClipCache::clear(QString *error) const
{
    QString localError;
    QString &reportedError = error ? *error : localError;
    reportedError.clear();
    auto lock = lockCache(rootDirectory_, &reportedError);
    if (!lock) {
        return false;
    }
    QVector<ClipCacheEntry> allEntries;
    if (!loadEntries(manifestPath(), &allEntries, &reportedError)) {
        return false;
    }
    if (!saveEntries(manifestPath(), {}, &reportedError)) {
        return false;
    }
    for (const ClipCacheEntry &entry : std::as_const(allEntries)) {
        removeManagedOutput(rootDirectory_, entry);
    }
    return true;
}

} // namespace billfish::trim
