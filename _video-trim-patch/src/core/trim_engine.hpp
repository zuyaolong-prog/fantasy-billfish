#pragma once

#include "ffmpeg_tools.hpp"
#include "range_selection.hpp"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <functional>

namespace billfish::trim {

enum class TrimMode {
    FastCopy,
    AccurateReencode,
};

struct TrimRequest {
    QString inputPath;
    QString outputPath;
    RangeSelection range;
    TrimMode mode = TrimMode::AccurateReencode;
    bool overwrite = false;
    bool preserveMetadata = true;
    QString videoCodec = QStringLiteral("libx264");
    QString audioCodec = QStringLiteral("aac");
    int videoCrf = 18;
    QString encoderPreset = QStringLiteral("medium");
    int audioBitrateKbps = 192;
    QStringList extraOutputArguments;
    qint64 processTimeoutMs = 0;
    qint64 validationToleranceMs = -1;
};

struct ValidationResult {
    bool success = false;
    QString error;
    QStringList warnings;
    qint64 expectedDurationMs = -1;
    qint64 actualDurationMs = -1;
    qint64 durationDeltaMs = -1;
};

struct TrimResult {
    bool success = false;
    QString outputPath;
    QString error;
    ProcessResult process;
    MediaProbe inputProbe;
    MediaProbe outputProbe;
    ValidationResult validation;
    qint64 elapsedMs = 0;
};

enum class TrimStage {
    Preparing,
    ProbingInput,
    Trimming,
    ProbingOutput,
    Validating,
    Committing,
    Completed,
};

struct TrimProgress {
    TrimStage stage = TrimStage::Preparing;
    // Raw FFmpeg out_time value in microseconds. It may exceed totalMs for
    // stream-copy cuts that begin before the requested keyframe.
    qint64 outTimeUs = 0;
    // Clamped selection-relative progress for UI display.
    qint64 processedMs = 0;
    qint64 totalMs = 0;
    double fraction = 0.0;
    int percent = 0;
};

// Called synchronously on the thread that invokes runTrim. UI clients should
// marshal updates to the GUI thread instead of touching widgets directly.
using TrimProgressCallback = std::function<void(const TrimProgress &)>;

[[nodiscard]] QString validateTrimRequest(
    const TrimRequest &request,
    qint64 inputDurationMs = -1);

[[nodiscard]] QStringList buildTrimArguments(
    const TrimRequest &request,
    const QString &effectiveOutputPath = {});

[[nodiscard]] ValidationResult validateTrimOutput(
    const TrimRequest &request,
    const MediaProbe &inputProbe,
    const MediaProbe &outputProbe,
    const QString &effectiveOutputPath = {});

[[nodiscard]] TrimResult runTrim(
    const FfmpegTools &tools,
    const TrimRequest &request,
    const std::function<bool()> &cancelRequested = {},
    const TrimProgressCallback &progressChanged = {});

} // namespace billfish::trim
