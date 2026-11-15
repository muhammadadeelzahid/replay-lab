// Edge cases of the core parsers and bundle I/O: malformed, truncated and hostile input must
// give errors or empty results, never a crash, a hang or a read out of bounds.

#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/mapped_file.h"
#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/io/policy_log_parser.h"
#include "abrviz/core/io/sim_log_parser.h"
#include "abrviz/core/io/text_lines.h"
#include "abrviz/core/io/topology_reader.h"
#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_loader.h"
#include "abrviz/core/load/bundle_reader.h"
#include "test_paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtEndian>

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace abrviz;

namespace {

// --- files ------------------------------------------------------------------------------------

bool writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/// Copies the directory tree `from` to `to`, creating `to`.
bool copyTree(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(info.filePath()));
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return false;
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!QFile::copy(info.filePath(), target))
                return false;
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        }
    }
    return true;
}

/// Copies a test bundle into `dir` under `name` so a test can break it.
QString copyBundle(const QTemporaryDir& dir, const QString& bundle, const QString& name = {})
{
    const QString target = dir.filePath(name.isEmpty() ? bundle : name);
    return copyTree(test::dataPath(bundle), target) ? target : QString();
}

bool isRoot()
{
    return ::geteuid() == 0;
}

// --- printing results so two results can be compared in full -----------------------------------

QString num(double value)
{
    return QString::number(value, 'g', 17);
}

template <typename T>
QString opt(const std::optional<T>& value)
{
    if (!value)
        return QStringLiteral("-");
    if constexpr (std::is_floating_point_v<T>)
        return num(*value);
    else
        return QString::number(*value);
}

QString dumpPart(const SimLogPart& p)
{
    QStringList out;
    for (const SimLogDecision& d : p.decisions)
        out << QStringLiteral("D %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11")
                   .arg(num(d.time))
                   .arg(d.segment)
                   .arg(d.newBitRate)
                   .arg(d.oldBitRate)
                   .arg(num(d.throughputEstimate), num(d.interruptionTime), num(d.bufferLevel),
                        num(d.bufferChange), num(d.bufferFullWait), num(d.fetchTime),
                        num(d.lossRate));
    for (const BufferPoint& b : p.buffer)
        out << QStringLiteral("B %1 %2").arg(num(b.time), num(b.level));
    for (const SegmentPlayback& s : p.playback)
        out << QStringLiteral("P %1 %2 %3 %4 %5")
                   .arg(s.segment)
                   .arg(num(s.firstFrameTime), num(s.lastFrameTime))
                   .arg(s.framesPlayed)
                   .arg(s.bitrate);
    for (const PlayerPause& x : p.pauses)
        out << QStringLiteral("Z %1").arg(num(x.time));
    for (const PlayerResume& x : p.resumes)
        out << QStringLiteral("R %1 %2").arg(num(x.time), num(x.pauseDuration));
    out << QStringLiteral("L %1").arg(p.linesRead);
    return out.join('\n');
}

QString dumpRun(const RunData& run)
{
    QStringList out;
    const RunInfo& i = run.info;
    out << QStringList{i.runId,         i.bundleDirectory, i.scenario,   i.policyName,
                       i.policyClient,  i.policyModel,     i.notes,      i.simLogSource,
                       i.videoName,     i.congestionControl, i.rlcMode}
               .join('|');
    out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
               .arg(opt(i.feederLinkRate))
               .arg(i.numUes)
               .arg(i.simulatedUes)
               .arg(i.numRelays)
               .arg(i.rngRun)
               .arg(i.nodeId)
               .arg(num(i.playerSegmentDuration), num(i.simStopTime), num(i.streamStart),
                    num(i.logDecisionAgreement));
    const NetworkTopology& t = run.topology;
    out << QStringLiteral("T %1 %2 %3 %4 %5 %6 %7 %8 %9")
               .arg(opt(t.satelliteNode), opt(t.satelliteAltitude))
               .arg(t.relayCount)
               .arg(opt(t.backhaulDistance), opt(t.carrierFrequencyGHz), opt(t.bandwidthMHz),
                    opt(t.servingNode), opt(t.accessDistance))
               .arg(t.viaRelay);
    const VideoInfo& v = run.video;
    out << QStringLiteral("V %1 %2 %3 %4 %5 %6 %7 %8")
               .arg(v.name, v.title, v.directory, v.segmentFilePattern,
                    num(v.nominalSegmentDuration), num(v.frameRate))
               .arg(v.segmentCount)
               .arg(v.ladder.size());
    for (const Rung& r : v.ladder)
        out << QStringLiteral("r %1 %2 %3 %4").arg(r.index).arg(r.bitrate).arg(r.width).arg(r.height);
    out << QStringLiteral("S %1 %2 %3").arg(num(run.streamStart), num(run.playbackStart),
                                            num(run.playbackEnd));
    for (const Segment& s : run.segments)
        out << QStringLiteral("seg %1 %2 %3 %4 %5 %6 %7 %8")
                   .arg(s.index)
                   .arg(num(s.requestTime), num(s.doneTime))
                   .arg(s.playedRung)
                   .arg(s.bytes)
                   .arg(s.mediaPath, num(s.mediaDuration))
                   .arg(s.played);
    for (const Decision& d : run.decisions)
        out << QStringLiteral("dec %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13")
                   .arg(d.record)
                   .arg(d.forSegment)
                   .arg(num(d.time))
                   .arg(d.chosenRung)
                   .arg(opt(d.bufferLevel), opt(d.throughputEstimate), opt(d.rttMs),
                        opt(d.lossRate), opt(d.decisionDelay))
                   .arg(d.guardOverride)
                   .arg(opt(d.llmRung), opt(d.bufferFullWait))
                   .arg(d.used);
    for (const Sample& s : run.samples)
        out << QStringLiteral("smp %1 %2 %3 %4 %5")
                   .arg(num(s.time), opt(s.throughput), opt(s.rttMs), opt(s.lossRate),
                        opt(s.bufferLevel));
    for (const BufferPoint& b : run.buffer)
        out << QStringLiteral("buf %1 %2").arg(num(b.time), num(b.level));
    for (const SegmentPlayback& p : run.playback)
        out << QStringLiteral("play %1 %2 %3 %4 %5")
                   .arg(p.segment)
                   .arg(num(p.firstFrameTime), num(p.lastFrameTime))
                   .arg(p.framesPlayed)
                   .arg(p.bitrate);
    for (const Stall& s : run.stalls)
        out << QStringLiteral("stall %1 %2 %3").arg(num(s.start), num(s.end)).arg(s.terminal);
    const ExporterSummary& m = run.summary;
    out << QStringLiteral("sum %1 %2 %3 %4 %5 %6 %7")
               .arg(opt(m.averageBitrate), opt(m.averageThroughput), opt(m.switchCount),
                    m.completed ? QString::number(*m.completed) : QStringLiteral("-"),
                    opt(m.rebufferTime), opt(m.stallCount), opt(m.startupDelay));
    for (const Warning& w : run.warnings)
        out << QStringLiteral("warn %1 %2").arg(static_cast<int>(w.kind)).arg(w.message);
    return out.join('\n');
}

QString dumpResult(const LoadResult& result)
{
    return result.errors.join('\n') + QStringLiteral("\n--\n")
         + (result.run ? dumpRun(*result.run) : QStringLiteral("<no run>"));
}

LoadResult loadAsync(const QString& directory, int threads)
{
    QThreadPool pool;
    pool.setMaxThreadCount(threads);
    QFuture<LoadResult> future = loadBundle(directory, &pool);
    future.waitForFinished();
    pool.waitForDone();
    return future.resultCount() > 0 ? future.result() : LoadResult{};
}

/// Compares the parallel loader's result with the synchronous one; empty if they agree.
/// When the run loads, both must be identical. When it does not, the parallel loader stops
/// after the first stage, so its errors are a subset of the synchronous loader's.
QString differences(const LoadResult& sync, const LoadResult& async)
{
    if (sync.run || async.run)
        return dumpResult(sync) == dumpResult(async)
                   ? QString()
                   : QStringLiteral("sync:\n%1\nasync:\n%2").arg(dumpResult(sync), dumpResult(async));
    for (const QString& error : async.errors) {
        if (!sync.errors.contains(error))
            return QStringLiteral("async error not in sync: ") + error;
    }
    if (async.errors.isEmpty() || async.errors.first() != sync.errors.first())
        return QStringLiteral("first errors differ:\n%1\n%2").arg(sync.errors.join('\n'), async.errors.join('\n'));
    return {};
}

// --- MP4 boxes -------------------------------------------------------------------------------

QByteArray be32(quint32 value)
{
    QByteArray bytes(4, '\0');
    qToBigEndian(value, bytes.data());
    return bytes;
}

QByteArray be64(quint64 value)
{
    QByteArray bytes(8, '\0');
    qToBigEndian(value, bytes.data());
    return bytes;
}

QByteArray box(const char* type, const QByteArray& payload)
{
    return be32(static_cast<quint32>(8 + payload.size())) + QByteArray(type, 4) + payload;
}

/// Version 0 `mvhd`/`mdhd` payload: version and flags, creation, modification, timescale,
/// duration.
QByteArray headerV0(quint32 timescale, quint32 duration)
{
    return be32(0) + be32(0) + be32(0) + be32(timescale) + be32(duration);
}

QByteArray hdlrPayload(const char* handler)
{
    return be32(0) + be32(0) + QByteArray(handler, 4) + QByteArray(12, '\0') + QByteArray(1, '\0');
}

const QByteArray kFtyp = box("ftyp", QByteArray("isom") + be32(0x200) + QByteArray("isom"));

/// A minimal MP4: movie header 5 s, video track media header 2.5 s.
QByteArray syntheticMp4(const char* handler = "vide", const QByteArray& mdhd = headerV0(100, 250))
{
    const QByteArray mdia = box("mdia", box("hdlr", hdlrPayload(handler)) + box("mdhd", mdhd));
    return kFtyp + box("moov", box("mvhd", headerV0(1000, 5000)) + box("trak", mdia));
}

// --- logs ------------------------------------------------------------------------------------

