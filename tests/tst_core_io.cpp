#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/mapped_file.h"
#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/io/policy_log_parser.h"
#include "abrviz/core/io/sim_log_parser.h"
#include "abrviz/core/io/text_lines.h"
#include "abrviz/core/load/segment_shift.h"
#include "test_paths.h"

#include <QTemporaryDir>
#include <QTest>

#include <type_traits>

using namespace abrviz;

static_assert(!std::is_copy_constructible_v<MappedFile>);
static_assert(!std::is_copy_assignable_v<MappedFile>);
static_assert(std::is_nothrow_move_constructible_v<MappedFile>);
static_assert(std::is_nothrow_move_assignable_v<MappedFile>);

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    return path;
}

// Lines in the format of the real logs (node 7 is player 2).
const QByteArray kSimLog = QByteArrayLiteral(
    "Install UE with IMSI 4 on Node ID 7 at position (430.576, 451.524, 1.7)\n"
    "0.234408 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 3000000 oldBitRate: 250000 "
    "estBitRate: 4.66288e+06 interTime: 0 T: 3.03259 dT: 0 del: 0 fetch: 0.109596 loss: 0\n"
    "0.230608 ue-id: 6 videoId: 2 segmentId: 0 newBitRate: 6000000 oldBitRate: 250000 "
    "estBitRate: 4.8e+06 interTime: 0 T: 3.0 dT: 0 del: 0 fetch: 0.1 loss: 0\n"
    "0x1c4aa18 1.15571 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 0 Res: 250000 FrameId: 0 "
    "PlayTime: 0 Type: B Size: 579 interTime: 0 queueLength: 150\n"
    "0x1c49848 1.15531 PLAYING FRAME:  PlayerId: 1 VidId: 2 SegId: 0 Res: 250000 FrameId: 0 "
    "PlayTime: 0 Type: B Size: 579 interTime: 0 queueLength: 136\n"
    "0x1c4aa18 1.18871 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 0 Res: 250000 FrameId: 1 "
    "PlayTime: 0.033 Type: B Size: 579 interTime: 0 queueLength: 149\n"
    "Simulator Time: 1.25\n"
    "0x1c4aa18 4.45571 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 1 Res: 3000000 FrameId: 0 "
    "PlayTime: 3.3 Type: B Size: 579 interTime: 0 queueLength: 120\n"
    "[MPEG_PLAYER] No frames to play at t=56.8597s. Player ID=2 State changing to PAUSED. "
    "Interruptions=0\n"
    "[MPEG_PLAYER] Play resumed at t=57.2204s. Player ID=2 Pause duration=0.3607s. Total "
    "interruption time=0s\n"
    "[MPEG_PLAYER] No frames to play at t=60.0s. Player ID=1 State changing to PAUSED. "
    "Interruptions=0\n");

} // namespace

