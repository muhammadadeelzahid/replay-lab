#pragma once

#include <QStringList>

#include <utility>

namespace abrviz {

/// A parser's result together with the problems found. Parsers never throw; a file with
/// errors still returns whatever could be read.
template <typename T>
struct Parsed {
    T value{};
    QStringList errors;

    [[nodiscard]] bool ok() const { return errors.isEmpty(); }

    void addError(const QString& file, qsizetype line, const QString& message)
    {
        errors.append(line > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(line).arg(message)
                               : QStringLiteral("%1: %2").arg(file, message));
    }
};

} // namespace abrviz
