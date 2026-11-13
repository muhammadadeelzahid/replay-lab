#pragma once

#include <QByteArrayView>
#include <QVarLengthArray>

#include <optional>

namespace abrviz {

[[nodiscard]] inline std::optional<double> toDouble(QByteArrayView text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

[[nodiscard]] inline std::optional<int> toInt(QByteArrayView text)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? std::optional<int>(value) : std::nullopt;
}

/// The space-separated words of a log line, as views into the line. Used for the
/// `Key: value Key: value` format of the ns-3 and policy logs.
class Tokens {
public:
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

    [[nodiscard]] QByteArrayView at(qsizetype index) const
    {
        return index < tokens_.size() ? tokens_[index] : QByteArrayView();
    }

    /// The word after the first word equal to `key` (for example `"SegId:"`).
    [[nodiscard]] QByteArrayView after(QByteArrayView key) const
    {
        for (qsizetype i = 0; i + 1 < tokens_.size(); ++i) {
            if (tokens_[i] == key)
                return tokens_[i + 1];
        }
        return {};
    }

    [[nodiscard]] std::optional<double> doubleAfter(QByteArrayView key) const
    {
        return toDouble(after(key));
    }
    [[nodiscard]] std::optional<int> intAfter(QByteArrayView key) const
    {
        return toInt(after(key));
    }

    [[nodiscard]] const QVarLengthArray<QByteArrayView, 48>& all() const { return tokens_; }

private:
    QVarLengthArray<QByteArrayView, 48> tokens_;
};

} // namespace abrviz