// Node 7 is player 2. Mixes the real line kinds with the noise found in real logs.
const QByteArray kNoisyLog = QByteArrayLiteral(
    "[Python] Spawning inference server: /x/python --device cuda\n"
    "Loading weights:   0%|          | 0/338 [00:00<?, ?it/s]\r"
    "Loading weights: 100%|##########| 338/338\n"
    "0.234408 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 3000000 oldBitRate: 250000 "
    "estBitRate: 4.66288e+06 interTime: 0 T: 3.03259 dT: 0 del: 0.5 fetch: 0.109596 loss: 0\n"
    "[Python][LLM] node=7 seg=1 choice=6000000 delay=0.0000s reason=llm_policy_model\n"
    "0x1c4aa18 1.15571 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 0 Res: 250000 FrameId: 0 "
    "PlayTime: 0 Type: B Size: 579 interTime: 0 queueLength: 150\r\n"
    "0x1c49848 1.15531 PLAYING FRAME:  PlayerId: 1 VidId: 2 SegId: 0 Res: 250000 FrameId: 0 "
    "PlayTime: 0 Type: B Size: 579 interTime: 0 queueLength: 136\n"
    "\n"
    "   \n"
    "0x1c4aa18 1.18871 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 0 Res: 250000 FrameId: 1 "
    "PlayTime: 0.033 Type: B Size: 579 interTime: 0 queueLength: 149\n"
    "0x1c4aa18 1.22171 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 0 Res: 250000 FrameId: 2 "
    "PlayTime: 0.066 Type: B Size: 579 interTime: 0 queueLength: 148\n"
    "1.5 ue-id: 7 videoId: 3 segmentId: 1 newBitRate: 6000000 oldBitRate: 3000000 T: 4\n"
    "0x1c4aa18 4.45571 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 1 Res: 3000000 FrameId: 0 "
    "PlayTime: 3.3 Type: B Size: 579 interTime: 0 queueLength: 120\n"
    "0x1c4aa18 4.48871 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 1 Res: 3000000 FrameId: 1 "
    "PlayTime: 3.3 Type: B Size: 579 interTime: 0 queueLength: 119\n"
    "[MPEG_PLAYER] No frames to play at t=56.8597s. Player ID=2 State changing to PAUSED.\n"
    "garbage \x01\x02\xff\xfe line\n"
    "[MPEG_PLAYER] Play resumed at t=57.2204s. Player ID=2 Pause duration=0.3607s. Total\n"
    "0x1c4aa18 57.3 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 1 Res: 3000000 FrameId: 2 "
    "PlayTime: 3.3 Type: B Size: 579 interTime: 0 queueLength: 10\n"
    "0x1c4aa18 57.4 PLAYING FRAME:  PlayerId: 2 VidId: 3 SegId: 2 Res: 6000000 FrameId: 0 "
    "PlayTime: 3.3 Type: B Size: 579 interTime: 0 queueLength: 9");  // no final newline

/// Parses `log` cut at the given byte offsets (which must be line starts) and merges the parts.
SimLogPart parseInPieces(const SimLogParser& parser, QByteArrayView log,
                         const QList<qsizetype>& cuts)
{
    SimLogPart merged;
    qsizetype start = 0;
    for (qsizetype cut : cuts) {
        if (cut > start) {
            SimLogParser::merge(merged, parser.parseChunk(log.sliced(start, cut - start)));
            start = cut;
        }
    }
    if (start < log.size())
        SimLogParser::merge(merged, parser.parseChunk(log.sliced(start)));
    return merged;
}

} // namespace

// Loads `bundle` with the parallel loader on `threads` threads and compares with `sync`.
#define COMPARE_WITH_PARALLEL_LOAD(sync, bundle, threads)                                          \
    do {                                                                                           \
        const QString diff = differences((sync), loadAsync((bundle), (threads)));                  \
        QVERIFY2(diff.isEmpty(), qPrintable(diff));                                                \
    } while (false)

class TestCoreEdgeCases : public QObject {
    Q_OBJECT

private slots:
    // --- line splitting and words ------------------------------------------------------------

    void forEachLineEdgeCases()
    {
        const auto lines = [](QByteArrayView text) {
            QList<QByteArray> out;
            forEachLine(text, [&](QByteArrayView line, qsizetype) { out.append(line.toByteArray()); });
            return out;
        };
        QVERIFY(lines("").isEmpty());
        QVERIFY(lines(QByteArrayView()).isEmpty());
        QCOMPARE(lines("\n"), (QList<QByteArray>{""}));
        QCOMPARE(lines("\n\n"), (QList<QByteArray>{"", ""}));
        QCOMPARE(lines("a"), (QList<QByteArray>{"a"}));
        QCOMPARE(lines("a\nb"), (QList<QByteArray>{"a", "b"}));
        QCOMPARE(lines("a\r\nb\r\n"), (QList<QByteArray>{"a", "b"}));
        QCOMPARE(lines("\r\n"), (QList<QByteArray>{""}));
        // A lone CR is not a line break, and only one CR is removed before the LF.
        QCOMPARE(lines("a\rb\n"), (QList<QByteArray>{"a\rb"}));
        QCOMPARE(lines("a\r\r\n"), (QList<QByteArray>{"a\r"}));
        QCOMPARE(lines("a\r"), (QList<QByteArray>{"a"}));
        QCOMPARE(lines("\r"), (QList<QByteArray>{""}));
        QCOMPARE(lines(" \t \n"), (QList<QByteArray>{" \t "}));

        // Embedded NUL bytes and non-UTF-8 bytes are ordinary bytes.
        const QByteArray binary("a\0b\n\xff\xfe\n\0", 8);
        const auto split = lines(binary);
        QCOMPARE(split.size(), 3);
        QCOMPARE(split[0], QByteArray("a\0b", 3));
        QCOMPARE(split[1], QByteArray("\xff\xfe"));
        QCOMPARE(split[2], QByteArray("\0", 1));

        // Line numbers start where asked, and blank lines are counted.
        QList<qsizetype> numbers;
        forEachLine("x\n\ny", [&](QByteArrayView, qsizetype n) { numbers.append(n); }, 41);
        QCOMPARE(numbers, (QList<qsizetype>{41, 42, 43}));
    }

    void forEachLineVeryLongLine()
    {
        const QByteArray longLine(8 * 1024 * 1024, 'x');
        qsizetype count = 0;
        qsizetype size = 0;
        forEachLine(longLine + "\nshort", [&](QByteArrayView line, qsizetype) {
            ++count;
            if (count == 1)
                size = line.size();
        });
        QCOMPARE(count, 2);
        QCOMPARE(size, longLine.size());
    }

    void splitAtLinesCoversTheTextForAnyPartCount()
    {
        const QList<QByteArray> texts = {
            QByteArray("x"),
            QByteArray("\n"),
            QByteArray("\n\n\n\n\n\n\n"),
            QByteArray("no newline at all, one long line"),
            QByteArray("a\r\nbb\r\nccc\r\n"),
            QByteArray("a\nb\nc"),
            QByteArray("lone\rcarriage\rreturns\n"),
            QByteArray("nul\0inside\n\0\n", 14),
            QByteArray(5000, 'y') + "\n" + QByteArray(3, 'z') + "\n",
            kNoisyLog,
        };
        for (const QByteArray& text : texts) {
            for (qsizetype parts = -3; parts <= 40; ++parts) {
                const QList<QByteArrayView> chunks = splitAtLines(text, parts);
                const QString where = QStringLiteral("text of %1 bytes, %2 parts")
                                          .arg(text.size())
                                          .arg(parts);
                QVERIFY2(!chunks.isEmpty(), qPrintable(where));
                QVERIFY2(chunks.size() <= std::max<qsizetype>(parts, 1), qPrintable(where));
                // Contiguous views into the original buffer, covering it exactly.
                const char* expected = text.constData();
                for (qsizetype i = 0; i < chunks.size(); ++i) {
                    QVERIFY2(!chunks[i].isEmpty(), qPrintable(where));
                    QVERIFY2(chunks[i].data() == expected, qPrintable(where));
                    expected += chunks[i].size();
                    if (i + 1 < chunks.size())
                        QVERIFY2(chunks[i].endsWith('\n'), qPrintable(where));
                }
                QVERIFY2(expected == text.constData() + text.size(), qPrintable(where));
            }
        }
        QVERIFY(splitAtLines(QByteArrayView(), 4).isEmpty());
        QVERIFY(splitAtLines("", 0).isEmpty());
    }

    void tokensEdgeCases()
    {
        QVERIFY(Tokens("").all().isEmpty());
        QVERIFY(Tokens("      ").all().isEmpty());
        QCOMPARE(Tokens("").at(0), QByteArrayView());
        QCOMPARE(Tokens("a").at(1), QByteArrayView());

        // Only spaces separate words; tabs and CR stay inside them.
        const Tokens tabs("a\tb  c\r");
        QCOMPARE(tabs.all().size(), 2);
        QCOMPARE(tabs.at(0), QByteArrayView("a\tb"));
        QCOMPARE(tabs.at(1), QByteArrayView("c\r"));

        // A key as the last word has no value; the first of repeated keys wins.
        const Tokens t("k: 1 k: 2 last:");
        QCOMPARE(t.intAfter("k:"), std::optional<int>(1));
        QCOMPARE(t.after("last:"), QByteArrayView());
        QVERIFY(!t.intAfter("last:"));
        // A key never matches part of a word.
        QVERIFY(!Tokens("xk: 1").intAfter("k:"));

        // More words than the inline capacity.
        QByteArray many;
        for (int i = 0; i < 500; ++i)
            many += "w" + QByteArray::number(i) + "   ";
        const Tokens big(many);
        QCOMPARE(big.all().size(), 500);
        QCOMPARE(big.at(499), QByteArrayView("w499"));
        QCOMPARE(big.at(500), QByteArrayView());

        // NUL bytes are not separators.
        const QByteArray nul("a\0b c", 5);
        QCOMPARE(Tokens(nul).all().size(), 2);
        QCOMPARE(Tokens(nul).at(0).size(), 3);

        // Numbers: missing units or extra characters make a value unreadable.
        QVERIFY(!toDouble("12ms"));
        QVERIFY(!toDouble(""));
        QVERIFY(!toDouble("1,5"));
        QVERIFY(!toDouble("0x10"));
        QVERIFY(!toDouble("1e400"));  // out of range
        QVERIFY(!toInt("2147483648"));
        QVERIFY(!toInt("1.5"));
        QCOMPARE(toInt("-2147483648"), std::optional<int>(std::numeric_limits<int>::min()));
        // Qt reads "nan" and "inf"; the log parsers must cope with them (see simLogExtremeNumbers).
        QVERIFY(toDouble("nan") && std::isnan(*toDouble("nan")));
        QVERIFY(toDouble("inf") && std::isinf(*toDouble("inf")));
    }

    // --- run.json ----------------------------------------------------------------------------

