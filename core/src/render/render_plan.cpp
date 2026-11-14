#include "abrviz/core/render/render_plan.h"

#include "abrviz/core/playback/playback_clock.h"

#include <QCryptographicHash>

namespace abrviz {

namespace {

/// Bumped whenever the encoding settings change, so old files in the cache are not reused.
constexpr int kRenderVersion = 1;

/**
 * @brief Formats a number with six decimals, for `ffmpeg` arguments and the cache key.
 *
 * @param value The number.
 * @return The formatted number.
 */
QString number(double value)
{
    return QString::number(value, 'f', 6);
}

} // namespace

RenderPlan makeRenderPlan(const RunData& run)
{
    RenderPlan plan;
    if (run.video.frameRate > 0.0)
        plan.frameRate = run.video.frameRate;
    const PlaybackClock clock(run);
    for (const PlaybackClock::Piece& piece : clock.pieces()) {
        if (piece.kind == PlaybackClock::Piece::Kind::Stall) {
            if (!plan.parts.empty())
                plan.parts.back().freezeAfter += piece.replayEnd - piece.replayStart;
            continue;
        }
        const auto it = std::ranges::find(run.segments, piece.segment, &Segment::index);
        if (it == run.segments.end())
            continue;
        plan.parts.push_back(
            {piece.segment, it->mediaPath, piece.mediaEnd - piece.mediaStart, 0.0});
    }
    return plan;
}

double RenderPlan::duration() const
{
    double total = 0.0;
    for (const RenderPart& part : parts)
        total += part.duration + part.freezeAfter;
    return total;
}

QString RenderPlan::cacheKey(const QString& runId) const
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(kRenderVersion));
    hash.addData(QStringLiteral("%1x%2@%3").arg(width).arg(height).arg(number(frameRate)).toUtf8());
    for (const RenderPart& part : parts) {
        hash.addData(QStringLiteral("|%1|%2|%3|%4")
                         .arg(part.segment)
                         .arg(part.mediaPath, number(part.duration), number(part.freezeAfter))
                         .toUtf8());
    }
    return QStringLiteral("%1-%2").arg(runId, QString::fromLatin1(hash.result().toHex().left(12)));
}

QStringList transcodeArguments(const RenderPlan& plan, const RenderPart& part,
                               const QString& output, int threads)
{
    const QString size = QStringLiteral("%1:%2").arg(plan.width).arg(plan.height);
    QStringList filters = {
        QStringLiteral("trim=duration=%1").arg(number(part.duration)),
        QStringLiteral("setpts=PTS-STARTPTS"),
        QStringLiteral("fps=%1").arg(number(plan.frameRate)),
        // Every rung is shown at the same size, so low rungs look as soft as they should.
        QStringLiteral("scale=%1:force_original_aspect_ratio=decrease:flags=bicubic").arg(size),
        QStringLiteral("pad=%1:(ow-iw)/2:(oh-ih)/2").arg(size),
        QStringLiteral("setsar=1"),
    };
    if (part.freezeAfter > 0.0)
        filters << QStringLiteral("tpad=stop_mode=clone:stop_duration=%1").arg(number(part.freezeAfter));
    filters << QStringLiteral("format=yuv420p");

    const int gop = std::max(1, static_cast<int>(plan.frameRate * 2.0 + 0.5));
    return {
        QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-threads"), QString::number(threads),
        QStringLiteral("-i"), part.mediaPath,
        QStringLiteral("-an"),
        QStringLiteral("-vf"), filters.join(QLatin1Char(',')),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("20"),
        QStringLiteral("-g"), QString::number(gop),
        QStringLiteral("-threads"), QString::number(threads),
        QStringLiteral("-video_track_timescale"), QStringLiteral("90000"),
        output,
    };
}

QStringList concatArguments(const QString& listFile, const QString& output)
{
    return {
        QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("concat"),
        QStringLiteral("-safe"), QStringLiteral("0"),
        QStringLiteral("-i"), listFile,
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        output,
    };
}

QByteArray concatList(const QStringList& files)
{
    QByteArray list;
    for (const QString& file : files) {
        QString escaped = file;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        list += "file '" + escaped.toUtf8() + "'\n";
    }
    return list;
}

} // namespace abrviz
