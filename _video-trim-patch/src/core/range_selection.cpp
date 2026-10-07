#include "range_selection.hpp"

#include <QtCore/QtGlobal>

#include <algorithm>

namespace billfish::trim {

bool RangeSelection::isValid() const noexcept
{
    return startMs >= 0 && endMs > startMs;
}

qint64 RangeSelection::durationMs() const noexcept
{
    return isValid() ? endMs - startMs : 0;
}

RangeSelection RangeSelection::normalized() const noexcept
{
    return startMs <= endMs ? *this : RangeSelection{endMs, startMs};
}

RangeSelection RangeSelection::clamped(qint64 mediaDurationMs) const noexcept
{
    if (mediaDurationMs < 0) {
        return *this;
    }
    return {
        std::clamp(startMs, qint64{0}, mediaDurationMs),
        std::clamp(endMs, qint64{0}, mediaDurationMs),
    };
}

QString RangeSelection::validationError(qint64 mediaDurationMs) const
{
    if (startMs < 0) {
        return QStringLiteral("The trim start must not be negative.");
    }
    if (endMs <= startMs) {
        return QStringLiteral("The trim end must be later than the start.");
    }
    if (mediaDurationMs >= 0 && endMs > mediaDurationMs) {
        return QStringLiteral("The trim end (%1 ms) exceeds the media duration (%2 ms).")
            .arg(endMs)
            .arg(mediaDurationMs);
    }
    return {};
}

QString formatTimestampMs(qint64 milliseconds)
{
    const bool negative = milliseconds < 0;
    const quint64 magnitude = negative
        ? static_cast<quint64>(-(milliseconds + 1)) + 1U
        : static_cast<quint64>(milliseconds);
    const quint64 hours = magnitude / 3'600'000U;
    const quint64 minutes = (magnitude / 60'000U) % 60U;
    const quint64 seconds = (magnitude / 1'000U) % 60U;
    const quint64 millis = magnitude % 1'000U;

    return QStringLiteral("%1%2:%3:%4.%5")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

} // namespace billfish::trim
