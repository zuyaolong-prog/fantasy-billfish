#pragma once

#include "core/video_trim_core.hpp"

#include <QObject>

#include <atomic>

namespace billfish::trim {

class TrimExportWorker final : public QObject {
    Q_OBJECT

public:
    TrimExportWorker(FfmpegTools tools, TrimRequest request);

    void requestCancel() noexcept;

public slots:
    void run();

signals:
    void progressChanged(int stage, qint64 processedMs, qint64 totalMs, int percent);
    void completed(bool success, const QString& outputPath, const QString& error, qint64 elapsedMs);

private:
    FfmpegTools tools_;
    TrimRequest request_;
    std::atomic_bool cancelRequested_{false};
};

class MediaProbeWorker final : public QObject {
    Q_OBJECT

public:
    MediaProbeWorker(FfmpegTools tools, QString inputPath);

public slots:
    void run();

signals:
    void completed(const QString& inputPath, bool success, qint64 durationMs, const QString& error);

private:
    FfmpegTools tools_;
    QString inputPath_;
};

}  // namespace billfish::trim
