// print_report: prints the QoE report of run bundles.
//
//   print_report <bundle-or-folder>...
//
// The exit code is 1 if a bundle cannot be loaded or its numbers disagree with summary.json.

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/playback/playback_clock.h"
#include "bundle_paths.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

QString mbps(double bitsPerSecond)
{
    return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1e6, 0, 'f', 2);
}

QString seconds(double value)
{
    return QStringLiteral("%1 s").arg(value, 0, 'f', 3);
}

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
