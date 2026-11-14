/**
 * @file
 * @brief print_report: prints the QoE report of run bundles.
 *
 * Usage:
 *
 *     print_report <bundle-or-folder>...
 *
 * The exit code is 1 if a bundle cannot be loaded or its numbers disagree with summary.json.
 */

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/playback/playback_clock.h"
#include "bundle_paths.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

/**
 * @brief Formats a bit rate in megabits per second.
 *
 * @param bitsPerSecond Bit rate (bit/s).
 * @return E.g. "4.20 Mbps", with two decimals.
 */
QString mbps(double bitsPerSecond)
{
    return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1e6, 0, 'f', 2);
}

/**
 * @brief Formats a duration in seconds.
 *
 * @param value Duration (seconds).
 * @return E.g. "1.250 s", with three decimals.
 */
QString seconds(double value)
{
    return QStringLiteral("%1 s").arg(value, 0, 'f', 3);
}

/**
 * @brief Prints the QoE report of one run.
 *
 * Lists segments, bitrates and switches, startup delay, stalls, throughput, guard overrides,
 * decision delay and replay duration, then one MISMATCH line for every number that disagrees
 * with summary.json.
 *
 * @param out Stream to print to.
 * @param run The loaded run.
 */
void printReport(QTextStream& out, const abrviz::RunData& run)
{
    const abrviz::QoeReport r = abrviz::computeQoe(run);
    const abrviz::PlaybackClock clock(run);

    out << run.info.runId << '\n';
    out << "  video            " << run.video.title << ", " << r.segmentsTotal << " segments\n";
    out << "  segments shown   " << r.segmentsShown;
    if (r.cutOff)
        out << " (cut off in segment " << r.lastSegmentShown << " after " << r.lastSegmentFrames
            << " of 100 frames)";
    out << '\n';
    out << "  bitrate played   " << mbps(r.averagePlayedBitrate) << ", " << r.playedSwitches
        << " switches\n";
    out << "  bitrate chosen   " << mbps(r.averageChosenBitrate) << ", " << r.chosenSwitches
        << " switches\n";
    out << "  startup delay    " << seconds(r.startupDelay) << '\n';
    out << "  stalls           " << r.stallCount << ", " << seconds(r.rebufferTime) << '\n';
    out << "  throughput       " << mbps(r.averageThroughput) << '\n';
    out << "  guard overrides  " << r.guardOverrides << " of " << r.decisions << " decisions\n";
    out << "  decision delay   " << seconds(r.averageDecisionDelay) << " average, "
        << seconds(r.maxDecisionDelay) << " max\n";
    out << "  replay           " << seconds(clock.replayDuration()) << " (simulated "
        << seconds(clock.simEnd() - clock.simStart()) << ")\n";
    for (const abrviz::SummaryCheck& check : r.checks) {
        if (!check.agrees())
            out << "  MISMATCH " << check.name << ": computed " << check.computed
                << ", summary.json " << *check.exported << '\n';
    }
}

} // namespace

/**
 * @brief Prints the report of every bundle named on the command line.
 *
 * @param argc Argument count.
 * @param argv Bundle folders, or folders searched one level down for bundles.
 * @return 0 if every bundle loaded and agrees with its summary.json, 1 otherwise, 2 without
 *         arguments.
 */
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList arguments = QCoreApplication::arguments().mid(1);
    if (arguments.isEmpty()) {
        err << "usage: print_report <bundle-or-folder>...\n";
        return 2;
    }

    int failed = 0;
    for (const QString& directory : abrviz::tools::expandBundlePaths(arguments)) {
        const abrviz::LoadResult result = abrviz::loadBundleSync(directory);
        if (!result.run) {
            err << directory << ": cannot load: " << result.errors.join(QStringLiteral("; "))
                << '\n';
            ++failed;
            continue;
        }
        printReport(out, *result.run);
        if (!abrviz::computeQoe(*result.run).agreesWithSummary())
            ++failed;
    }
    return failed == 0 ? 0 : 1;
}
