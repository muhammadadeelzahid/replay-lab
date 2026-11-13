#include "abrviz/core/io/text_lines.h"

#include <QList>

#include <algorithm>

namespace abrviz {

QList<QByteArrayView> splitAtLines(QByteArrayView text, qsizetype parts)
{
    QList<QByteArrayView> chunks;
    if (text.isEmpty())
        return chunks;
    parts = std::max<qsizetype>(parts, 1);
    const qsizetype target = std::max<qsizetype>(text.size() / parts, 1);

    qsizetype start = 0;
    while (start < text.size()) {
        qsizetype end = std::min(start + target, text.size());
        if (end < text.size()) {
            const qsizetype newline = text.indexOf('\n', end);
            end = newline < 0 ? text.size() : newline + 1;
        }
        chunks.append(text.sliced(start, end - start));
        start = end;
    }
    return chunks;
}

} // namespace abrviz
