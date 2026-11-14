#include "abrviz/core/render/segment_transcoder.h"

#include "abrviz/core/util/process_guard.h"

namespace abrviz {

TranscodeResult SegmentTranscoder::transcode(const TranscodeJob& job, std::stop_token stop)
{
    if (stop.stop_requested())
        return {false, true, {}};
    ProcessGuard process;
    if (!process.start(job.program, job.arguments))
        return {false, false, process.error()};
    switch (process.wait(stop)) {
    case ProcessGuard::Outcome::Finished:
        return {true, false, {}};
    case ProcessGuard::Outcome::Stopped:
        return {false, true, {}};
    case ProcessGuard::Outcome::Failed:
        break;
    }
    QString error = process.error();
    const QString output = QString::fromUtf8(process.output()).trimmed();
    if (!output.isEmpty())
        error += QStringLiteral(": ") + output.section(QLatin1Char('\n'), 0, 2);
    return {false, false, error};
}

} // namespace abrviz
