#pragma once

#include <QPainterPath>

namespace billfish::shell {

inline QPainterPath pluginGlyphPath()
{
    QPainterPath path;
    path.moveTo(5, 5); path.lineTo(10, 5); path.cubicTo(8, 1, 16, 1, 14, 5);
    path.lineTo(19, 5); path.lineTo(19, 10); path.cubicTo(23, 8, 23, 16, 19, 14);
    path.lineTo(19, 19); path.lineTo(14, 19); path.cubicTo(16, 15, 8, 15, 10, 19);
    path.lineTo(5, 19); path.closeSubpath();
    return path;
}

} // namespace billfish::shell
