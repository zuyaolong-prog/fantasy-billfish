#pragma once

#include "trim_engine.hpp"

#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtCore/QVector>
#include <QtCore/QtGlobal>

namespace billfish::trim {

struct ClipCacheEntry {
    QString key;
    QString inputPath;
    QString outputPath;
    qint64 inputSize = -1;
    qint64 inputModifiedMs = -1;
    qint64 outputSize = -1;
    RangeSelection range;
    TrimMode mode = TrimMode::AccurateReencode;
    qint64 createdUtcMs = 0;
    qint64 lastAccessUtcMs = 0;
};

struct ClipCacheLookup {
    bool hit = false;
    ClipCacheEntry entry;
    QString error;
};

struct ClipCachePruneResult {
    bool success = false;
    bool limitsSatisfied = false;
    int removedEntries = 0;
    qint64 removedBytes = 0;
    QString error;
};

class ClipCache final {
public:
    explicit ClipCache(QString rootDirectory);

    [[nodiscard]] QString rootDirectory() const;
    [[nodiscard]] QString manifestPath() const;
    [[nodiscard]] QString keyFor(const TrimRequest &request) const;
    // Stable before and after assigning the returned path to request.outputPath.
    [[nodiscard]] QString outputPathFor(const TrimRequest &request) const;

    [[nodiscard]] ClipCacheLookup lookup(const TrimRequest &request, bool touch = true) const;
    [[nodiscard]] bool record(
        const TrimRequest &request,
        const QString &outputPath,
        QString *error = nullptr) const;
    [[nodiscard]] QVector<ClipCacheEntry> entries(QString *error = nullptr) const;
    [[nodiscard]] ClipCachePruneResult prune(
        qint64 maximumBytes,
        int maximumEntries,
        const QSet<QString> &protectedKeys = {}) const;
    [[nodiscard]] bool remove(const QString &key, QString *error = nullptr) const;
    [[nodiscard]] bool clear(QString *error = nullptr) const;

private:
    QString rootDirectory_;
};

} // namespace billfish::trim