    void runJsonWrongTypes()
    {
        const auto parse = [](const QByteArray& json) { return parseRunJson(json, QStringLiteral("/b")); };
        const QByteArray ue = R"("ues": [{"node_id": 6, "video": "v"}])";

        QVERIFY(!parse("").ok());
        QVERIFY(!parse("   \n").ok());
        QVERIFY(!parse("[]").ok());
        QVERIFY(!parse("null").ok());
        QVERIFY(!parse(R"({"bundle_version": 1, "run_id": "r", )" + ue + "} trailing").ok());
        QVERIFY(!parse(QByteArray(R"({"bundle_version": 1, "run_id": "r)") + '\0' + "\"}").ok());
        QVERIFY(!parse("{\"bundle_version\": 1, \"run_id\": \"\xff\xfe\", " + ue + "}").ok());

        // bundle_version must be the number 1.
        QVERIFY(!parse(R"({"bundle_version": "1", "run_id": "r", )" + ue + "}").ok());
        QVERIFY(!parse(R"({"bundle_version": null, "run_id": "r", )" + ue + "}").ok());
        QVERIFY(!parse(R"({"bundle_version": 1.5, "run_id": "r", )" + ue + "}").ok());
        QVERIFY(!parse(R"({"run_id": "r", )" + ue + "}").ok());
        QVERIFY(parse(R"({"bundle_version": 1.0, "run_id": "r", )" + ue + "}").ok());

        // A UTF-8 byte order mark is accepted.
        QVERIFY(parse("\xEF\xBB\xBF{\"bundle_version\": 1, \"run_id\": \"r\", " + ue + "}").ok());

        // Wrong types for the required fields are reported as missing.
        const auto wrong = parse(R"({"bundle_version": 1, "run_id": 5,
                                     "ues": [{"node_id": "6", "video": ["v"]}]})");
        QCOMPARE(wrong.errors.size(), 3);
        QVERIFY(!parse(R"({"bundle_version": 1, "run_id": "r", "ues": {"0": {}}})").ok());
        QVERIFY(!parse(R"({"bundle_version": 1, "run_id": "r", "ues": [null]})").ok());
        QVERIFY(!parse(R"({"bundle_version": 1, "run_id": "r", "ues": [{"node_id": -3, "video": "v"}]})").ok());

