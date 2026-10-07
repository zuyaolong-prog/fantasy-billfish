#include "trim_engine.hpp"

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace billfish::trim {
namespace {

QString comparablePath(const QString &path)
{
    QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool hasFastStartContainer(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toCaseFolded();
    return suffix == QStringLiteral("mp4") || suffix == QStringLiteral("mov")
        || suffix == QStringLiteral("m4v");
}

bool parseFfmpegProgressTime(const QByteArray &line, qint64 *outTimeUs)
{
    const int separator = line.indexOf('=');
    if (separator <= 0) {
        return false;
    }
    const QByteArray key = line.left(separator).trimmed();
    const QByteArray value = line.mid(separator + 1).trimmed();
    if (key == "out_time_us" || key == "out_time_ms") {
        bool ok = false;
        const qint64 parsed = value.toLongLong(&ok);
        if (ok && parsed >= 0) {
            // Despite its historical name, ffmpeg's out_time_ms value is also
            // expressed in microseconds. Newer versions expose out_time_us.
            *outTimeUs = parsed;
            return true;
        }
        return false;
    }
    if (key != "out_time") {
        return false;
    }

    const QList<QByteArray> parts = value.split(':');
    if (parts.size() != 3) {
        return false;
    }
    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    const qint64 hours = parts.at(0).toLongLong(&hoursOk);
    const qint64 minutes = parts.at(1).toLongLong(&minutesOk);
    const double seconds = parts.at(2).toDouble(&secondsOk);
    if (!hoursOk || !minutesOk || !secondsOk || hours < 0 || minutes < 0
        || seconds < 0.0) {
        return false;
    }
    const long double totalMicroseconds = static_cast<long double>(hours) * 3'600'000'000.0L
        + static_cast<long double>(minutes) * 60'000'000.0L
        + static_cast<long double>(seconds) * 1'000'000.0L;
    if (totalMicroseconds > static_cast<long double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    *outTimeUs = static_cast<qint64>(std::llround(totalMicroseconds));
    return true;
}

void reportProgress(
    const TrimProgressCallback &callback,
    TrimStage stage,
    qint64 outTimeUs,
    qint64 totalMs)
{
    if (!callback) {
        return;
    }
    TrimProgress progress;
    progress.stage = stage;
    progress.outTimeUs = std::max<qint64>(0, outTimeUs);
    progress.totalMs = std::max<qint64>(0, totalMs);
    progress.processedMs = progress.outTimeUs / 1'000;
    if (progress.totalMs > 0) {
        progress.processedMs = std::clamp(
            progress.processedMs,
            qint64{0},
            progress.totalMs);
        progress.fraction = std::clamp(
            static_cast<double>(progress.processedMs)
                / static_cast<double>(progress.totalMs),
            0.0,
            1.0);
    } else if (stage == TrimStage::Completed) {
        progress.fraction = 1.0;
    }
    progress.percent = static_cast<int>(std::llround(progress.fraction * 100.0));
    callback(progress);
}

QString makeTemporaryOutputPath(const QString &outputPath)
{
    const QFileInfo outputInfo(outputPath);
    const QString suffix = outputInfo.completeSuffix();
    QString fileName = QStringLiteral(".%1.%2.trim-part")
                           .arg(outputInfo.completeBaseName(),
                                QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!suffix.isEmpty()) {
        fileName += QLatin1Char('.') + suffix;
    }
    return outputInfo.dir().filePath(fileName);
}

QString makeBackupPath(const QString &outputPath)
{
    const QFileInfo outputInfo(outputPath);
    return outputInfo.dir().filePath(
        QStringLiteral(".%1.%2.trim-backup")
            .arg(outputInfo.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

bool commitOutput(
    const QString &temporaryPath,
    const QString &outputPath,
    bool overwrite,
    QString *error)
{
    const QFileInfo outputInfo(outputPath);
    QString backupPath;
    if (outputInfo.exists()) {
        if (!overwrite) {
            *error = QStringLiteral("The output appeared while trimming and overwrite is disabled: %1")
                         .arg(outputPath);
            return false;
        }
        backupPath = makeBackupPath(outputPath);
        if (!QFile::rename(outputPath, backupPath)) {
            *error = QStringLiteral("Could not preserve the existing output before replacement: %1")
                         .arg(outputPath);
            return false;
        }
    }

    if (!QFile::rename(temporaryPath, outputPath)) {
        if (!backupPath.isEmpty()) {
            QFile::rename(backupPath, outputPath);
        }
        *error = QStringLiteral("Could not move the validated clip to its destination: %1")
                     .arg(outputPath);
        return false;
    }

    if (!backupPath.isEmpty() && !QFile::remove(backupPath)) {
        // The new output is already committed. A stale backup is recoverable and must not
        // turn a successful trim into a reported failure.
    }
    return true;
}

} // namespace

QString validateTrimRequest(const TrimRequest &request, qint64 inputDurationMs)
{
    if (request.inputPath.trimmed().isEmpty()) {
        return QStringLiteral("The input path is empty.");
    }
    const QFileInfo inputInfo(request.inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        return QStringLiteral("The input file does not exist: %1").arg(request.inputPath);
    }
    if (!inputInfo.isReadable()) {
        return QStringLiteral("The input file is not readable: %1").arg(request.inputPath);
    }
    if (request.outputPath.trimmed().isEmpty()) {
        return QStringLiteral("The output path is empty.");
    }
    if (comparablePath(request.inputPath) == comparablePath(request.outputPath)) {
        return QStringLiteral("The output path must differ from the input path.");
    }

    const QString rangeError = request.range.validationError(inputDurationMs);
    if (!rangeError.isEmpty()) {
        return rangeError;
    }

    const QFileInfo outputInfo(request.outputPath);
    if (outputInfo.exists() && outputInfo.isDir()) {
        return QStringLiteral("The output path names a directory: %1").arg(request.outputPath);
    }
    if (outputInfo.exists() && !request.overwrite) {
        return QStringLiteral("The output already exists and overwrite is disabled: %1")
            .arg(request.outputPath);
    }

    if (request.mode == TrimMode::AccurateReencode) {
        if (request.videoCodec.trimmed().isEmpty() || request.audioCodec.trimmed().isEmpty()) {
            return QStringLiteral("Accurate trimming requires non-empty video and audio codecs.");
        }
        if (request.videoCodec.compare(QStringLiteral("copy"), Qt::CaseInsensitive) == 0
            || request.audioCodec.compare(QStringLiteral("copy"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("Accurate trimming cannot use the stream-copy codec.");
        }
        if (request.videoCrf < 0 || request.videoCrf > 63) {
            return QStringLiteral("The video CRF must be between 0 and 63.");
        }
        if (request.encoderPreset.trimmed().isEmpty()) {
            return QStringLiteral("The encoder preset is empty.");
        }
        if (request.audioBitrateKbps <= 0) {
            return QStringLiteral("The audio bitrate must be positive.");
        }
    }
    if (request.processTimeoutMs < 0) {
        return QStringLiteral("The process timeout must not be negative.");
    }
    if (request.validationToleranceMs < -1) {
        return QStringLiteral("The validation tolerance must be -1 or non-negative.");
    }
    return {};
}

QStringList buildTrimArguments(const TrimRequest &request, const QString &effectiveOutputPath)
{
    const QString outputPath = effectiveOutputPath.isEmpty()
        ? request.outputPath
        : effectiveOutputPath;
    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-progress"),
        QStringLiteral("pipe:1"),
        QStringLiteral("-nostats"),
        request.overwrite ? QStringLiteral("-y") : QStringLiteral("-n"),
    };

    if (request.mode == TrimMode::FastCopy) {
        arguments << QStringLiteral("-ss") << formatTimestampMs(request.range.startMs);
    }
    arguments << QStringLiteral("-i") << QFileInfo(request.inputPath).absoluteFilePath();
    if (request.mode == TrimMode::AccurateReencode) {
        arguments << QStringLiteral("-ss") << formatTimestampMs(request.range.startMs);
    }
    arguments << QStringLiteral("-t") << formatTimestampMs(request.range.durationMs())
              << QStringLiteral("-map") << QStringLiteral("0:v:0?")
              << QStringLiteral("-map") << QStringLiteral("0:a?")
              << QStringLiteral("-sn")
              << QStringLiteral("-dn");

    if (request.preserveMetadata) {
        arguments << QStringLiteral("-map_metadata") << QStringLiteral("0");
    } else {
        arguments << QStringLiteral("-map_metadata") << QStringLiteral("-1");
    }

    if (request.mode == TrimMode::FastCopy) {
        arguments << QStringLiteral("-c") << QStringLiteral("copy");
    } else {
        arguments << QStringLiteral("-c:v") << request.videoCodec
                  << QStringLiteral("-preset") << request.encoderPreset
                  << QStringLiteral("-crf") << QString::number(request.videoCrf)
                  << QStringLiteral("-c:a") << request.audioCodec
                  << QStringLiteral("-b:a")
                  << QStringLiteral("%1k").arg(request.audioBitrateKbps);
    }

    arguments << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero");
    if (hasFastStartContainer(outputPath)) {
        arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    arguments << request.extraOutputArguments << outputPath;
    return arguments;
}

ValidationResult validateTrimOutput(
    const TrimRequest &request,
    const MediaProbe &inputProbe,
    const MediaProbe &outputProbe,
    const QString &effectiveOutputPath)
{
    ValidationResult result;
    result.expectedDurationMs = request.range.durationMs();
    result.actualDurationMs = outputProbe.durationMs;

    const QString outputPath = effectiveOutputPath.isEmpty()
        ? request.outputPath
        : effectiveOutputPath;
    const QFileInfo outputInfo(outputPath);
    if (!outputInfo.exists() || !outputInfo.isFile() || outputInfo.size() <= 0) {
        result.error = QStringLiteral("The trim output is missing or empty: %1").arg(outputPath);
        return result;
    }
    if (!outputProbe.isValid()) {
        result.error = QStringLiteral("The trim output is not valid playable media.");
        return result;
    }
    if (inputProbe.hasVideo && !outputProbe.hasVideo) {
        result.error = QStringLiteral("The trim output lost the video stream.");
        return result;
    }
    if (inputProbe.hasAudio && !outputProbe.hasAudio) {
        result.error = QStringLiteral("The trim output lost the audio stream.");
        return result;
    }

    result.durationDeltaMs = std::abs(result.actualDurationMs - result.expectedDurationMs);
    qint64 toleranceMs = request.validationToleranceMs;
    if (toleranceMs < 0) {
        toleranceMs = request.mode == TrimMode::AccurateReencode ? 500 : 2'500;
        if (request.mode == TrimMode::AccurateReencode && inputProbe.frameRate > 0.0) {
            toleranceMs = std::max<qint64>(
                toleranceMs,
                static_cast<qint64>(std::ceil(2'000.0 / inputProbe.frameRate)));
        }
    }
    if (result.durationDeltaMs > toleranceMs) {
        result.error = QStringLiteral(
                           "The output duration differs from the requested duration by %1 ms "
                           "(allowed: %2 ms).")
                           .arg(result.durationDeltaMs)
                           .arg(toleranceMs);
        return result;
    }
    if (result.durationDeltaMs > 0) {
        result.warnings.append(
            QStringLiteral("Container duration differs from the requested duration by %1 ms.")
                .arg(result.durationDeltaMs));
    }
    result.success = true;
    return result;
}

TrimResult runTrim(
    const FfmpegTools &tools,
    const TrimRequest &request,
    const std::function<bool()> &cancelRequested,
    const TrimProgressCallback &progressChanged)
{
    TrimResult result;
    QElapsedTimer timer;
    timer.start();
    result.outputPath = QFileInfo(request.outputPath).absoluteFilePath();
    reportProgress(progressChanged, TrimStage::Preparing, 0, 0);

    result.error = validateTrimRequest(request);
    if (!result.error.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if (!tools.canTrim()) {
        result.error = tools.trimCapabilityError(false);
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("FFmpeg executable is unavailable.");
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if (request.mode == TrimMode::AccurateReencode && !tools.canAccurateTrim()) {
        result.error = tools.trimCapabilityError(true);
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("The selected FFmpeg executable cannot perform accurate trimming.");
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if (!tools.canProbe()) {
        result.error = tools.probeCapabilityError();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("FFprobe executable is unavailable; validated trimming cannot run.");
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }

    reportProgress(progressChanged, TrimStage::ProbingInput, 0, 0);
    const ProbeResult input = probeMedia(tools, request.inputPath, 30'000, cancelRequested);
    result.inputProbe = input.media;
    if (!input.success) {
        result.error = QStringLiteral("Could not probe the input: %1").arg(input.error);
        result.elapsedMs = timer.elapsed();
        return result;
    }
    result.error = validateTrimRequest(request, input.media.durationMs);
    if (!result.error.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }

    const QFileInfo outputInfo(request.outputPath);
    QDir outputDirectory(outputInfo.absolutePath());
    if (!outputDirectory.exists() && !QDir().mkpath(outputDirectory.absolutePath())) {
        result.error = QStringLiteral("Could not create the output directory: %1")
                           .arg(outputDirectory.absolutePath());
        result.elapsedMs = timer.elapsed();
        return result;
    }

    const QString temporaryPath = makeTemporaryOutputPath(outputInfo.absoluteFilePath());
    ProcessSpec spec;
    spec.program = tools.ffmpegPath;
    spec.arguments = buildTrimArguments(request, temporaryPath);
    spec.workingDirectory = outputDirectory.absolutePath();
    spec.timeoutMs = request.processTimeoutMs;
    const qint64 totalMs = request.range.durationMs();
    qint64 latestOutTimeUs = 0;
    QByteArray progressBuffer;
    reportProgress(progressChanged, TrimStage::Trimming, 0, totalMs);
    const ProcessOutputCallback processOutput = [&](const QByteArray &standardOutput, const QByteArray &) {
        progressBuffer += standardOutput;
        while (true) {
            const int newline = progressBuffer.indexOf('\n');
            if (newline < 0) {
                break;
            }
            QByteArray line = progressBuffer.left(newline);
            progressBuffer.remove(0, newline + 1);
            if (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }
            qint64 parsedOutTimeUs = 0;
            if (parseFfmpegProgressTime(line, &parsedOutTimeUs)
                && parsedOutTimeUs > latestOutTimeUs) {
                latestOutTimeUs = parsedOutTimeUs;
                reportProgress(
                    progressChanged,
                    TrimStage::Trimming,
                    latestOutTimeUs,
                    totalMs);
            }
        }
    };
    result.process = runProcess(spec, cancelRequested, processOutput);
    if (!result.process.succeeded()) {
        QFile::remove(temporaryPath);
        result.error = result.process.error.isEmpty()
            ? QStringLiteral("FFmpeg executable \"%1\" failed.")
                  .arg(QDir::toNativeSeparators(QFileInfo(tools.ffmpegPath).absoluteFilePath()))
            : result.process.error;
        result.elapsedMs = timer.elapsed();
        return result;
    }
    const qint64 requestedOutTimeUs = totalMs > std::numeric_limits<qint64>::max() / 1'000
        ? std::numeric_limits<qint64>::max()
        : totalMs * 1'000;
    latestOutTimeUs = std::max(latestOutTimeUs, requestedOutTimeUs);
    reportProgress(
        progressChanged,
        TrimStage::Trimming,
        latestOutTimeUs,
        totalMs);

    reportProgress(
        progressChanged,
        TrimStage::ProbingOutput,
        latestOutTimeUs,
        totalMs);
    const ProbeResult output = probeMedia(tools, temporaryPath, 30'000, cancelRequested);
    result.outputProbe = output.media;
    if (!output.success) {
        QFile::remove(temporaryPath);
        result.error = QStringLiteral("Could not validate the trim output: %1").arg(output.error);
        result.elapsedMs = timer.elapsed();
        return result;
    }

    reportProgress(
        progressChanged,
        TrimStage::Validating,
        latestOutTimeUs,
        totalMs);
    result.validation = validateTrimOutput(request, input.media, output.media, temporaryPath);
    if (!result.validation.success) {
        QFile::remove(temporaryPath);
        result.error = result.validation.error;
        result.elapsedMs = timer.elapsed();
        return result;
    }

    reportProgress(
        progressChanged,
        TrimStage::Committing,
        latestOutTimeUs,
        totalMs);
    if (!commitOutput(temporaryPath, outputInfo.absoluteFilePath(), request.overwrite, &result.error)) {
        QFile::remove(temporaryPath);
        result.elapsedMs = timer.elapsed();
        return result;
    }

    result.outputProbe.inputPath = outputInfo.absoluteFilePath();
    result.outputPath = outputInfo.absoluteFilePath();
    result.success = true;
    result.elapsedMs = timer.elapsed();
    reportProgress(
        progressChanged,
        TrimStage::Completed,
        latestOutTimeUs,
        totalMs);
    return result;
}

} // namespace billfish::trim
