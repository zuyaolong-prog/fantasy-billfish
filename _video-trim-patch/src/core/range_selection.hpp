#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace billfish::trim {

struct RangeSelection {
    qint64 startMs = 0;
    qint64 endMs = 0;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] RangeSelection normalized() const noexcept;
    [[nodiscard]] RangeSelection clamped(qint64 mediaDurationMs) const noexcept;
    [[nodiscard]] QString validationError(qint64 mediaDurationMs = -1) const;

    friend bool operator==(const RangeSelection &lhs, const RangeSelection &rhs) noexcept
    {
        return lhs.startMs == rhs.startMs && lhs.endMs == rhs.endMs;
    }

    friend bool operator!=(const RangeSelection &lhs, const RangeSelection &rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

[[nodiscard]] QString formatTimestampMs(qint64 milliseconds);

} // namespace billfish::trim