        // Optional sections of the wrong type fall back to their defaults.
        const auto odd = parse(R"({"bundle_version": 1, "run_id": "r", "policy": [1, 2],
                                   "network": "fast", "player": null, "sim_stop_time_s": "37",
                                   "unknown": {"deep": [1, {"x": null}]}, )" + ue + "}");
        QVERIFY2(odd.ok(), qPrintable(odd.errors.join('\n')));
        QCOMPARE(odd.value.numUes, 1);
        QCOMPARE(odd.value.simulatedUes, 1);
        QCOMPARE(odd.value.playerSegmentDuration, 3.3);
        QCOMPARE(odd.value.simStopTime, 0.0);
        QVERIFY(!odd.value.feederLinkRate);
        QCOMPARE(odd.value.bundleDirectory, QStringLiteral("/b"));

        // Duplicate keys: the last one wins (Qt's JSON parser).
        const auto dup = parse(R"({"bundle_version": 1, "run_id": "a", "run_id": "b", )" + ue + "}");
        QCOMPARE(dup.value.runId, QStringLiteral("b"));
    }

    void runJsonExtremeNumbers()
    {
        const auto rate = [](const QByteArray& value) {
            const QByteArray json = R"({"bundle_version": 1, "run_id": "r",
                "network": {"feeder_link_rate_bps": )" + value + R"(, "num_ues": 1e300,
                            "simulated_num_ues": -7, "rng_run": 3.5},
                "sim_log_decision_agreement": -2,
                "ues": [{"node_id": 2147483648, "video": "v", "stream_start_s": 1e308}]})";
            return parseRunJson(json, {});
        };
        // 1e300 does not fit in 64 bits; converting it must not be undefined behaviour.
        const auto huge = rate("1e300");
        QVERIFY(!huge.value.feederLinkRate);
        QVERIFY(!rate("-1e300").value.feederLinkRate);
        QVERIFY(!rate("9.3e18").value.feederLinkRate);
        QCOMPARE(rate("9e18").value.feederLinkRate, std::optional<qint64>(9000000000000000000));
        QCOMPARE(rate("18000000.9").value.feederLinkRate, std::optional<qint64>(18000000));
        QVERIFY(!rate("\"18Mbps\"").value.feederLinkRate);
        QVERIFY(!rate("null").value.feederLinkRate);
        // JSON has no NaN or Infinity, and numbers beyond double range are rejected.
        QVERIFY(!rate("NaN").ok());
        QVERIFY(!rate("Infinity").ok());
        QVERIFY(!rate("1e400").ok());

        // Integers out of int range take the defaults; node_id 2^31 is reported as missing.
        QCOMPARE(huge.value.numUes, 1);
        QCOMPARE(huge.value.simulatedUes, -7);
        QCOMPARE(huge.value.rngRun, 0);
        QCOMPARE(huge.value.nodeId, -1);
        QVERIFY(huge.errors.contains(QStringLiteral("run.json: missing ues[0].node_id")));
        QCOMPARE(huge.value.streamStart, 1e308);
        QCOMPARE(huge.value.logDecisionAgreement, -2.0);
    }

    // --- segments.jsonl ----------------------------------------------------------------------

    void segmentRecordsMalformedLines()
    {
        const QByteArray good0 =
            R"({"segment": 0, "request_s": 0, "done_s": 1, "rung": 1, "bitrate_bps": 5, "bytes": 9})";
        const QByteArray good1 =
            R"({"segment": 1, "request_s": 1, "done_s": 2, "rung": 2, "bitrate_bps": 6, "bytes": 8, "extra": [1, {"a": null}]})";
        const QByteArray good2 =
            R"({"segment": 2, "request_s": 2, "done_s": 3, "rung": 0, "bitrate_bps": 7, "bytes": 7})";
        const QByteArray text = "\xEF\xBB\xBF" + good0 + "\r\n"  // 1: BOM on the first line
                              + "\r\n"                          // 2: blank
                              + " \t \n"                        // 3: whitespace only
                              + "[1, 2, 3]\n"                   // 4: not an object
                              + QByteArray("{\"segment\": 9\0}\n", 16)  // 5: NUL byte
                              + "{\"segment\": \"\xff\"}\n"     // 6: invalid UTF-8
                              + good1 + "\n"                    // 7
                              + "{}\n"                          // 8: every field missing
                              + good2.left(good2.size() - 3);  // 9: truncated last line

        const auto parsed = parseSegmentRecords(text);
        QCOMPARE(parsed.value.size(), 2u);
        QCOMPARE(parsed.value[0].segment, 0);
        QCOMPARE(parsed.value[1].segment, 1);
        QCOMPARE(parsed.value[1].bytes, 8);

        QStringList lines;
        for (const QString& error : parsed.errors)
            lines.append(error.section(':', 1, 1));
        // Line 8 has one error per missing field.
        QCOMPARE(lines, (QStringList{"4", "5", "6", "8", "8", "8", "8", "8", "8", "9"}));
    }

    void segmentRecordsWrongTypesAndOrder()
    {
        const auto record = [](const QByteArray& segment, const QByteArray& rest) {
            return R"({"segment": )" + segment + R"(, "request_s": 1, "done_s": 2, "rung": 1, )"
                 + rest + "}\n";
        };
        const QByteArray sizes = R"("bitrate_bps": 5, "bytes": 9)";

        // Out of order and duplicate segments are kept and reported.
        const auto order = parseSegmentRecords(record("0", sizes) + record("2", sizes)
                                               + record("2", sizes) + record("1", sizes));
        QCOMPARE(order.value.size(), 4u);
        QCOMPARE(order.errors.size(), 3);
        QVERIFY2(order.errors[0].startsWith(QStringLiteral("segments.jsonl:2: segment 2 does not follow 0")),
                 qPrintable(order.errors[0]));

        // Present but of the wrong type counts as present; the value becomes 0.
        const auto nulls = parseSegmentRecords(
            R"({"segment": null, "request_s": "1", "done_s": null, "rung": true, "bitrate_bps": [], "bytes": {}})");
        QCOMPARE(nulls.value.size(), 1u);
        QCOMPARE(nulls.value[0].segment, 0);
        QCOMPARE(nulls.value[0].rung, 0);
        QCOMPARE(nulls.value[0].bitrate, 0);
        QCOMPARE(nulls.value[0].bytes, 0);
        QVERIFY(nulls.ok());

        // Sizes that do not fit in 64 bits must not be undefined behaviour; they read as 0.
        const auto huge = parseSegmentRecords(
            record("0", R"("bitrate_bps": 1e300, "bytes": -1e300)")
            + record("1", R"("bitrate_bps": 9.3e18, "bytes": -9.3e18)")
            + record("2", R"("bitrate_bps": 1.5e18, "bytes": 123.9)")
            + record("3", R"("bitrate_bps": -5, "bytes": 1.7976931348623157e308)"));
        QVERIFY2(huge.ok(), qPrintable(huge.errors.join('\n')));
        QCOMPARE(huge.value.size(), 4u);
        QCOMPARE(huge.value[0].bitrate, 0);
        QCOMPARE(huge.value[0].bytes, 0);
        QCOMPARE(huge.value[1].bitrate, 0);
        QCOMPARE(huge.value[1].bytes, 0);
        QCOMPARE(huge.value[2].bitrate, 1500000000000000000);
        QCOMPARE(huge.value[2].bytes, 123);
        QCOMPARE(huge.value[3].bitrate, -5);
        QCOMPARE(huge.value[3].bytes, 0);

        // Segment numbers beyond int range read as 0.
        const auto bigSegment = parseSegmentRecords(record("1e10", sizes));
        QCOMPARE(bigSegment.value[0].segment, 0);

        // Times running backwards, negative and huge values are kept.
        const auto times = parseSegmentRecords(
            R"({"segment": 0, "request_s": 5, "done_s": -1e308, "rung": -4, "bitrate_bps": 1, "bytes": 1, )"
            R"("decision": {"buffer_s": "3", "rtt_ms": null, "loss_rate": 1e308, "fallback": "yes"}})");
        QCOMPARE(times.errors.size(), 1);
        QVERIFY(times.errors[0].endsWith(QStringLiteral("done_s is before request_s")));
        QCOMPARE(times.value[0].rung, -4);
        QVERIFY(!times.value[0].bufferLevel);
        QVERIFY(!times.value[0].rttMs);
        QCOMPARE(times.value[0].lossRate, std::optional<double>(1e308));
        QVERIFY(!times.value[0].fallback);

        // A decision that is not an object leaves the optional fields empty.
        const auto noDecision = parseSegmentRecords(record("0", sizes + R"(, "decision": [1])"));
        QVERIFY(noDecision.ok());
        QVERIFY(!noDecision.value[0].bufferLevel);
    }

    void segmentRecordsEmptyInputs()
    {
        for (const QByteArray& text : {QByteArray(), QByteArray("\n"), QByteArray("\r\n\r\n"),
                                       QByteArray("   \n\t\n"), QByteArray("\xEF\xBB\xBF")}) {
            const auto parsed = parseSegmentRecords(text);
            QVERIFY(parsed.value.empty());
            QCOMPARE(parsed.errors.size(), 1);
        }
        // Only broken lines: their errors are reported, not "no segment records".
        const auto broken = parseSegmentRecords("{\n{\n");
        QCOMPARE(broken.errors.size(), 2);
        QVERIFY(broken.value.empty());
    }

    void segmentRecordsManyLinesAndLongLine()
    {
        QByteArray text;
        for (int i = 0; i < 20000; ++i)
            text += R"({"segment": )" + QByteArray::number(i)
                  + R"(, "request_s": 1, "done_s": 2, "rung": 1, "bitrate_bps": 5, "bytes": 9})" + "\n";
        // A very long line with a 4 MB string field.
        text += "{\"segment\": 20000, \"request_s\": 1, \"done_s\": 2, \"rung\": 1, "
                "\"bitrate_bps\": 5, \"bytes\": 9, \"pad\": \""
              + QByteArray(4 * 1024 * 1024, 'p') + "\"}";
        const auto parsed = parseSegmentRecords(text);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.value.size(), 20001u);
        QCOMPARE(parsed.value.back().segment, 20000);
    }

    // --- samples.jsonl, player_events.jsonl, summary.json ------------------------------------

    void samplesEdgeCases()
    {
        // An empty file is not an error.
        QVERIFY(parseSamples("").ok());
        QVERIFY(parseSamples("").value.empty());

        const auto parsed = parseSamples(
            R"({"t": 1, "throughput_bps": "fast", "rtt_ms": null, "loss_rate": -1, "buffer_s": 1e308})" "\n"
            R"({"throughput_bps": 5})" "\n"
            R"({"t": null})" "\n"
            R"({"t": 0.5})" "\n"
            R"({"t": 2, "t": 3})" "\n"
            "{\"t\": 4");
        QCOMPARE(parsed.value.size(), 4u);
        QVERIFY(!parsed.value[0].throughput);
        QVERIFY(!parsed.value[0].rttMs);
        QCOMPARE(parsed.value[0].lossRate, std::optional<double>(-1.0));
        QCOMPARE(parsed.value[0].bufferLevel, std::optional<double>(1e308));
        QCOMPARE(parsed.value[1].time, 0.0);  // t: null reads as 0
        QCOMPARE(parsed.value[3].time, 3.0);  // duplicate key: the last one wins
        QStringList lines;
        for (const QString& error : parsed.errors)
            lines.append(error.section(':', 1, 1));
        // 2: missing t; 3: goes back to 0; 6: truncated. Line 4 (0.5 after 0) is in order.
        QCOMPARE(lines, (QStringList{"2", "3", "6"}));
    }

    void playerEventsEdgeCases()
    {
        QVERIFY(!parsePlayerEvents("").ok());
        QVERIFY(parsePlayerEvents("").value.stalls.empty());

        const QByteArray base = R"({"t": 0, "event": "stream_start"})" "\n"
                                R"({"t": 1, "event": "playback_start"})" "\n";
        // Repeated milestones: the last one wins.
        const auto repeated = parsePlayerEvents(base + R"({"t": 9, "event": "playback_end"})" "\n"
                                                       R"({"t": 7, "event": "playback_end"})");
        QVERIFY(repeated.ok());
        QCOMPARE(repeated.value.playbackEnd, std::optional<double>(7.0));

        // Two starts in a row: the first stall is lost and reported; a stall_end that ends
        // before it starts is kept as it is.
        const auto twice = parsePlayerEvents(base + R"({"t": 2, "event": "stall_start"})" "\n"
                                                    R"({"t": 3, "event": "stall_start"})" "\n"
                                                    R"({"t": 2.5, "event": "stall_end"})" "\n"
                                                    R"({"t": 4, "event": "stall_end"})" "\n"
                                                    R"({"t": 9, "event": "playback_end"})");
        QCOMPARE(twice.value.stalls.size(), 1u);
        QCOMPARE(twice.value.stalls[0].start, 3.0);
        QCOMPARE(twice.value.stalls[0].end, 2.5);
        QCOMPARE(twice.errors.size(), 2);

        // Wrong types: event not a string is an unknown event; t not a number reads as 0.
        const auto types = parsePlayerEvents(base + R"({"t": "5", "event": "playback_end"})" "\n"
                                                    R"({"t": 6, "event": 12})" "\n"
                                                    R"({"t": 6, "event": null})" "\n"
                                                    R"({"t": 6, "event": "STALL_START"})" "\n"
                                                    R"({"event": "stall_start"})" "\n"
                                                    R"({"t": 7})");
        QCOMPARE(types.value.playbackEnd, std::optional<double>(0.0));
        QVERIFY(types.value.stalls.empty());
        QCOMPARE(types.errors.size(), 5);

        // An unended stall without playback_end ends at its own start and is terminal.
        const auto unended = parsePlayerEvents(base + R"({"t": 3, "event": "stall_start"})");
        QCOMPARE(unended.value.stalls.size(), 1u);
        QCOMPARE(unended.value.stalls[0].end, 3.0);
        QVERIFY(unended.value.stalls[0].terminal);
        QVERIFY(!unended.ok());  // playback_end missing
    }

    void summaryWrongTypes()
    {
        QVERIFY(!parseSummary("").ok());
        QVERIFY(!parseSummary("[]").ok());
        QVERIFY(parseSummary("{}").ok());
        QVERIFY(parseSummary(R"({"ues": "x"})").ok());
        QVERIFY(parseSummary(R"({"ues": [null]})").ok());

        const auto odd = parseSummary(R"({"ues": [{"avg_bitrate_bps": "10", "switch_count": null,
            "completed": "true", "rebuffer_time_s": -1, "stall_count": 3, "startup_delay_s": 1e308,
            "avg_throughput_bps": []}, {"avg_bitrate_bps": 5}]})");
        QVERIFY(odd.ok());
        QVERIFY(!odd.value.averageBitrate);
        QVERIFY(!odd.value.averageThroughput);
        QVERIFY(!odd.value.switchCount);
        QVERIFY(!odd.value.completed);
        QCOMPARE(odd.value.rebufferTime, std::optional<double>(-1.0));
        QCOMPARE(odd.value.stallCount, std::optional<int>(3));
        QCOMPARE(odd.value.startupDelay, std::optional<double>(1e308));
    }

    // --- video.json and the video's segments.jsonl -------------------------------------------

    void videoJsonEdgeCases()
    {
        QVERIFY(!parseVideoJson("", {}).ok());
        QVERIFY(!parseVideoJson("{}", {}).ok());
        const auto rungsOutOfOrder = parseVideoJson(
            R"({"segment_count": 2, "rungs": [{"rung": 1}, {"rung": 0}, "x"]})", {});
        QVERIFY(!rungsOutOfOrder.ok());
        QCOMPARE(rungsOutOfOrder.value.ladder.size(), 3u);
        QVERIFY(!parseVideoJson(R"({"segment_count": -5, "rungs": [{"rung": 0}]})", {}).ok());
        QVERIFY(!parseVideoJson(R"({"segment_count": 1e12, "rungs": [{"rung": 0}]})", {}).ok());
        QVERIFY(!parseVideoJson(R"({"segment_count": 2, "rungs": [{"rung": 0}],
                                    "segment_file_pattern": "../../{rung}"})", {}).ok());
        // A missing pattern is accepted; the supported one is assumed.
        QVERIFY(parseVideoJson(R"({"segment_count": 2, "rungs": [{"rung": 0, "bitrate_bps": 1e300}]})", {}).ok());
        QCOMPARE(parseVideoJson(R"({"segment_count": 2, "rungs": [{"rung": 0, "bitrate_bps": 1e300}]})", {})
                     .value.ladder[0].bitrate, 0);
    }

    void videoSegmentTableEdgeCases()
    {
        // Negative dimensions give an empty table instead of a huge allocation.
        const VideoSegmentTable negative(-1, 6);
        QVERIFY(!negative.bytes(0, 0));
        QVERIFY(!negative.bytes(-1, -1));
        const auto fromNegative = parseVideoSegments(R"({"segment": 0, "rung": 0, "bytes": 1})", -1, 6);
        QVERIFY(!fromNegative.value.bytes(0, 0));
        const auto noRungs = parseVideoSegments(R"({"segment": 0, "rung": 0, "bytes": 1})", 3, -2);
        QVERIFY(!noRungs.value.bytes(0, 0));

        // Dimensions far beyond any real video are refused with one error, not allocated.
        const auto huge = parseVideoSegments(R"({"segment": 0, "rung": 0, "bytes": 1})",
                                             std::numeric_limits<int>::max(), 1000);
        QCOMPARE(huge.errors.size(), 1);
        QVERIFY2(huge.errors[0].startsWith(QStringLiteral("videos/segments.jsonl: ")),
                 qPrintable(huge.errors[0]));

        // Entries outside the table, wrong types and duplicates.
        const auto parsed = parseVideoSegments(
            R"({"segment": 0, "rung": 0, "bytes": 10})" "\n"
            R"({"segment": 0, "rung": 0, "bytes": 11})" "\n"
            R"({"segment": 5, "rung": 0, "bytes": 1})" "\n"
            R"({"segment": -1, "rung": 0, "bytes": 1})" "\n"
            R"({"segment": 1, "rung": 99, "bytes": 1})" "\n"
            R"({"segment": "1", "rung": 0, "bytes": 1e300})" "\n"
            R"({"segment": 1, "rung": 1})",
            2, 2);
        // The last entry wins; segment "1" (a string) reads as 0 and its size 1e300 as 0.
        QCOMPARE(parsed.value.bytes(0, 0), std::optional<qint64>(0));
        QCOMPARE(parsed.value.bytes(0, 1), std::nullopt);
        QCOMPARE(parsed.value.bytes(5, 0), std::nullopt);
        // 1 missing field + (0,1), (1,0), (1,1) without an entry.
        QCOMPARE(parsed.errors.size(), 4);
    }

    // --- sim.log -----------------------------------------------------------------------------

    void simLogIgnoresNoise()
    {
        const SimLogParser parser(7);
        const SimLogPart part = parser.parseChunk(kNoisyLog);
        QCOMPARE(part.decisions.size(), 2u);
        QCOMPARE(part.decisions[0].bufferFullWait, 0.5);
        QCOMPARE(part.decisions[1].segment, 1);
        QCOMPARE(part.decisions[1].bufferLevel, 4.0);
        QCOMPARE(part.decisions[1].throughputEstimate, 0.0);  // missing optional field
        QCOMPARE(part.buffer.size(), 7u);
        QCOMPARE(part.playback.size(), 3u);
        QCOMPARE(part.playback[0].framesPlayed, 3);
        QCOMPARE(part.playback[1].framesPlayed, 3);
        QCOMPARE(part.playback[1].lastFrameTime, 57.3);
        QCOMPARE(part.playback[2].bitrate, 6000000);
        QCOMPARE(part.pauses.size(), 1u);
        QCOMPARE(part.resumes.size(), 1u);
        QCOMPARE(part.resumes[0].pauseDuration, 0.3607);
        // The progress bar line with a lone CR counts as one line.
        QCOMPARE(part.linesRead, 18);

        // Nothing at all for another UE, or for an empty or binary chunk.
        const SimLogPart other = SimLogParser(99).parseChunk(kNoisyLog);
        QVERIFY(other.decisions.empty() && other.buffer.empty() && other.pauses.empty());
        QCOMPARE(parser.parseChunk("").linesRead, 0);
        QByteArray junk(4096, '\0');
        for (qsizetype i = 0; i < junk.size(); ++i)
            junk[i] = static_cast<char>((i * 131 + 7) % 256);
        const SimLogPart binary = parser.parseChunk(junk);
        QVERIFY(binary.decisions.empty() && binary.buffer.empty() && binary.playback.empty());
    }

    void simLogMalformedLines()
    {
        const SimLogParser parser(7);
        const QByteArray text =
            // Decision lines missing a required value, or with units glued on.
            "0.1 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: oldBitRate: 250000\n"
            "0.2 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 3000000bps oldBitRate: 250000\n"
            "0.3s ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 3000000 oldBitRate: 250000\n"
            "0.4 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 3000000 oldBitRate: 250000 T:\n"
            // Repeated keys: the first wins.
            "0.5 ue-id: 7 ue-id: 8 videoId: 3 segmentId: 4 segmentId: 5 newBitRate: 1 oldBitRate: 2\n"
            // Frame lines missing a value.
            "0x1 1.0 PLAYING FRAME: PlayerId: 2 SegId: 0 Res: 1 FrameId: 0\n"
            "0x1 1.0 PLAYING FRAME: PlayerId: 2 SegId: 0 Res: 1 FrameId: x queueLength: 1\n"
            "0x1 PLAYING FRAME: PlayerId: 2 SegId: 0 Res: 1 FrameId: 0 queueLength: 1\n"
            "0x\n"
            "0\n"
            "9\n"
            // Player lines without a time, without an id, or with an empty duration.
            "[MPEG_PLAYER] No frames to play at Player ID=2\n"
            "[MPEG_PLAYER] No frames to play at t=s. Player ID=2\n"
            "[MPEG_PLAYER] No frames to play at t=3s.\n"
            "[MPEG_PLAYER] No frames to play at t=4s. Player ID=\n"
            "[MPEG_PLAYER] Play resumed at t=5s. Player ID=2 Pause duration=s.\n"
            "[MPEG_PLAYER] Something else at t=6s. Player ID=2\n"
            "[MPEG_PLAYER]\n";
        const SimLogPart part = parser.parseChunk(text);
        QCOMPARE(part.decisions.size(), 2u);
        QCOMPARE(part.decisions[0].time, 0.4);
        QCOMPARE(part.decisions[0].bufferLevel, 0.0);
        QCOMPARE(part.decisions[1].segment, 4);
        QVERIFY(part.buffer.empty());
        QVERIFY(part.playback.empty());
        QVERIFY(part.pauses.empty());
        QCOMPARE(part.resumes.size(), 1u);
        QCOMPARE(part.resumes[0].pauseDuration, 0.0);
    }

    void simLogExtremeNumbers()
    {
        // Values Qt reads as NaN, infinity or beyond 64 bits must not reach an integer cast
        // (undefined behaviour); such lines are skipped like any other unreadable line.
        const SimLogParser parser(7);
        QByteArray text;
        for (const char* rate : {"nan", "inf", "-inf", "1e300", "-1e300", "9.3e18"}) {
            text += "0.1 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: " + QByteArray(rate)
                  + " oldBitRate: 250000\n";
            text += "0.1 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 250000 oldBitRate: "
                  + QByteArray(rate) + "\n";
            text += "0x1 1.0 PLAYING FRAME: PlayerId: 2 SegId: 0 Res: " + QByteArray(rate)
                  + " FrameId: 0 queueLength: 1\n";
        }
        // FrameId + 1 would overflow an int.
        text += "0x1 1.0 PLAYING FRAME: PlayerId: 2 SegId: 3 Res: 1 FrameId: 2147483647 queueLength: 1\n";
        text += "0x1 1.1 PLAYING FRAME: PlayerId: 2 SegId: 3 Res: 1 FrameId: 0 queueLength: 1\n";
        text += "0x1 1.2 PLAYING FRAME: PlayerId: 2 SegId: 3 Res: 1 FrameId: 2147483647 queueLength: 1\n";
        text += "0x1 1.3 PLAYING FRAME: PlayerId: 2 SegId: 4 Res: 1 FrameId: -5 queueLength: -1\n";

        const SimLogPart part = parser.parseChunk(text);
        QVERIFY(part.decisions.empty());
        QCOMPARE(part.playback.size(), 2u);
        QCOMPARE(part.playback[0].segment, 3);
        QCOMPARE(part.playback[0].framesPlayed, 1);
        QCOMPARE(part.playback[1].framesPlayed, -4);

        // Large values that fit are kept.
        const SimLogPart big = parser.parseChunk(
            "0.1 ue-id: 7 videoId: 3 segmentId: 0 newBitRate: 9e18 oldBitRate: -9e18 T: nan\n");
        QCOMPARE(big.decisions.size(), 1u);
        QCOMPARE(big.decisions[0].newBitRate, 9000000000000000000);
        QCOMPARE(big.decisions[0].oldBitRate, -9000000000000000000);
        QVERIFY(std::isnan(big.decisions[0].bufferLevel));
    }

    void simLogSplitAnywhereGivesTheSameResult()
    {
        const SimLogParser parser(7);
        const QByteArray log = kNoisyLog;
        const QString whole = dumpPart(parser.parseChunk(log));

        // Two pieces, with the cut at every byte offset moved to the next line start, as the
        // loader does.
        for (qsizetype offset = 0; offset <= log.size(); ++offset) {
            const qsizetype newline = log.indexOf('\n', offset);
            const qsizetype cut = newline < 0 ? log.size() : newline + 1;
            const QString merged = dumpPart(parseInPieces(parser, log, {cut}));
            QVERIFY2(merged == whole, qPrintable(QStringLiteral("cut at %1").arg(cut)));
        }

        // Three pieces, at every pair of line starts.
        QList<qsizetype> starts;
        for (qsizetype i = 0; i < log.size(); ++i) {
            if (log[i] == '\n')
                starts.append(i + 1);
        }
        for (qsizetype a = 0; a < starts.size(); ++a) {
            for (qsizetype b = a; b < starts.size(); ++b) {
                const QString merged = dumpPart(parseInPieces(parser, log, {starts[a], starts[b]}));
                QVERIFY2(merged == whole,
                         qPrintable(QStringLiteral("cuts at %1, %2").arg(starts[a]).arg(starts[b])));
            }
        }

        // One piece per line.
        QCOMPARE(dumpPart(parseInPieces(parser, log, starts)), whole);

        // splitAtLines, for any number of parts.
        for (qsizetype parts = 1; parts <= 64; ++parts) {
            SimLogPart merged;
            for (QByteArrayView chunk : splitAtLines(log, parts))
                SimLogParser::merge(merged, parser.parseChunk(chunk));
            QVERIFY2(dumpPart(merged) == whole, qPrintable(QStringLiteral("%1 parts").arg(parts)));
        }

        // Merging empty parts changes nothing.
        SimLogPart merged = parser.parseChunk(log);
        SimLogParser::merge(merged, SimLogPart{});
        SimLogPart fromEmpty;
        SimLogParser::merge(fromEmpty, parser.parseChunk(log));
        QCOMPARE(dumpPart(merged), whole);
        QCOMPARE(dumpPart(fromEmpty), whole);
    }

    void simLogRealFileSplitAnywhere()
    {
        // The real log of a bundle, cut into 2..48 pieces, gives the same result as in one.
        const auto file = MappedFile::open(test::dataPath(QStringLiteral("bbb_36/logs/sim.log")));
        QVERIFY(file);
        const SimLogParser parser(7);
        const QString whole = dumpPart(parser.parseChunk(file->view()));
        for (qsizetype parts = 2; parts <= 48; parts += 7) {
            SimLogPart merged;
            for (QByteArrayView chunk : splitAtLines(file->view(), parts))
                SimLogParser::merge(merged, parser.parseChunk(chunk));
            QVERIFY2(dumpPart(merged) == whole, qPrintable(QStringLiteral("%1 parts").arg(parts)));
        }
    }

    void stallsFromOddPauses()
    {
        SimLogPart log;
        // Resumes before any pause, two pauses sharing one resume, a pause after stopTime.
        log.resumes = {{1.0, 0.0}, {5.0, 1.0}};
        log.pauses = {{2.0}, {3.0}, {9.0}};
        const auto stalls = SimLogParser::stalls(log, false, 8.0);
        QCOMPARE(stalls.size(), 3u);
        QCOMPARE(stalls[0].end, 5.0);
        QCOMPARE(stalls[1].end, 5.0);
        QVERIFY(stalls[2].terminal);
        QCOMPARE(stalls[2].end, 9.0);  // never before its own start
        QVERIFY(SimLogParser::stalls(SimLogPart{}, true, 0.0).empty());
    }

    // --- policy.log --------------------------------------------------------------------------

    void policyLogEdgeCases()
    {
        const QByteArray text =
            "\xEF\xBB\xBF# comment with a BOM before it\n"
            "  # indented comment\n"
            "\t0.2 ue-id: 7 seg(1-idx): 1 rung: 4 reason: llm\r\n"
            "0.3 ue-id: 7 seg(1-idx): 2 rung: 4\n"                    // no reason
            "0.4 ue-id: 7 seg(1-idx): 3 rung: 4 reason:\n"            // empty reason
            "0.5 ue-id: 7 seg(1-idx): 4 rung: 3 reason: x+guard(buf5.8:r->r4)\n"
            "0.6 ue-id: 7 seg(1-idx): 5 rung: 3 reason: x+guard(buf5.8:r5\n"
            "0.7 ue-id: 7 seg(1-idx): 6 rung: 3 reason: x+guard(buf5.8)->r4:r9\n"
            "0.8 ue-id: 7 seg(1-idx): 7 rung: 3 reason: guard(:r2->r1)\n"
            "0.9 ue-id: 7 seg(1-idx): 8 rung: 3 reason: x+guard(buf:r99999999999->r1)\n"
            "1.0 ue-id: 7 seg(1-idx): x rung: 3 reason: y\n"          // bad segment
            "1.1 ue-id: 7 seg(1-idx): 9 reason: y\n"                  // no rung
            "ue-id: 7 seg(1-idx): 9 rung: 3\n"                        // no time
            "1.2 ue-id: 8 seg(1-idx): 9 rung: 3\n"                    // other UE
            "1.3 ue-id:7 seg(1-idx): 9 rung: 3\n";                    // no space after key
        const auto decisions = parsePolicyLog(text, 7);
        QCOMPARE(decisions.size(), 8u);
        // The BOM makes the first word unreadable, so the BOM line is skipped as a comment
        // would be; tabs before the line are trimmed.
        QCOMPARE(decisions[0].time, 0.2);
        QCOMPARE(decisions[0].reason, QStringLiteral("llm"));
        QVERIFY(decisions[1].reason.isEmpty());
        QVERIFY(decisions[2].reason.isEmpty());
        QVERIFY(decisions[3].guardOverride);
        QVERIFY(!decisions[3].llmRung);
        QVERIFY(!decisions[4].llmRung);
        QVERIFY(!decisions[5].llmRung);
        QVERIFY(!decisions[6].guardOverride);  // needs "+guard("
        QCOMPARE(decisions[6].llmRung, std::optional<int>(2));
        QVERIFY(!decisions[7].llmRung);  // does not fit an int

        QVERIFY(parsePolicyLog("", 7).empty());
        QVERIFY(parsePolicyLog(QByteArray("\0\0\0\n\xff", 5), 7).empty());
    }

    // --- network topology --------------------------------------------------------------------

    void topologyEdgeCases()
    {
        const QString full = QStringLiteral(
            "Install Satellite eNB with CellId 1 on Node ID 3 at position (500, 500, 550000)\n"
            "Install IAB device with CellId 2 on Node ID 4 at position (500, 500, 10)\n"
            "distance 549990\n"
            "Center Frequency: 28 GHz\n"
            "Total Bandwidth: 100.008 MHz\n"
            "AttachToSingleClosestEnb called for UE device: 0x1 node_id: 7\n"
            "Closest eNB found at distance: 85.08 node_id: 4\n");

        // No header at all, garbage, and the noise of real logs.
        for (const QString& text :
             {QString(), QStringLiteral("\n\n\n"), QStringLiteral("garbage \x01\x02"),
              QStringLiteral("[Python][LLM] node=7 seg=1 choice=6000000\n[Python] ready\n")}) {
            const NetworkTopology t = parseNetworkTopology(text, 7);
            QVERIFY(!t.satelliteNode && !t.hasSatellite() && !t.servingNode && !t.carrierFrequencyGHz);
            QCOMPARE(t.relayCount, 0);
            QVERIFY(!t.viaRelay);
        }

        // Only part of the header: the rest stays empty.
        const NetworkTopology partial =
            parseNetworkTopology(full.section('\n', 0, 0) + QStringLiteral("\n"), 7);
        QCOMPARE(partial.satelliteNode, std::optional<int>(3));
        QVERIFY(!partial.servingNode);
        QVERIFY(!partial.viaRelay);

        // Numbers without their units, or that are not numbers, are not read.
        const NetworkTopology units = parseNetworkTopology(
            QStringLiteral("Center Frequency: 28\nTotal Bandwidth: MHz\nCenter Frequency: GHz\n"
                           "Install Satellite eNB with CellId 1 on Node ID 3 at position (e, ., -)\n"),
            7);
        QVERIFY(!units.carrierFrequencyGHz);
        QVERIFY(!units.bandwidthMHz);
        QCOMPARE(units.satelliteNode, std::optional<int>(3));
        QVERIFY(!units.satelliteAltitude);

        // Repeated keys: the last line wins, except the relay's first "distance".
        const NetworkTopology repeated = parseNetworkTopology(
            full + QStringLiteral("Center Frequency: 3.5 GHz\ndistance 1\n"
                                  "Install IAB device with CellId 5 on Node ID 9 at position (1, 1, 1)\n"),
            7);
        QCOMPARE(repeated.carrierFrequencyGHz, std::optional<double>(3.5));
        QCOMPARE(repeated.backhaulDistance, std::optional<double>(549990.0));
        QCOMPARE(repeated.relayCount, 2);

        // The closest-eNB line belongs to the attach call before it; other UEs are ignored.
        const NetworkTopology otherUe = parseNetworkTopology(
            QStringLiteral("Closest eNB found at distance: 1 node_id: 4\n"
                           "AttachToSingleClosestEnb called for UE device: 0x1 node_id: 8\n"
                           "Closest eNB found at distance: 2 node_id: 4\n"),
            7);
        QVERIFY(!otherUe.servingNode);

        // CRLF and surrounding whitespace.
        QString crlf = full;
        crlf.replace(QStringLiteral("\n"), QStringLiteral("  \r\n"));
        const NetworkTopology fromCrlf = parseNetworkTopology(crlf, 7);
        QCOMPARE(fromCrlf.backhaulDistance, std::optional<double>(549990.0));
        QCOMPARE(fromCrlf.accessDistance, std::optional<double>(85.08));
        QVERIFY(fromCrlf.viaRelay);

        // Served by the satellite itself: not via a relay.
        const NetworkTopology direct = parseNetworkTopology(
            full + QStringLiteral("AttachToSingleClosestEnb called for UE device: 0x1 node_id: 7\n"
                                  "Closest eNB found at distance: 5 node_id: 3\n"),
            7);
        QCOMPARE(direct.servingNode, std::optional<int>(3));
        QVERIFY(!direct.viaRelay);
    }

    void topologyFromFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray header =
            "Install Satellite eNB with CellId 1 on Node ID 3 at position (500, 500, 550000)\r\n"
            "Center Frequency: 28 GHz\r\n";

        // CRLF, a BOM and non-UTF-8 bytes in the file.
        const QString path = dir.filePath(QStringLiteral("sim.log"));
        QVERIFY(writeFile(path, "\xEF\xBB\xBF" + header + "\xff\xfe junk\r\n"));
        const NetworkTopology t = readNetworkTopology(path, 7);
        QCOMPARE(t.satelliteAltitude, std::optional<double>(550000.0));
        QCOMPARE(t.carrierFrequencyGHz, std::optional<double>(28.0));

        // A very long first line does not hide the header after it.
        QVERIFY(writeFile(path, QByteArray(2 * 1024 * 1024, 'x') + "\n" + header));
        QCOMPARE(readNetworkTopology(path, 7).carrierFrequencyGHz, std::optional<double>(28.0));

        // Only the head of the log is read: set-up lines far down are ignored.
        QByteArray late;
        for (int i = 0; i < 5000; ++i)
            late += "0x1 1.0 PLAYING FRAME: PlayerId: 2\n";
        QVERIFY(writeFile(path, late + header));
        QVERIFY(!readNetworkTopology(path, 7).carrierFrequencyGHz);

        // Missing, empty, a directory: empty topology.
        QVERIFY(!readNetworkTopology(dir.filePath(QStringLiteral("missing")), 7).satelliteNode);
        QVERIFY(writeFile(path, {}));
        QVERIFY(!readNetworkTopology(path, 7).satelliteNode);
        QVERIFY(!readNetworkTopology(dir.path(), 7).satelliteNode);
    }

    // --- MP4 duration ------------------------------------------------------------------------

    void mp4TruncatedAtManyOffsets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("cut.mp4"));
        for (const char* name : {"one_second_faststart.mp4", "version1_largesize.mp4",
                                 "not_an_mp4.mp4", "two_and_a_half_seconds.mp4"}) {
            const QByteArray bytes = readFile(test::dataPath(QStringLiteral("media/") + name));
            QVERIFY(!bytes.isEmpty());
            const auto full = readMp4Duration(test::dataPath(QStringLiteral("media/") + name));
            // Every offset for the small files; for the larger one, the head, the tail and a
            // stride in between.
            for (qsizetype size = 0; size <= bytes.size(); ++size) {
                const bool check = bytes.size() <= 4096 || size < 1024
                                || size > bytes.size() - 1024 || size % 61 == 0;
                if (!check)
                    continue;
                QVERIFY(writeFile(path, bytes.left(size)));
                const auto duration = readMp4Duration(path);
                // Either the headers it needs are intact and the answer is the same, or nothing.
                QVERIFY2(!duration || duration == full,
                         qPrintable(QStringLiteral("%1 cut to %2 bytes gave %3")
                                        .arg(QLatin1StringView(name))
                                        .arg(size)
                                        .arg(*duration)));
            }
        }
    }

    void mp4CraftedBoxes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("x.mp4"));
        const auto duration = [&](const QByteArray& bytes) {
            return writeFile(path, bytes) ? readMp4Duration(path) : std::optional<double>(-1.0);
        };
        const QByteArray moovPayload = syntheticMp4().mid(kFtyp.size() + 8);

        QCOMPARE(duration(syntheticMp4()), std::optional<double>(2.5));
        // No video track: the movie header.
        QCOMPARE(duration(syntheticMp4("soun")), std::optional<double>(5.0));
        // A media header with timescale 0 gives nothing (no fallback to the movie header).
        QCOMPARE(duration(syntheticMp4("vide", headerV0(0, 250))), std::nullopt);

        // Size 0: moov extends to the end of the file.
        QCOMPARE(duration(kFtyp + be32(0) + "moov" + moovPayload), std::optional<double>(2.5));
        // Size 0 in front of moov swallows it.
        QCOMPARE(duration(kFtyp + be32(0) + "free" + syntheticMp4().mid(kFtyp.size())),
                 std::nullopt);
        // Sizes below the header size, or past the end of the file.
        for (quint32 size : {4u, 7u}) {
            QCOMPARE(duration(kFtyp + be32(size) + "free" + syntheticMp4().mid(kFtyp.size())),
                     std::nullopt);
        }
        QCOMPARE(duration(kFtyp + be32(0xFFFFFFFF) + "free" + syntheticMp4().mid(kFtyp.size())),
                 std::nullopt);
        QCOMPARE(duration(kFtyp + be32(static_cast<quint32>(8 + moovPayload.size() + 1)) + "moov"
                          + moovPayload),
                 std::nullopt);

        // 64-bit sizes: valid, too small, past the end, and so large that adding the offset
        // would overflow a signed 64-bit integer.
        QCOMPARE(duration(kFtyp + be32(1) + "moov" + be64(quint64(16 + moovPayload.size()))
                          + moovPayload),
                 std::optional<double>(2.5));
        for (quint64 size : {quint64(0), quint64(8), quint64(15), quint64(1) << 40,
                             quint64(0x7FFFFFFFFFFFFFF0), quint64(0x7FFFFFFFFFFFFFFF),
                             quint64(0x8000000000000000), ~quint64(0)}) {
            QCOMPARE(duration(kFtyp + be32(1) + "free" + be64(size)
                              + syntheticMp4().mid(kFtyp.size())),
                     std::nullopt);
        }
        // The 64-bit size itself cut off.
        QCOMPARE(duration(kFtyp + be32(1) + "moov" + be64(100).left(3)), std::nullopt);

        // A media header too short for its fields must not read the bytes after it.
        const QByteArray shortMdhd = box("mdhd", be32(0));
        const QByteArray decoy = box("free", be32(0) + be32(0) + be32(10) + be32(990));
        const QByteArray mdiaShort =
            box("mdia", box("hdlr", hdlrPayload("vide")) + shortMdhd + decoy);
        QCOMPARE(duration(kFtyp + box("moov", box("mvhd", headerV0(1000, 5000)) + box("trak", mdiaShort))),
                 std::nullopt);
        // A version 1 header with only version 0 room.
        const QByteArray v1Short = box("mdhd", be32(0x01000000) + QByteArray(16, '\0'));
        const QByteArray mdiaV1 =
            box("mdia", box("hdlr", hdlrPayload("vide")) + v1Short + box("free", QByteArray(32, '\x01')));
        QCOMPARE(duration(kFtyp + box("moov", box("mvhd", headerV0(1000, 5000)) + box("trak", mdiaV1))),
                 std::nullopt);
        // A handler box too short to hold the handler type must not read the next box's type:
        // here the next box is named "vide", which would make an audio track look like video.
        const QByteArray mdiaHdlr =
            box("mdia", box("hdlr", be32(0)) + box("vide", {}) + box("mdhd", headerV0(100, 250)));
        QCOMPARE(duration(kFtyp + box("moov", box("mvhd", headerV0(1000, 5000)) + box("trak", mdiaHdlr))),
                 std::optional<double>(5.0));

        // Version 1 header with the largest duration.
        const QByteArray v1 = be32(0x01000000) + be64(0) + be64(0) + be32(1) + be64(~quint64(0));
        QCOMPARE(duration(syntheticMp4("vide", v1)), std::optional<double>(18446744073709551615.0));

        // Many empty boxes before moov.
        QByteArray padding;
        for (int i = 0; i < 20000; ++i)
            padding += box("free", {});
        QCOMPARE(duration(kFtyp + padding + syntheticMp4().mid(kFtyp.size())),
                 std::optional<double>(2.5));

        // Not files at all.
        QCOMPARE(duration({}), std::nullopt);
        QCOMPARE(readMp4Duration(dir.path()), std::nullopt);
        QCOMPARE(readMp4Duration(QString()), std::nullopt);
    }

    // --- odd files ---------------------------------------------------------------------------

    void oddFilesAreNotReadAsData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        QVERIFY(!MappedFile::open(dir.path(), &error));
        QTest::ignoreMessage(QtWarningMsg, "QFSFileEngine::open: No file name specified");
        QVERIFY(!MappedFile::open(QString(), &error));

        // A symlink loop.
        const QString loop = dir.filePath(QStringLiteral("loop"));
        QVERIFY(QFile::link(loop, loop));
        QVERIFY(!MappedFile::open(loop, &error));
        QVERIFY(!readMp4Duration(loop));
        QVERIFY(!readNetworkTopology(loop, 7).satelliteNode);

        // An unreadable file.
        if (!isRoot()) {
            const QString locked = dir.filePath(QStringLiteral("locked"));
            QVERIFY(writeFile(locked, "data"));
            QVERIFY(QFile::setPermissions(locked, QFile::Permissions()));
            error.clear();
            QVERIFY(!MappedFile::open(locked, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!readMp4Duration(locked));
            QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner);
        }
    }

    void fifoDoesNotBlock()
    {
        // Opening a FIFO for reading blocks until a writer appears; the readers must refuse
        // anything that is not a regular file instead of hanging.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fifo = dir.filePath(QStringLiteral("fifo"));
        QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);

        std::atomic<int> done{0};
        std::thread reader([&] {
            QString error;
            const bool mapped = MappedFile::open(fifo, &error).has_value();
            const bool duration = readMp4Duration(fifo).has_value();
            const bool topology = readNetworkTopology(fifo, 7).satelliteNode.has_value();
            done = (mapped || duration || topology) ? 2 : 1;
        });
        // Hang guard only: the readers return at once when correct.
        for (int i = 0; i < 500 && done == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const bool hung = done == 0;
        while (done == 0) {
            // Unblock the reader by opening and closing the write end.
            const int fd = ::open(QFile::encodeName(fifo).constData(), O_WRONLY | O_NONBLOCK);
            if (fd >= 0)
                ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        reader.join();
        QVERIFY2(!hung, "reading a FIFO blocked");
        QCOMPARE(done.load(), 1);
    }

    // --- broken bundles ----------------------------------------------------------------------

    void bundleWithoutVideosFolder()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
        QVERIFY(!bundle.isEmpty());
        QVERIFY(QDir(QDir(bundle).filePath(QStringLiteral("videos"))).removeRecursively());

        const LoadResult sync = loadBundleSync(bundle);
        QVERIFY(!sync.run);
        QVERIFY2(sync.errors.first().startsWith(QStringLiteral("videos/chef_cooking/video.json: cannot read")),
                 qPrintable(sync.errors.join('\n')));
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 2);
        QVERIFY(!readBundleSummary(bundle).errors.isEmpty());
    }

    void bundleWithBrokenSegmentCounts()
    {
        // segment_count that would size the table to nothing or to hundreds of gigabytes.
        for (const QByteArray& count : {QByteArray("-1"), QByteArray("0"), QByteArray("2000000000")}) {
            QTemporaryDir dir;
            const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
            const QString videoJson =
                QDir(bundle).filePath(QStringLiteral("videos/chef_cooking/video.json"));
            QByteArray json = readFile(videoJson);
            json.replace("\"segment_count\": 11", "\"segment_count\": " + count);
            QVERIFY(writeFile(videoJson, json));

            const LoadResult sync = loadBundleSync(bundle);
            QVERIFY2(!sync.errors.isEmpty(), count.constData());
            if (count == "2000000000") {
                // The video header is valid, so the run loads; the size table is refused.
                QVERIFY(sync.run);
                QCOMPARE(sync.run->segments.size(), 11u);
                QVERIFY(std::ranges::any_of(sync.errors, [](const QString& e) {
                    return e.startsWith(QStringLiteral("videos/segments.jsonl"));
                }));
            } else {
                QVERIFY(!sync.run);
            }
            COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 3);
            QVERIFY(!readBundleSummary(bundle).directory.isEmpty());
        }
    }

    void bundleWithMissingAndBrokenMedia()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
        LoadResult first = loadBundleSync(bundle);
        QVERIFY(first.run);
        const QByteArray good = readFile(test::dataPath(QStringLiteral("media/one_second_faststart.mp4")));
        for (const Segment& segment : first.run->segments) {
            QDir().mkpath(QFileInfo(segment.mediaPath).absolutePath());
            QVERIFY(writeFile(segment.mediaPath, good));
        }
        const auto& segments = first.run->segments;
        QVERIFY(QFile::remove(segments[1].mediaPath));
        QVERIFY(writeFile(segments[2].mediaPath, "not an mp4"));
        QVERIFY(writeFile(segments[3].mediaPath, good.left(500)));  // inside moov
        QVERIFY(writeFile(segments[4].mediaPath, {}));
        QVERIFY(QFile::remove(segments[5].mediaPath));
        QVERIFY(QDir().mkpath(segments[5].mediaPath));  // a directory in place of the file
        QVERIFY(QFile::remove(segments[6].mediaPath));
        QVERIFY(QFile::link(segments[6].mediaPath, segments[6].mediaPath));

        const LoadResult sync = loadBundleSync(bundle);
        QVERIFY2(sync.errors.isEmpty(), qPrintable(sync.errors.join('\n')));
        const RunData& run = *sync.run;
        for (std::size_t i = 0; i < run.segments.size(); ++i) {
            const bool broken = i >= 1 && i <= 6;
            QCOMPARE(run.segments[i].mediaDuration, broken ? 2.0 : 1.0);
        }
        const auto missing = std::ranges::find_if(run.warnings, [](const Warning& w) {
            return w.kind == Warning::Kind::MissingMedia;
        });
        QVERIFY(missing != run.warnings.end());
        QVERIFY2(missing->message.startsWith(QStringLiteral("6 of 11")), qPrintable(missing->message));
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 4);
    }

    void bundleWithEmptyOrBlankFiles_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<bool>("loads");
        QTest::addColumn<bool>("hasErrors");
        const QList<std::tuple<const char*, bool, bool>> files = {
            {"run.json", false, true},
            {"segments.jsonl", false, true},
            {"player_events.jsonl", false, true},
            {"videos/chef_cooking/video.json", false, true},
            {"videos/chef_cooking/segments.jsonl", true, true},
            {"summary.json", true, true},
            {"samples.jsonl", true, false},
            {"logs/sim.log", true, false},
            {"logs/policy.log", true, false},
        };
        for (const auto& [file, loads, hasErrors] : files) {
            QTest::addRow("%s empty", file) << QString::fromLatin1(file) << QByteArray() << loads
                                            << hasErrors;
            QTest::addRow("%s blank lines", file)
                << QString::fromLatin1(file) << QByteArray("\n\r\n  \n") << loads << hasErrors;
        }
    }

    void bundleWithEmptyOrBlankFiles()
    {
        QFETCH(QString, file);
        QFETCH(QByteArray, content);
        QFETCH(bool, loads);
        QFETCH(bool, hasErrors);
        QTemporaryDir dir;
        const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
        QVERIFY(writeFile(QDir(bundle).filePath(file), content));

        const LoadResult sync = loadBundleSync(bundle);
        QCOMPARE(bool(sync.run), loads);
        QVERIFY2(sync.errors.isEmpty() != hasErrors, qPrintable(sync.errors.join('\n')));
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 1);
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 8);
        if (file == QLatin1StringView("logs/sim.log"))
            QVERIFY(sync.run->playback.empty() && sync.run->buffer.empty());
    }

    void bundleWithTruncatedFiles()
    {
        // Every file cut in the middle of its last line: never a crash, the same result from
        // both loaders, and only the essential files stop the run from loading.
        const QStringList files = {QStringLiteral("run.json"),
                                   QStringLiteral("segments.jsonl"),
                                   QStringLiteral("samples.jsonl"),
                                   QStringLiteral("player_events.jsonl"),
                                   QStringLiteral("summary.json"),
                                   QStringLiteral("videos/chef_cooking/video.json"),
                                   QStringLiteral("videos/chef_cooking/segments.jsonl"),
                                   QStringLiteral("logs/sim.log"),
                                   QStringLiteral("logs/policy.log")};
        for (const QString& file : files) {
            QTemporaryDir dir;
            const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
            const QString path = QDir(bundle).filePath(file);
            QByteArray bytes = readFile(path);
            while (bytes.endsWith('\n'))
                bytes.chop(1);
            QVERIFY(writeFile(path, bytes.left(bytes.size() - 7)));
            const LoadResult sync = loadBundleSync(bundle);
            COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 3);
            if (file == QLatin1StringView("segments.jsonl")) {
                QVERIFY(sync.run);
                QCOMPARE(sync.run->decisions.size(), 10u);
                QVERIFY2(sync.errors.first().startsWith(QStringLiteral("segments.jsonl:11:")),
                         qPrintable(sync.errors.join('\n')));
            }
            if (file == QLatin1StringView("run.json") || file.endsWith(QLatin1StringView("video.json")))
                QVERIFY(!sync.run);
        }
    }

    void bundleWithUnreadableFiles()
    {
        if (isRoot())
            QSKIP("permissions do not apply to root");
        const QStringList files = {QStringLiteral("run.json"),
                                   QStringLiteral("segments.jsonl"),
                                   QStringLiteral("samples.jsonl"),
                                   QStringLiteral("summary.json"),
                                   QStringLiteral("videos/chef_cooking/segments.jsonl"),
                                   QStringLiteral("logs/sim.log"),
                                   QStringLiteral("logs/policy.log"),
                                   QStringLiteral("logs")};
        for (const QString& file : files) {
            QTemporaryDir dir;
            const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
            const QString path = QDir(bundle).filePath(file);
            QVERIFY(QFile::setPermissions(path, QFile::Permissions()));
            const LoadResult sync = loadBundleSync(bundle);
            const LoadResult async = loadAsync(bundle, 2);
            const BundleSummary summary = readBundleSummary(bundle);
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

            QVERIFY2(std::ranges::any_of(sync.errors, [](const QString& e) {
                         return e.contains(QStringLiteral("cannot read"));
                     }),
                     qPrintable(file + ": " + sync.errors.join('\n')));
            QCOMPARE(dumpResult(async), dumpResult(sync));
            QCOMPARE(bool(sync.run), file != QLatin1StringView("run.json")
                                         && file != QLatin1StringView("segments.jsonl"));
            QCOMPARE(summary.errors.isEmpty(), file != QLatin1StringView("run.json"));
        }
    }

    void bundleWithSymlinkLoops()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"));
        const QDir b(bundle);
        QVERIFY(QFile::remove(b.filePath(QStringLiteral("logs/sim.log"))));
        QVERIFY(QFile::link(b.filePath(QStringLiteral("logs/sim.log")),
                            b.filePath(QStringLiteral("logs/sim.log"))));
        // A loop back to the bundle itself inside the bundle.
        QVERIFY(QFile::link(bundle, b.filePath(QStringLiteral("logs/self"))));

        const LoadResult sync = loadBundleSync(bundle);
        QVERIFY(sync.run);
        QVERIFY2(sync.errors.size() == 1 && sync.errors[0].startsWith(QStringLiteral("logs/sim.log: cannot read")),
                 qPrintable(sync.errors.join('\n')));
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 2);

        // The video folder as a loop.
        QVERIFY(QDir(b.filePath(QStringLiteral("videos/chef_cooking"))).removeRecursively());
        QVERIFY(QFile::link(b.filePath(QStringLiteral("videos/chef_cooking")),
                            b.filePath(QStringLiteral("videos/chef_cooking"))));
        const LoadResult noVideo = loadBundleSync(bundle);
        QVERIFY(!noVideo.run);
        COMPARE_WITH_PARALLEL_LOAD(noVideo, bundle, 2);
    }

    void bundlePathWithSpacesAndNonAscii()
    {
        QTemporaryDir dir;
        const QString name = QStringLiteral("run with spaces \u00e9\u00e8 \u65e5\u672c \U0001F3AC");
        const QString bundle = copyBundle(dir, QStringLiteral("chef_cooking_18"), name);
        QVERIFY(!bundle.isEmpty());

        // Media in the unicode path too.
        const LoadResult first = loadBundleSync(bundle);
        QVERIFY2(first.run, qPrintable(first.errors.join('\n')));
        const QByteArray good = readFile(test::dataPath(QStringLiteral("media/one_second_faststart.mp4")));
        for (const Segment& segment : first.run->segments) {
            QVERIFY(segment.mediaPath.contains(name));
            QDir().mkpath(QFileInfo(segment.mediaPath).absolutePath());
            QVERIFY(writeFile(segment.mediaPath, good));
        }

        const LoadResult sync = loadBundleSync(bundle);
        QVERIFY2(sync.errors.isEmpty(), qPrintable(sync.errors.join('\n')));
        QVERIFY(!sync.run->hasWarning(Warning::Kind::MissingMedia));
        QCOMPARE(sync.run->segments[0].mediaDuration, 1.0);
        QVERIFY(sync.run->topology.hasSatellite());
        QCOMPARE(QFileInfo(sync.run->info.bundleDirectory).fileName(), name);
        COMPARE_WITH_PARALLEL_LOAD(sync, bundle, 4);

        QCOMPARE(findBundles(dir.path()), QStringList{QDir(bundle).absolutePath()});
        const BundleSummary summary = readBundleSummary(bundle);
        QVERIFY2(summary.errors.isEmpty(), qPrintable(summary.errors.join('\n')));
        QCOMPARE(summary.segments, 11);
    }

    void libraryScanOfAWideFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir root(dir.path());
        for (int i = 0; i < 3000; ++i)
            QVERIFY(root.mkdir(QStringLiteral("empty_%1").arg(i, 4, 10, QLatin1Char('0'))));
        QVERIFY(!copyBundle(dir, QStringLiteral("chef_cooking_18"), QStringLiteral("b_chef")).isEmpty());
        QVERIFY(!copyBundle(dir, QStringLiteral("bbb_36"), QStringLiteral("a_bbb")).isEmpty());
        // Symlinks: a loop, a link to the library itself, a link to a bundle, a dangling one.
        QVERIFY(QFile::link(root.filePath(QStringLiteral("loop")), root.filePath(QStringLiteral("loop"))));
        QVERIFY(QFile::link(root.path(), root.filePath(QStringLiteral("self"))));
        QVERIFY(QFile::link(root.filePath(QStringLiteral("b_chef")), root.filePath(QStringLiteral("z_link"))));
        QVERIFY(QFile::link(root.filePath(QStringLiteral("nowhere")), root.filePath(QStringLiteral("dangling"))));
        // A folder whose run.json is a folder, and one whose run.json is empty.
        QVERIFY(root.mkpath(QStringLiteral("c_dir_run/run.json")));
        QVERIFY(root.mkpath(QStringLiteral("d_empty_run")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("d_empty_run/run.json")), {}));
        // A plain file at the top level.
        QVERIFY(writeFile(root.filePath(QStringLiteral("notes.txt")), "x"));

        const QStringList found = findBundles(dir.path());
        QStringList names;
        for (const QString& path : found)
            names.append(QFileInfo(path).fileName());
        // Links to bundle folders count as bundles; anything with a run.json entry is listed.
        QCOMPARE(names, (QStringList{"a_bbb", "b_chef", "c_dir_run", "d_empty_run", "z_link"}));

        QThreadPool pool;
        pool.setMaxThreadCount(4);
        QFuture<QList<BundleSummaryPtr>> future = scanLibrary(dir.path(), &pool);
        future.waitForFinished();
        const QList<BundleSummaryPtr> summaries = future.result();
        QCOMPARE(summaries.size(), 5);
        for (const BundleSummaryPtr& s : summaries) {
            const bool broken = s->directory.endsWith(QLatin1StringView("_run"));
            QVERIFY2(s->errors.isEmpty() != broken,
                     qPrintable(s->directory + ": " + s->errors.join('\n')));
        }
        QVERIFY(findBundles(root.filePath(QStringLiteral("missing"))).isEmpty());
        QVERIFY(findBundles(root.filePath(QStringLiteral("loop"))).isEmpty());
    }

    // --- determinism -------------------------------------------------------------------------

    void loadingTwiceGivesIdenticalData_data()
    {
        QTest::addColumn<QString>("bundle");
        QTest::newRow("chef_cooking_18") << QStringLiteral("chef_cooking_18");
        QTest::newRow("bbb_36") << QStringLiteral("bbb_36");
    }

    void loadingTwiceGivesIdenticalData()
    {
        QFETCH(QString, bundle);
        const QString path = test::dataPath(bundle);
        const QString first = dumpResult(loadBundleSync(path));
        QVERIFY(first.contains(QStringLiteral("seg 0 ")));
        QCOMPARE(dumpResult(loadBundleSync(path)), first);
        for (int threads : {1, 2, 7, 16})
            QCOMPARE(dumpResult(loadAsync(path, threads)), first);
        QCOMPARE(dumpResult(loadAsync(path, 3)), dumpResult(loadAsync(path, 3)));

        const BundleSummary a = readBundleSummary(path);
        const BundleSummary b = readBundleSummary(path);
        QCOMPARE(a.runId, b.runId);
        QCOMPARE(a.stalls, b.stalls);
        QCOMPARE(a.cutOff, b.cutOff);
        QCOMPARE(a.errors, b.errors);
    }
};

QTEST_GUILESS_MAIN(TestCoreEdgeCases)
#include "tst_core_edge_cases.moc"
