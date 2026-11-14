#pragma once

/**
 * @file
 * @brief The result type of the bundle parsers: a value plus the errors found.
 */

#include <QStringList>

#include <utility>

namespace abrviz {

/**
 * @brief A parser's result together with the problems found.
 *
 * Parsers never throw; a file with errors still returns whatever could be read.
 *
 * @tparam T Type of the parsed value.
 */
template <typename T>
struct Parsed {
    T value{};  ///< What could be read; default-constructed if nothing could.
    QStringList errors;  ///< One message per problem, prefixed with file and line.

    /**
     * @brief Tells whether parsing found no problem.
     *
     * @return True if `errors` is empty.
     */
    [[nodiscard]] bool ok() const { return errors.isEmpty(); }

    /**
     * @brief Appends an error as `file:line: message`, or `file: message` without a line.
     *
     * @param file Name of the file the error is in.
     * @param line One-based line number, or 0 for an error about the whole file.
     * @param message Description of the problem.
     */
    void addError(const QString& file, qsizetype line, const QString& message)
    {
        errors.append(line > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(line).arg(message)
                               : QStringLiteral("%1: %2").arg(file, message));
    }
};

} // namespace abrviz
