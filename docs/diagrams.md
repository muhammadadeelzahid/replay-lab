# Diagrams

Class and thread diagrams for [architecture.md](architecture.md), in Mermaid (rendered by
GitHub and most Markdown previews).

Class diagram notation:
- `*--` owns (Qt parent, `std::unique_ptr` or member);
- `o--` shares (`std::shared_ptr` or `QSharedPointer`);
- `-->` uses or observes;
- `..>` calls or creates;
- `$` static; `dtor` what the destructor does.

Templates are written with `~`: `vector~Segment~` is `std::vector<Segment>`.


## Core

```mermaid
classDiagram
  direction LR

  class RunData {
    +RunInfo info
    +VideoInfo video
    +vector~Segment~ segments
    +vector~Decision~ decisions
    +vector~Sample~ samples
    +vector~BufferPoint~ buffer
    +vector~SegmentPlayback~ playback
    +vector~Stall~ stalls
    +vector~Warning~ warnings
    +segmentAt(double simTime) const Segment*
    +samplesBetween(double t0, double t1) span~Sample~
  }
  class Segment {
    +int index
    +double requestTime
    +double doneTime
    +int playedRung
    +qint64 bytes
    +QString mediaPath
    +double mediaDuration
  }
  class Decision {
    +int record
    +double time
    +double bufferLevel
    +double throughputEstimate
    +optional~double~ rttMs
    +optional~double~ lossRate
    +double decisionDelay
    +bool guardOverride
    +int llmRung
    +double bufferFullWait
  }
  class Stall {
    +double start
    +double end
    +bool terminal
  }
  RunData *-- Segment
  RunData *-- Decision
  RunData *-- Stall

  class BundleLoader {
    +loadBundle(QString dir, QThreadPool* pool)$ QFuture~LoadResult~
  }
  class LoadState {
    +QPromise~LoadResult~ promise
    +BundleParts parts
    +vector~SimLogPart~ logParts
    +atomic~bool~ ready
    +atomic~int~ remaining
    +mutex progressMutex
  }
  BundleLoader ..> LoadState : one per load, shared_ptr
  class LoadResult {
    +shared_ptr~RunData~ run
    +QStringList errors
  }
  class SimLogParser {
    +parseChunk(QByteArrayView chunk, int nodeId)$ SimLogPart
    +merge(SimLogPart acc, SimLogPart part)$
  }
  class BundleFiles {
    <<utility>>
    +parseRunJson(QByteArray)$ Parsed~RunInfo~
    +parseSegments(QByteArrayView)$ Parsed~Records~
    +parseSamples(QByteArrayView)$ Parsed~Samples~
    +parseEvents(QByteArrayView)$ Parsed~Events~
    +parseVideo(QString dir)$ Parsed~VideoInfo~
  }
  class PolicyLogParser {
    +parse(QByteArrayView text, int nodeId)$ vector~PolicyLine~
  }
  class Mp4Duration {
    <<utility>>
    +readMp4Duration(QString path)$ optional~double~
  }
  class SegmentShift {
    <<utility>>
    +applySegmentShift(Records records, VideoInfo video)$ ShiftResult
  }
  class MappedFile {
    -unique_ptr~QFile~ file
    -uchar* data
    -qint64 size
    +open(QString path)$ MappedFile
    +view() QByteArrayView
    +MappedFile(MappedFile&& other)
    +dtor() unmaps the file
  }
  BundleLoader ..> BundleFiles
  BundleLoader ..> SimLogParser
  BundleLoader ..> PolicyLogParser
  BundleLoader ..> Mp4Duration
  BundleLoader ..> SegmentShift
  BundleLoader ..> MappedFile
  BundleLoader ..> LoadResult : returns
  LoadResult o-- RunData : shared_ptr const

  class PlaybackClock {
    -vector~Piece~ pieces
    +simToReplay(double simTime) double
    +replayToSim(double replayTime) double
    +replayToMedia(double replayTime) double
    +mediaToReplay(double mediaTime) double
    +positionAt(double simTime) Position
  }
  class QoeMetrics {
    <<utility>>
    +computeQoe(RunData run)$ QoeReport
  }
  PlaybackClock --> RunData
  QoeMetrics ..> RunData

  class LogIndex {
    -MappedFile file
    -vector~LogLine~ lines
    +build(MappedFile file, int nodeId)$ shared_ptr~LogIndex~
    +lineCount() int
    +text(int row) QByteArrayView
    +firstLineAt(double time) int
  }
  class LogFilter {
    -atomic~uint64_t~ generation
    -FutureGuard~Rows~ pending
    -QTimer debounce
    +setIndex(shared_ptr~LogIndex~ index)
    +request(FilterSpec spec)
    +resultsReady(QList~int~ rows) signal
  }
  LogIndex *-- MappedFile
  LogFilter o-- LogIndex : shared_ptr const

  class RenderQueue {
    -RenderCache cache
    -QThreadPool pool
    -mutex mutex
    -condition_variable_any wake
    -deque~RenderJob~ jobs
    -jthread worker
    +enqueue(RenderRequest request) QFuture~RenderResult~
    -workerLoop(stop_token stop)
    +dtor() stop and join the worker first
  }
  class RenderCache {
    -mutex mutex
    -QHash~QString_QString~ done
    -QSet~QString~ inProgress
    +lookup(QString key) optional~QString~
    +tryClaim(QString key) optional~ClaimGuard~
  }
  class ClaimGuard {
    -RenderCache* cache
    -QString key
    -bool committed
    +commit(QString path)
    +dtor() releases the claim unless committed
  }
  class SegmentTranscoder {
    +transcode(TranscodeJob job, stop_token stop)$ bool
  }
  class ProcessGuard {
    -unique_ptr~QProcess~ process
    +dtor() kills and waits
  }
  class FutureGuard~T~ {
    -QFuture~T~ future
    +dtor() cancels and waits
  }
  RenderQueue *-- RenderCache
  RenderQueue ..> SegmentTranscoder
  RenderCache ..> ClaimGuard : tryClaim returns
  SegmentTranscoder *-- ProcessGuard
```


