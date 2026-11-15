#include "abrviz/core/io/sim_log_parser.h"

#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/text_lines.h"

#include <algorithm>
#include <limits>

namespace abrviz {

namespace {

/** @brief Seconds per frame: the simulated player renders one every 33 ms. */
constexpr double kFrameDuration = 0.033;

/**
 * @brief Returns the bit rate after `key` as an integer.
 *
 * @param t The words of the line.
 * @param key The word before the value, for example `"Res:"`.
 * @return The bit rate, or nothing if it is missing, not a number, or does not fit in 64 bits.
 */
std::optional<qint64> bitRateAfter(const Tokens& t, QByteArrayView key)
{
    const auto value = t.doubleAfter(key);
    return value ? doubleToInt64(*value) : std::nullopt;
}

/**
 * @brief Returns the time of a `[MPEG_PLAYER]` line.
 *
 * Example: `t=56.8597s.` gives 56.8597.
 *
 * @param tokens The words of the line.
 * @return The time in seconds, or nothing if there is no `t=` word or it is not a number.
 */
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

/**
 * @brief Returns the player id of a `[MPEG_PLAYER]` line.
 *
 * Example: `ID=2` gives 2.
 *
 * @param tokens The words of the line.
 * @return The player id, or nothing if there is no `ID=` word.
 */
std::optional<int> playerIdAssignment(const Tokens& tokens)
{
    for (const QByteArrayView token : tokens.all()) {
        if (token.startsWith("ID="))
            return toInt(token.sliced(3));
    }
    return std::nullopt;
}

/**
 * @brief Adds a decision line of UE `nodeId` to `part`.
 *
 * Lines of other UEs, and lines without a time, segment, new or old bit rate, are ignored.
 *
 * @param t The words of the line.
 * @param nodeId The ns-3 node of the UE.
 * @param part Receives the decision.
 */
void parseDecision(const Tokens& t, int nodeId, SimLogPart& part)
{
    if (t.intAfter("ue-id:") != nodeId)
        return;
    const auto time = toDouble(t.at(0));
    const auto segment = t.intAfter("segmentId:");
    const auto newRate = bitRateAfter(t, "newBitRate:");
    const auto oldRate = bitRateAfter(t, "oldBitRate:");
    if (!time || !segment || !newRate || !oldRate)
        return;
    SimLogDecision d;
    d.time = *time;
    d.segment = *segment;
    d.newBitRate = *newRate;
    d.oldBitRate = *oldRate;
    d.throughputEstimate = t.doubleAfter("estBitRate:").value_or(0.0);
    d.interruptionTime = t.doubleAfter("interTime:").value_or(0.0);
    d.bufferLevel = t.doubleAfter("T:").value_or(0.0);
    d.bufferChange = t.doubleAfter("dT:").value_or(0.0);
    d.bufferFullWait = t.doubleAfter("del:").value_or(0.0);
    d.fetchTime = t.doubleAfter("fetch:").value_or(0.0);
    d.lossRate = t.doubleAfter("loss:").value_or(0.0);
    part.decisions.push_back(d);
}

/**
 * @brief Adds a `PLAYING FRAME` line of player `playerId` to `part`.
 *
 * Adds a buffer point (`queueLength` frames of 33 ms each) and starts or extends the playback
 * entry of the frame's segment. Lines of other players or with a missing field are ignored.
 *
 * @param t The words of the line.
 * @param playerId The player id of the UE.
 * @param part Receives the buffer point and playback.
 */
void parseFrame(const Tokens& t, int playerId, SimLogPart& part)
{
    if (t.intAfter("PlayerId:") != playerId)
        return;
    const auto time = toDouble(t.at(1));
    const auto segment = t.intAfter("SegId:");
    const auto frame = t.intAfter("FrameId:");
    const auto rate = bitRateAfter(t, "Res:");
    const auto queue = t.intAfter("queueLength:");
    // The frame count is FrameId + 1, which must not overflow.
    if (!time || !segment || !frame || !rate || !queue
        || *frame == std::numeric_limits<int>::max())
        return;

    part.buffer.push_back(BufferPoint{*time, *queue * kFrameDuration});

    if (part.playback.empty() || part.playback.back().segment != *segment) {
        part.playback.push_back(SegmentPlayback{*segment, *time, *time, *frame + 1, *rate});
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
