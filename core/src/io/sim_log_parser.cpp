#include "abrviz/core/io/sim_log_parser.h"

#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/text_lines.h"

#include <algorithm>

namespace abrviz {

namespace {

constexpr double kFrameDuration = 0.033;  // the simulated player renders a frame every 33 ms

/// `t=56.8597s.` -> 56.8597
std::optional<double> timeAssignment(const Tokens& tokens)
{
    for (const QByteArrayView token : tokens.all()) {
        if (token.startsWith("t=")) {
            QByteArrayView value = token.sliced(2);
            while (!value.isEmpty() && (value.endsWith('.') || value.endsWith('s')))
                value.chop(1);
            return toDouble(value);
        }
    }
    return std::nullopt;
}

/// `ID=2` -> 2
std::optional<int> playerIdAssignment(const Tokens& tokens)
{
    for (const QByteArrayView token : tokens.all()) {
        if (token.startsWith("ID="))
            return toInt(token.sliced(3));
    }
    return std::nullopt;
}

void parseDecision(const Tokens& t, int nodeId, SimLogPart& part)
{
    if (t.intAfter("ue-id:") != nodeId)
        return;
    const auto time = toDouble(t.at(0));
    const auto segment = t.intAfter("segmentId:");
    const auto newRate = t.doubleAfter("newBitRate:");
    const auto oldRate = t.doubleAfter("oldBitRate:");
    if (!time || !segment || !newRate || !oldRate)
        return;
    SimLogDecision d;
    d.time = *time;
    d.segment = *segment;
    d.newBitRate = static_cast<qint64>(*newRate);
    d.oldBitRate = static_cast<qint64>(*oldRate);
    d.throughputEstimate = t.doubleAfter("estBitRate:").value_or(0.0);
    d.interruptionTime = t.doubleAfter("interTime:").value_or(0.0);
    d.bufferLevel = t.doubleAfter("T:").value_or(0.0);
    d.bufferChange = t.doubleAfter("dT:").value_or(0.0);
    d.bufferFullWait = t.doubleAfter("del:").value_or(0.0);
    d.fetchTime = t.doubleAfter("fetch:").value_or(0.0);
    d.lossRate = t.doubleAfter("loss:").value_or(0.0);
    part.decisions.push_back(d);
}

void parseFrame(const Tokens& t, int playerId, SimLogPart& part)
{
    if (t.intAfter("PlayerId:") != playerId)
        return;
    const auto time = toDouble(t.at(1));
    const auto segment = t.intAfter("SegId:");
    const auto frame = t.intAfter("FrameId:");
    const auto rate = t.doubleAfter("Res:");
    const auto queue = t.intAfter("queueLength:");
    if (!time || !segment || !frame || !rate || !queue)
        return;

    part.buffer.push_back(BufferPoint{*time, *queue * kFrameDuration});

    if (part.playback.empty() || part.playback.back().segment != *segment) {
        part.playback.push_back(
            SegmentPlayback{*segment, *time, *time, *frame + 1, static_cast<qint64>(*rate)});
        return;
    }
    SegmentPlayback& current = part.playback.back();
    current.lastFrameTime = std::max(current.lastFrameTime, *time);
    current.framesPlayed = std::max(current.framesPlayed, *frame + 1);
}

} // namespace

SimLogParser::SimLogParser(int nodeId)
    : nodeId_(nodeId)
    , playerId_(playerIdForNode(nodeId))
{
}

SimLogPart SimLogParser::parseChunk(QByteArrayView chunk) const
{
    SimLogPart part;
    forEachLine(chunk, [&](QByteArrayView line, qsizetype) {
        ++part.linesRead;
        if (line.isEmpty())
            return;
        const char first = line.front();
        if (first >= '0' && first <= '9') {
            if (line.startsWith("0x")) {
                if (line.contains("PLAYING FRAME:"))
                    parseFrame(Tokens(line), playerId_, part);
            } else if (line.contains(" ue-id: ") && line.contains(" segmentId: ")) {
                parseDecision(Tokens(line), nodeId_, part);
            }
            return;
        }
        if (line.startsWith("[MPEG_PLAYER]")) {
            const Tokens tokens(line);
            if (playerIdAssignment(tokens) != playerId_)
                return;
            const auto time = timeAssignment(tokens);
            if (!time)
                return;
            if (line.contains("No frames to play")) {
                part.pauses.push_back(PlayerPause{*time});
            } else if (line.contains("Play resumed")) {
                double duration = 0.0;
                for (const QByteArrayView token : tokens.all()) {
                    if (token.startsWith("duration=")) {
                        QByteArrayView value = token.sliced(9);
                        while (!value.isEmpty() && (value.endsWith('.') || value.endsWith('s')))
                            value.chop(1);
                        duration = toDouble(value).value_or(0.0);
                    }
                }
                part.resumes.push_back(PlayerResume{*time, duration});
            }
        }
    });
    return part;
}

void SimLogParser::merge(SimLogPart& accumulated, const SimLogPart& part)
{
    auto append = [](auto& to, const auto& from) { to.insert(to.end(), from.begin(), from.end()); };
    append(accumulated.decisions, part.decisions);
    append(accumulated.buffer, part.buffer);
    append(accumulated.pauses, part.pauses);
    append(accumulated.resumes, part.resumes);
    accumulated.linesRead += part.linesRead;

    auto first = part.playback.begin();
    if (first != part.playback.end() && !accumulated.playback.empty()
        && accumulated.playback.back().segment == first->segment) {
        SegmentPlayback& joined = accumulated.playback.back();
        joined.lastFrameTime = std::max(joined.lastFrameTime, first->lastFrameTime);
        joined.framesPlayed = std::max(joined.framesPlayed, first->framesPlayed);
        ++first;
    }
    accumulated.playback.insert(accumulated.playback.end(), first, part.playback.end());
}

std::vector<Stall> SimLogParser::stalls(const SimLogPart& log, bool videoFinished,
                                        double stopTime)
{
    const double lastFrame = log.playback.empty() ? 0.0 : log.playback.back().lastFrameTime;
    std::vector<Stall> result;
    for (const PlayerPause& pause : log.pauses) {
        const auto resume = std::ranges::find_if(
            log.resumes, [&](const PlayerResume& r) { return r.time >= pause.time; });
        if (resume != log.resumes.end()) {
            result.push_back(Stall{pause.time, resume->time, false});
            continue;
        }
        if (videoFinished && pause.time >= lastFrame)
            continue;  // the player ran out of frames because the video ended
        result.push_back(Stall{pause.time, std::max(stopTime, pause.time), true});
    }
    return result;
}

} // namespace abrviz