## Media

```mermaid
classDiagram
  direction LR

  class ReplaySource {
    <<abstract>>
    +name() QString
    +setRun(shared_ptr~RunData~ run)
    +setVideoSink(QVideoSink* sink)
    +play()
    +pause()
    +seek(double replayTime)
    +positionChanged(double replayTime) signal
    +stateChanged(State state) signal
    +errorOccurred(QString message) signal
  }
  class PrerenderedSource {
    -QMediaPlayer* player
    -RenderQueue* queue
    -FutureGuard~RenderResult~ render
  }
  class GstSegmentSource {
    -shared_ptr~RunData~ run
    -QPointer~QVideoSink~ sink
    -GstElement* appsink
    -mutex frameMutex
    -QVideoFrame latestFrame
    -atomic~bool~ framePending
    -PipelineGuard pipeline
    -jthread busThread
    -onNewSample(GstAppSink* sink, void* self)$ GstFlowReturn
    -onFormatLocation(GstElement* src, guint index, void* self)$ gchar*
    -deliverFrame()
    -busLoop(stop_token stop)
    +dtor() join bus thread, stop pipeline
  }
  class ReplaySourceFactory {
    +available()$ QList~Engine~
    +create(Engine engine, RenderQueue* queue)$ unique_ptr~ReplaySource~
  }
  class GstRuntime {
    +ensureInitialized()$
  }
  class GstPtr~T~ {
    unique_ptr with gst_object_unref
  }
  class PipelineGuard {
    -GstElement* pipeline
    +stop()
    +dtor() state NULL, then unref
  }
  class GstVideoBuffer {
    -GstSamplePtr sample
    -VideoFrameMapGuard frame
    +map(MapMode mode) MapData
    +format() QVideoFrameFormat
    +dtor() unmaps, releases the sample
  }
  class VideoFrameMapGuard {
    -GstVideoFrame frame
    +dtor() gst_video_frame_unmap
  }
  ReplaySource <|-- PrerenderedSource
  ReplaySource <|-- GstSegmentSource
  ReplaySourceFactory ..> ReplaySource : creates
  ReplaySourceFactory ..> GstRuntime
  GstSegmentSource *-- PipelineGuard
  GstSegmentSource ..> GstVideoBuffer : one per frame
  GstVideoBuffer *-- VideoFrameMapGuard
  PipelineGuard ..> GstPtr : bus
```

`GstSegmentSource` members are listed in declaration order and destroyed in reverse: the bus
thread is joined, the pipeline is stopped (no streaming thread runs after that), then the frame
slot is destroyed. `appsink` is owned by the pipeline.


## GUI