class TestCoreIo : public QObject {
    Q_OBJECT

private slots:
    void mappedFileReadsAndMoves()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("a.txt"), "hello\nworld\n");
        auto file = MappedFile::open(path);
        QVERIFY(file);
        QCOMPARE(file->view(), QByteArrayView("hello\nworld\n"));

        MappedFile moved(std::move(*file));
        QCOMPARE(moved.size(), 12);
        QVERIFY(!file->isMapped());
        QCOMPARE(file->size(), 0);

        const QString other = writeFile(dir, QStringLiteral("b.txt"), "second");
        auto second = MappedFile::open(other);
        QVERIFY(second);
        moved = std::move(*second);  // unmaps the first file
        QCOMPARE(moved.view(), QByteArrayView("second"));
    }

    void mappedFileEmptyAndMissing()
    {
        QTemporaryDir dir;
        auto empty = MappedFile::open(writeFile(dir, QStringLiteral("empty"), {}));
        QVERIFY(empty);
        QVERIFY(empty->view().isEmpty());

        QString error;
        QVERIFY(!MappedFile::open(dir.filePath(QStringLiteral("missing")), &error));
        QVERIFY(!error.isEmpty());
    }

    void linesAndChunks()
    {
        const QByteArray text = "a\r\nbb\n\nccc";
        QList<QByteArray> lines;
        QList<qsizetype> numbers;
        forEachLine(text, [&](QByteArrayView line, qsizetype n) {
            lines.append(line.toByteArray());
            numbers.append(n);
        });
        QCOMPARE(lines, (QList<QByteArray>{"a", "bb", "", "ccc"}));
        QCOMPARE(numbers, (QList<qsizetype>{1, 2, 3, 4}));

        for (qsizetype parts : {1, 2, 3, 7, 100}) {
            const auto chunks = splitAtLines(kSimLog, parts);
            QByteArray joined;
            for (const auto chunk : chunks) {
                QVERIFY(chunk.endsWith('\n'));
                joined += chunk.toByteArray();
            }
            QCOMPARE(joined, kSimLog);
        }
    }

    void tokens()
    {
        const Tokens t("0.5 ue-id: 7  T: 3.0 dT: 0.25");
        QCOMPARE(t.at(0), QByteArrayView("0.5"));
        QCOMPARE(t.intAfter("ue-id:"), std::optional<int>(7));
        QCOMPARE(t.doubleAfter("T:"), std::optional<double>(3.0));
        QCOMPARE(t.doubleAfter("dT:"), std::optional<double>(0.25));
        QVERIFY(!t.doubleAfter("missing:"));
    }

    void runJson()
    {
        const auto ok = parseRunJson(
            R"({"bundle_version": 1, "run_id": "r", "network": {"num_ues": 1,
                "simulated_num_ues": 3, "feeder_link_rate_bps": 9000000, "rng_run": 4},
                "player": {"segment_duration_s": 3.3}, "sim_stop_time_s": 30.5,
                "sim_log_decision_agreement": 0.5,
                "ues": [{"ue": 0, "node_id": 6, "video": "v", "stream_start_s": 0.1}]})",
            QStringLiteral("/b"));
        QVERIFY2(ok.ok(), qPrintable(ok.errors.join('\n')));
        QCOMPARE(ok.value.nodeId, 6);
        QCOMPARE(ok.value.simulatedUes, 3);
        QCOMPARE(ok.value.feederLinkRate, std::optional<qint64>(9000000));
        QCOMPARE(ok.value.logDecisionAgreement, 0.5);

        QVERIFY(!parseRunJson(R"({"bundle_version": 2})", {}).ok());
        QVERIFY(!parseRunJson("not json", {}).ok());
        QVERIFY(!parseRunJson(R"({"bundle_version": 1, "run_id": "r", "ues": []})", {}).ok());
    }

    void segmentRecords()
    {
        const auto parsed = parseSegmentRecords(
            "{\"segment\": 0, \"request_s\": 0.1, \"done_s\": 0.2, \"rung\": 3, "
            "\"bitrate_bps\": 3000000, \"bytes\": 10, \"decision\": {\"rtt_ms\": null, "
            "\"buffer_s\": 3.0, \"fallback\": true}}\n"
            "{\"segment\": 1, \"request_s\": 0.2, \"done_s\": 0.9, \"rung\": 1, "
            "\"bitrate_bps\": 750000, \"bytes\": 20}\n");
        QVERIFY2(parsed.ok(), qPrintable(parsed.errors.join('\n')));
        QCOMPARE(parsed.value.size(), 2u);
        QVERIFY(!parsed.value[0].rttMs);
        QCOMPARE(parsed.value[0].bufferLevel, std::optional<double>(3.0));
        QVERIFY(parsed.value[0].fallback);

        const auto broken = parseSegmentRecords(
            "{\"segment\": 0, \"request_s\": 0.1, \"done_s\": 0.2, \"rung\": 3, "
            "\"bitrate_bps\": 1, \"bytes\": 1}\n{oops\n");
        QCOMPARE(broken.errors.size(), 1);
        QVERIFY2(broken.errors.first().startsWith(QStringLiteral("segments.jsonl:2:")),
                 qPrintable(broken.errors.first()));
    }

    void playerEvents()
    {
        const auto parsed = parsePlayerEvents(
            R"({"t": 0.1, "event": "stream_start"}
{"t": 1.1, "event": "playback_start"}
{"t": 5.0, "event": "stall_start"}
{"t": 5.5, "event": "stall_end"}
{"t": 9.0, "event": "stall_start"}
{"t": 20.0, "event": "playback_end"})");
        QVERIFY2(parsed.ok(), qPrintable(parsed.errors.join('\n')));
        QCOMPARE(parsed.value.stalls.size(), 2u);
        QCOMPARE(parsed.value.stalls[0].duration(), 0.5);
        QVERIFY(!parsed.value.stalls[0].terminal);
        QVERIFY(parsed.value.stalls[1].terminal);
        QCOMPARE(parsed.value.stalls[1].end, 20.0);
    }

    void policyLog()
    {
        const auto decisions = parsePolicyLog(
            "# header\n"
            "    0.2284 ue-id: 6 seg(1-idx): 1 rung: 3 bitrate: 3000000 thr_est: 1 buf: 3.0 "
            "infer_ms: 257.9 reason: llm_policy_model+guard(buf3.0:r4->r3)\n"
            "    0.2290 ue-id: 7 seg(1-idx): 1 rung: 3 bitrate: 3000000 thr_est: 1 buf: 3.0 "
            "infer_ms: 29.7 reason: llm_policy_model\n"
            "    2.4219 ue-id: 6 seg(1-idx): 3 rung: 5 bitrate: 12000000 thr_est: 1 buf: 8.6 "
            "infer_ms: 30.0 reason: llm_policy_model\n",
            6);
        QCOMPARE(decisions.size(), 2u);
        QVERIFY(decisions[0].guardOverride);
        QCOMPARE(decisions[0].llmRung, std::optional<int>(4));
        QCOMPARE(decisions[0].rung, 3);
        QVERIFY(!decisions[1].guardOverride);
        QVERIFY(!decisions[1].llmRung);
    }

    void simLogOneUe()
    {
        const SimLogPart part = SimLogParser(7).parseChunk(kSimLog);
        QCOMPARE(part.decisions.size(), 1u);
        QCOMPARE(part.decisions[0].segment, 0);
        QCOMPARE(part.decisions[0].oldBitRate, 250000);
        QCOMPARE(part.decisions[0].newBitRate, 3000000);
        QCOMPARE(part.decisions[0].bufferLevel, 3.03259);

        QCOMPARE(part.buffer.size(), 3u);
        QCOMPARE(part.buffer[0].level, 150 * 0.033);

        QCOMPARE(part.playback.size(), 2u);
        QCOMPARE(part.playback[0].segment, 0);
        QCOMPARE(part.playback[0].framesPlayed, 2);
        QCOMPARE(part.playback[0].lastFrameTime, 1.18871);
        QCOMPARE(part.playback[1].bitrate, 3000000);

        QCOMPARE(part.pauses.size(), 1u);
        QCOMPARE(part.resumes.size(), 1u);
        QCOMPARE(part.resumes[0].pauseDuration, 0.3607);
    }

    void simLogChunksMergeToTheSameResult()
    {
        const SimLogParser parser(7);
        const SimLogPart whole = parser.parseChunk(kSimLog);
        // One chunk per line: segments are split across chunks and must be joined.
        SimLogPart merged;
        for (const auto chunk : splitAtLines(kSimLog, 1000))
            SimLogParser::merge(merged, parser.parseChunk(chunk));
        QCOMPARE(merged.decisions.size(), whole.decisions.size());
        QCOMPARE(merged.buffer.size(), whole.buffer.size());
        QCOMPARE(merged.playback.size(), whole.playback.size());
        QCOMPARE(merged.playback[0].framesPlayed, whole.playback[0].framesPlayed);
        QCOMPARE(merged.playback[0].lastFrameTime, whole.playback[0].lastFrameTime);
        QCOMPARE(merged.linesRead, whole.linesRead);
    }

    void stallsFromPauses()
    {
        SimLogPart log;
        log.playback.push_back({0, 1.0, 4.0, 100, 250000});
        log.pauses.push_back({2.0});
        log.resumes.push_back({2.5, 0.5});
        log.pauses.push_back({4.05});  // after the last frame

        const auto finished = SimLogParser::stalls(log, true, 10.0);
        QCOMPARE(finished.size(), 1u);
        QCOMPARE(finished[0].end, 2.5);

        // The same pause before the end of the video is a stall that never ended.
        const auto unfinished = SimLogParser::stalls(log, false, 10.0);
        QCOMPARE(unfinished.size(), 2u);
        QVERIFY(unfinished[1].terminal);
        QCOMPARE(unfinished[1].end, 10.0);
    }

    void mp4Duration()
    {
        const auto media = [](const char* name) {
            return test::dataPath(QStringLiteral("media/") + QLatin1StringView(name));
        };
        QCOMPARE(readMp4Duration(media("one_second_faststart.mp4")), std::optional<double>(1.0));
        QCOMPARE(readMp4Duration(media("two_and_a_half_seconds.mp4")),
                 std::optional<double>(2.5));
        QCOMPARE(readMp4Duration(media("version1_largesize.mp4")), std::optional<double>(3.0));
        QVERIFY(!readMp4Duration(media("not_an_mp4.mp4")));
        QVERIFY(!readMp4Duration(media("missing.mp4")));
    }

    void segmentShift()
    {
        VideoInfo video;
        video.segmentCount = 3;
        video.directory = QStringLiteral("/v");
        video.ladder = {{0, 250000, 0, 0}, {1, 750000, 0, 0}, {2, 1500000, 0, 0}};
        VideoSegmentTable sizes(3, 3);
        for (int s = 0; s < 3; ++s)
            for (int r = 0; r < 3; ++r)
                sizes.set(s, r, 100 * s + r);

        std::vector<SegmentRecord> records(3);
        const int rungs[] = {2, 1, 2};  // rung chosen for the *next* segment
        for (int n = 0; n < 3; ++n) {
            records[static_cast<std::size_t>(n)].segment = n;
            records[static_cast<std::size_t>(n)].rung = rungs[n];
            records[static_cast<std::size_t>(n)].doneTime = n + 0.5;
        }

        const ShiftResult result = applySegmentShift(records, video, sizes);
        QCOMPARE(result.segments.size(), 3u);
        QCOMPARE(result.segments[0].playedRung, 0);
        QCOMPARE(result.segments[1].playedRung, 2);
        QCOMPARE(result.segments[2].playedRung, 1);
        QCOMPARE(result.segments[2].bytes, 201);
        QCOMPARE(result.segments[1].mediaPath, QStringLiteral("/v/r2/seg_0001.mp4"));

        QCOMPARE(result.decisions.size(), 3u);
        QCOMPARE(result.decisions[0].forSegment, 1);
        QVERIFY(result.decisions[1].used);
        QVERIFY(!result.decisions[2].used);  // decision for segment 3, past the end
    }
};

QTEST_GUILESS_MAIN(TestCoreIo)
#include "tst_core_io.moc"
