#pragma once

/**
 * @file
 * @brief Number conversion and word splitting for the text logs of the simulation.
 */

#include <QByteArrayView>
#include <QVarLengthArray>

#include <optional>

namespace abrviz {

/**
 * @brief Converts text to a double.
 *
 * @param text The text to convert.
 * @return The value, or nothing if `text` is not a number.
 */
[[nodiscard]] inline std::optional<double> toDouble(QByteArrayView text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

/**
 * @brief Converts text to an int.
 *
 * @param text The text to convert.
 * @return The value, or nothing if `text` is not an integer.
 */
[[nodiscard]] inline std::optional<int> toInt(QByteArrayView text)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? std::optional<int>(value) : std::nullopt;
}

/**
 * @brief The space-separated words of a log line, as views into the line.
 *
 * Used for the `Key: value Key: value` format of the ns-3 and policy logs. The views point into
 * the line passed to the constructor, which must outlive the object.
 */
class Tokens {
public:
    /**
     * @brief Splits `line` at runs of spaces.
     *
     * @param line The log line; empty words are dropped.
     */
    explicit Tokens(QByteArrayView line)
    {
        qsizetype start = 0;
        while (start < line.size()) {
            while (start < line.size() && line[start] == ' ')
                ++start;
            qsizetype end = start;
            while (end < line.size() && line[end] != ' ')
                ++end;
            if (end > start)
                tokens_.append(line.sliced(start, end - start));
            start = end;
        }
    }

    /**
     * @brief Returns the word at a position.
     *
     * @param index Zero-based word index.
     * @return The word, or an empty view if `index` is past the last word.
     */
    [[nodiscard]] QByteArrayView at(qsizetype index) const
    {
        return index < tokens_.size() ? tokens_[index] : QByteArrayView();
    }

    /**
     * @brief Returns the word after the first word equal to `key`.
     *
     * @param key The word to look for, for example `"SegId:"`.
     * @return The following word, or an empty view if `key` is missing or is the last word.
     */
    [[nodiscard]] QByteArrayView after(QByteArrayView key) const
    {
        for (qsizetype i = 0; i + 1 < tokens_.size(); ++i) {
            if (tokens_[i] == key)
                return tokens_[i + 1];
        }
        return {};
    }

    /**
     * @brief Returns the word after `key` as a double.
     *
     * @param key The word to look for.
     * @return The value, or nothing if `key` is missing or the next word is not a number.
     * @see after()
     */
    [[nodiscard]] std::optional<double> doubleAfter(QByteArrayView key) const
    {
        return toDouble(after(key));
    }

    /**
     * @brief Returns the word after `key` as an int.
     *
     * @param key The word to look for.
     * @return The value, or nothing if `key` is missing or the next word is not an integer.
     * @see after()
     */
    [[nodiscard]] std::optional<int> intAfter(QByteArrayView key) const
    {
        return toInt(after(key));
    }

    /**
     * @brief Returns all words in line order.
     *
     * @return The words.
     */
    [[nodiscard]] const QVarLengthArray<QByteArrayView, 48>& all() const { return tokens_; }

private:
    /** @brief Views into the line, one per word, in line order. */
    QVarLengthArray<QByteArrayView, 48> tokens_;
};

} // namespace abrviz
