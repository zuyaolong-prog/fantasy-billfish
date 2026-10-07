#include "ui/trim_export_worker.hpp"

#include <utility>

namespace billfish::trim {

TrimExportWorker::TrimExportWorker(FfmpegTools tools, TrimRequest request)
    : tools_(std::move(tools)), request_(std::move(request)) {}

void TrimExportWorker::requestCancel() noexcept {
    cancelRequested_.store(true, std::memory_order_release);
}

void TrimExportWorker::run() {
    const TrimResult result = runTrim(
        tools_, request_,
        [this] { return cancelRequested_.load(std::memory_order_acquire); },
        [this](const TrimProgress& progress) {
            emit progressChanged(static_cast<int>(progress.stage), progress.processedMs,
                                 progress.totalMs, progress.percent);
        });
    emit completed(result.success, result.outputPath, result.error, result.elapsedMs);
}

MediaProbeWorker::MediaProbeWorker(FfmpegTools tools, QString inputPath)
    : tools_(std::move(tools)), inputPath_(std::move(inputPath)) {}

void MediaProbeWorker::run() {
    const ProbeResult result = probeMedia(tools_, inputPath_);
    emit completed(inputPath_, result.success, result.media.durationMs, result.error);
}

}  // namespace billfish::trim
