#pragma once

/**
 * @file
 * @brief Line iteration and line-aligned splitting of text buffers.
 */

#include <QByteArrayView>
#include <QList>

#include <concepts>

namespace abrviz {

/**
 * @brief Calls `fn(line, lineNumber)` for every line of `text`, without the line ending.
 *
 * Both `\n` and `\r\n` endings are removed. A final line without a newline is included.
 *
 * @param text The text to split.
 * @param fn Called once per line, in order.
 * @param firstLineNumber Number passed for the first line.
 */
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

/**
 * @brief Splits `text` into at most `parts` pieces that each end at a line boundary.
 *
 * Each piece is about `text.size() / parts` bytes, extended to the next newline.
 *
 * @param text The text to split.
 * @param parts Wanted number of pieces; values below 1 are treated as 1.
 * @return Views into `text` that together cover it; empty if `text` is empty.
 */
[[nodiscard]] QList<QByteArrayView> splitAtLines(QByteArrayView text, qsizetype parts);

} // namespace abrviz
