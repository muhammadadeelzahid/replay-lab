#pragma once

#include <QByteArrayView>
#include <QList>

#include <concepts>

namespace abrviz {

/// Calls `fn(line, lineNumber)` for every line of `text`, without the line ending.
/// `lineNumber` starts at `firstLineNumber`. A final line without a newline is included.
template <typename Fn>
    requires std::invocable<Fn&, QByteArrayView, qsizetype>
void forEachLine(QByteArrayView text, Fn&& fn, qsizetype firstLineNumber = 1)
{
    qsizetype lineNumber = firstLineNumber;
    qsizetype start = 0;
    while (start < text.size()) {
        qsizetype end = text.indexOf('\n', start);
        if (end < 0)
            end = text.size();
        QByteArrayView line = text.sliced(start, end - start);
        if (line.endsWith('\r'))
            line.chop(1);
        fn(line, lineNumber);
        ++lineNumber;
        start = end + 1;
    }
}

/// Splits `text` into at most `parts` pieces that each end at a line boundary.
[[nodiscard]] QList<QByteArrayView> splitAtLines(QByteArrayView text, qsizetype parts);

} // namespace abrviz
