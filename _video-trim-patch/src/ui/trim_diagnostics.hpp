#pragma once

#include <QString>
#include <QtGlobal>

namespace billfish::trim {

[[nodiscard]] bool trimDiagnosticsEnabled() noexcept;
[[nodiscard]] QString trimDiagnosticPath();
[[nodiscard]] constexpr qint64 trimDiagnosticMaximumBytes() noexcept {
    return 5LL * 1024LL * 1024LL;
}
void trimDiagnostic(const QString& message);

}  // namespace billfish::trim
