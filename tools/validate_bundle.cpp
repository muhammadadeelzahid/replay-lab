/**
 * @file
 * @brief validate_bundle: loads run bundles and reports errors and warnings.
 *
 * Usage:
 *
 *     validate_bundle <bundle-or-folder>...
 *
 * A folder that is not a bundle itself is searched for bundles one level down. The exit code
 * is 0 when every bundle loads without errors (warnings are allowed), 1 otherwise.
 */

#include "abrviz/core/load/bundle_reader.h"
#include "bundle_paths.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>

namespace {

/**
 * @brief Counts the used decisions in which the guard overrode the ABR choice.
 *
 * @param run The loaded run.
 * @return The number of decisions with both `used` and `guardOverride` set.
 */
int countGuardOverrides(const abrviz::RunData& run)
{
    return static_cast<int>(std::ranges::count_if(
        run.decisions, [](const abrviz::Decision& d) { return d.used && d.guardOverride; }));
}

} // namespace

/**
 * @brief Loads every bundle named on the command line and prints its status.
 *
 * For each bundle prints ok/errors/FAILED with segment, stall and guard-override counts, then
 * its warnings and errors, and finally how many bundles are valid.
 *
 * @param argc Argument count.
 * @param argv Bundle folders, or folders searched one level down for bundles.
 * @return 0 if every bundle loaded without errors, 1 if any failed or none were found, 2
 *         without arguments.
 */
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList arguments = QCoreApplication::arguments().mid(1);
    if (arguments.isEmpty()) {
        err << "usage: validate_bundle <bundle-or-folder>...\n";
        return 2;
    }

    const QStringList bundles = abrviz::tools::expandBundlePaths(arguments);
    if (bundles.isEmpty()) {
        err << "no bundles found\n";
        return 1;
    }

    int failed = 0;
    for (const QString& directory : bundles) {
        const abrviz::LoadResult result = abrviz::loadBundleSync(directory);
        const QString name = result.run ? result.run->info.runId : directory;
        if (!result.run) {
            out << name << ": FAILED\n";
        } else {
            const abrviz::RunData& run = *result.run;
            out << name << ": " << (result.errors.isEmpty() ? "ok" : "errors") << ", "
                << run.segments.size() << " segments, " << run.stalls.size() << " stall(s), "
                << countGuardOverrides(run) << " guard override(s)\n";
            for (const abrviz::Warning& warning : run.warnings)
                out << "  warning: " << warning.message << '\n';
        }
        for (const QString& error : result.errors)
            out << "  error: " << error << '\n';
        if (!result.errors.isEmpty() || !result.run)
            ++failed;
    }

    out << bundles.size() - failed << " of " << bundles.size() << " bundle(s) valid\n";
    return failed == 0 ? 0 : 1;
}
