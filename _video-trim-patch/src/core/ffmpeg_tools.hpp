#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <functional>

namespace billfish::trim {

struct FfmpegCapabilities {
    bool ffmpegCli = false;
    bool ffprobeCli = false;
    bool libx264Encoder = false;
    bool libx264Crf = false;
    QString ffmpegVersion;
    QString ffprobeVersion;
    QString ffmpegError;
    QString ffprobeError;

    [[nodiscard]] bool canFastTrim() const noexcept;
    [[nodiscard]] bool canAccurateTrim() const noexcept;
    [[nodiscard]] bool canProbe() const noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
};

struct FfmpegTools {
    QString ffmpegPath;
    QString ffprobePath;
    QString discoveryError;

    [[nodiscard]] bool canTrim() const;
    [[nodiscard]] bool canAccurateTrim() const;
    [[nodiscard]] bool canProbe() const;
    [[nodiscard]] bool isComplete() const;
    [[nodiscard]] FfmpegCapabilities capabilities() const;
    [[nodiscard]] QString trimCapabilityError(bool requireAccurate) const;
    [[nodiscard]] QString probeCapabilityError() const;
};

struct ProcessSpec {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
    bool inheritSystemEnvironment = true;
    int startTimeoutMs = 10'000;
    qint64 timeoutMs = 0;
};

enum class ProcessTermination {
    NotStarted,
    Completed,
    StartFailed,
    Crashed,
    TimedOut,
    Cancelled,
};

struct ProcessResult {
    ProcessTermination termination = ProcessTermination::NotStarted;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    QString error;
    qint64 elapsedMs = 0;

    [[nodiscard]] bool succeeded() const noexcept;
};

struct MediaProbe {
    QString inputPath;
    QString formatName;
    QString videoCodec;
    QString audioCodec;
    qint64 durationMs = -1;
    qint64 startTimeMs = 0;
    QSize videoSize;
    double frameRate = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;

    [[nodiscard]] bool isValid() const noexcept;
};

struct ProbeResult {
    bool success = false;
    MediaProbe media;
    ProcessResult process;
    QString error;
};

using ProcessOutputCallback = std::function<void(
    const QByteArray &standardOutputChunk,
    const QByteArray &standardErrorChunk)>;

[[nodiscard]] FfmpegTools locateFfmpegTools(
    const QString &applicationDirectory = {},
    const QString &explicitFfmpeg = {},
    const QString &explicitFfprobe = {},
    const QStringList &additionalSearchDirectories = {});

[[nodiscard]] QStringList buildProbeArguments(const QString &inputPath);

[[nodiscard]] ProcessResult runProcess(
    const ProcessSpec &spec,
    const std::function<bool()> &cancelRequested = {},
    const ProcessOutputCallback &outputReceived = {});

[[nodiscard]] ProbeResult probeMedia(
    const FfmpegTools &tools,
    const QString &inputPath,
    qint64 timeoutMs = 30'000,
    const std::function<bool()> &cancelRequested = {});

} // namespace billfish::trim