```mermaid
classDiagram
  direction LR

  class MainWindow {
    -unique_ptr~Impl~ d
    +openBundle(QString dir)
    +openFolder(QString dir)
    -shutdown()
    +dtor() shutdown() first
  }
  class Impl {
    -unique_ptr~RenderQueue~ renderQueue
    -FutureGuard~LoadResult~ loading
  }
  class Session {
    -shared_ptr~RunData~ run
    -shared_ptr~LogIndex~ logs
    -TimeCursor* cursor
    +setRun(shared_ptr~RunData~ run)
    +runChanged() signal
  }
  class TimeCursor {
    -double time
    +setTime(double simTime, Source source)
    +timeChanged(double simTime, Source source) signal
  }
  class LibraryView
  class SummaryPanel
  class SegmentTimeline
  class ChartsPanel
  class LogConsole
  class ReplayPlayer {
    -QGraphicsVideoItem* video
    -unique_ptr~ReplaySource~ engine
    +setEngine(Engine engine)
    +releaseEngine()
  }
  class ReportView

  MainWindow *-- Impl
  MainWindow *-- Session
  MainWindow *-- LibraryView
  MainWindow *-- SummaryPanel
  MainWindow *-- SegmentTimeline
  MainWindow *-- ChartsPanel
  MainWindow *-- LogConsole
  MainWindow *-- ReplayPlayer
  MainWindow *-- ReportView
  Session *-- TimeCursor
  SummaryPanel --> Session
  SegmentTimeline --> Session
  ChartsPanel --> Session
  LogConsole --> Session
  ReplayPlayer --> Session
  ReportView --> Session
```

`~MainWindow()` calls `shutdown()` before anything is destroyed: pending loads are cancelled and
awaited, and the replay engine is destroyed before `Impl` destroys the `RenderQueue` it points
to.


## Threads

Each box is a thread or a pool; each arrow is a call or a hand-off, labelled with the function
and the data that crosses.

```mermaid
flowchart LR
  subgraph GUI["GUI thread"]
    MW["MainWindow / Session"]
    CUR["TimeCursor"]
    PANELS["timeline, charts, logs, report"]
    RP["ReplayPlayer + QVideoSink"]
  end

  subgraph POOL["global QThreadPool"]
    FILES["file parsers<br/>run, segments, samples, events,<br/>video metadata, policy.log"]
    MP4["MP4 durations<br/>one task per played segment"]
    SIMLOG["sim.log chunks<br/>one task per chunk"]
    JOIN["last task to finish<br/>(atomic countdown)<br/>assembles RunData"]
    SCAN["library scan<br/>one task per bundle"]
    FILT["log filter task<br/>shared_ptr#lt;const LogIndex#gt;"]
  end

  subgraph RQ["RenderQueue: std::jthread"]
    WL["workerLoop(stop_token)"]
  end

  subgraph RPOOL["render QThreadPool"]
    FF["SegmentTranscoder: ffmpeg"]
  end

  subgraph GST["GStreamer streaming threads"]
    PIPE["splitmuxsrc → decodebin3 → scale → appsink"]
    CB["onNewSample: wrap frame, store in slot"]
  end

  subgraph BUS["GstSegmentSource: std::jthread"]
    BL["busLoop(stop_token)"]
  end

  MW -- "loadBundle(dir)" --> FILES
  MW -- "loadBundle(dir)" --> MP4
  MW -- "loadBundle(dir)" --> SIMLOG
  FILES --> JOIN
  MP4 --> JOIN
  SIMLOG --> JOIN
  JOIN -- "QFuture#lt;LoadResult#gt;" --> MW
  MW -- "scanLibrary(dir)" --> SCAN
  SCAN -- "QList#lt;BundleSummary#gt;" --> MW
  MW -- "runChanged" --> PANELS
  PANELS -- "request(FilterSpec)" --> FILT
  FILT -- "rows, dropped if outdated" --> PANELS
  PANELS <-- "setTime / timeChanged" --> CUR
  RP <-- "PlaybackClock" --> CUR
  RP -- "enqueue(RenderRequest)" --> WL
  WL -- "one task per segment" --> FF
  WL -- "queued: progress, file path" --> RP
  RP -- "play / pause / seek" --> PIPE
  PIPE --> CB
  CB -- "queued: deliverFrame" --> RP
  BL -- "queued: end of stream, errors" --> RP
```

| Shared between threads | Protection |
| --- | --- |
| `shared_ptr<const RunData>`, `shared_ptr<const LogIndex>` | immutable, no lock |
| load setup and task results | `std::atomic` release/acquire and an acq_rel countdown |
| load progress and result | `QPromise` |
| log filter generation, render progress | `std::atomic` |
| render job queue | `std::mutex`, `std::condition_variable_any` |
| `RenderCache` | `std::mutex`, `ClaimGuard` |
| GStreamer frame slot | `std::mutex`, `std::atomic<bool>` |
