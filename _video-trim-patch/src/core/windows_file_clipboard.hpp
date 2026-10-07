#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

namespace billfish::trim {

enum class DropEffect : quint32 {
    Copy = 1,
    Move = 2,
    Link = 4,
};

struct ClipboardResult {
    bool success = false;
    QString error;
};

class WindowsFileClipboard final {
public:
    [[nodiscard]] static QByteArray buildHDropPayload(
        const QStringList &absoluteFilePaths,
        QString *error = nullptr);
    [[nodiscard]] static QByteArray buildPreferredDropEffectPayload(DropEffect effect);
    [[nodiscard]] static ClipboardResult setFiles(
        const QStringList &absoluteFilePaths,
        DropEffect effect = DropEffect::Copy,
        quintptr ownerWindow = 0);
};

} // namespace billfish::trim
