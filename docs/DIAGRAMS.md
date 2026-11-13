# abr-viz: diagrams

Class and threading diagrams for [ARCHITECTURE.md](ARCHITECTURE.md). They are Mermaid
diagrams: GitHub and VS Code's Markdown preview render them.

Notation in the class diagrams:
- `*--` owns (Qt parent, `std::unique_ptr` or plain member);
- `o--` shares (`std::shared_ptr` / `QSharedPointer`);
- `-->` uses or observes (holds a pointer or connects to its signals);
- `..>` calls or creates;
- `$` = static; `dtor` = what the destructor does (RAII).

C++ templates are written with `~`: `vector~Segment~` is `std::vector<Segment>`.


## 1. Core: data, loading, logs, rendering (`core/`)

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
    +vector~Stall~ stalls
    +vector~Warning~ warnings
    +segmentAt(double simT) const Segment*
    +samplesIn(double t0, double t1) span~Sample~
  }
  class Segment {
    +int index
    +double requestS
    +double doneS
    +int playedRung
    +qint64 bytes
    +QString mediaPath
  }
  class Decision {
    +int record
    +double t
    +double bufferS
    +double thrEstBps
    +optional~double~ rttMs
    +optional~double~ loss
    +bool fallback
    +double delayS
    +QString reason
    +double delS
  }
  class Sample {
    +double t
    +optional~double~ thrBps
    +optional~double~ rttMs
    +optional~double~ loss
  }
  class BufferPoint {
    +double t
    +double bufferS
  }
  class Stall {
    +double startS
    +double endS
    +bool terminal
  }
  RunData *-- Segment
  RunData *-- Decision
  RunData *-- Sample
  RunData *-- BufferPoint
  RunData *-- Stall

  class BundleLoader {
    +load(QString dir) QFuture~LoadResult~
    -startParsers(QString dir)$ QList~QFuture~
    -assemble(Parts parts)$ LoadResult
  }
  class LoadResult {
    +shared_ptr~RunData~ run
    +QStringList errors
  }
  class SimLogParser {
    +parseChunk(QByteArrayView chunk, int nodeId)$ SimLogPart
    +reduce(SimLogPart acc, SimLogPart part)$
  }
  class JsonlReader {
    +forEachRecord(QByteArrayView text, Fn fn)$
  }
  class PolicyLogParser {
    +parse(QByteArrayView text, int nodeId)$ vector~Reason~
  }
  class SegmentShift {
    <<utility>>
    +applySegmentShift(RawSegments raw, VideoInfo video)$ vector~Segment~
  }
  class MappedFile {
    -unique_ptr~QFile~ file
    -uchar* data
    -qint64 size
    +view() QByteArrayView
    +MappedFile(MappedFile&& other)
    +operator=(MappedFile&& other) unmaps first
    +dtor() unmaps the file
  }
  BundleLoader ..> SimLogParser
  BundleLoader ..> JsonlReader
  BundleLoader ..> PolicyLogParser
  BundleLoader ..> SegmentShift
  BundleLoader ..> MappedFile
  BundleLoader ..> LoadResult : returns
  LoadResult o-- RunData : shared_ptr const

  class PlaybackClock {
    -vector~Slot~ slots
    +simToVideo(double simT) double
    +videoToSim(double videoT) double
    +positionAt(double simT) SegmentPos
    +isStalled(double simT) bool
  }
  class QoeMetrics {
    +compute(RunData run)$ QoeReport
  }
  PlaybackClock --> RunData
  QoeMetrics ..> RunData

  class LogIndex {
    -MappedFile file
    -vector~LineInfo~ lines
    +lineCount() int
    +line(int i) QByteArrayView
    +firstLineAt(double t) int
  }
  class LogFilter {
    -shared_ptr~LogIndex~ index
    -atomic~uint64_t~ generation
    -FutureGuard~Rows~ pending
    -QTimer debounce
    +request(FilterSpec spec)
    +resultsReady(vector~int~ rows) signal
  }
  LogIndex *-- MappedFile
  LogFilter o-- LogIndex : shared_ptr const

  class RenderQueue {
    -RenderCache cache
    -QThreadPool pool
    -mutex m
    -condition_variable_any cv
    -deque~RenderJob~ jobs
    -jthread worker
    +enqueue(RenderJob job) QFuture~QString~
    -workerLoop(stop_token st)
    +dtor() stop and join worker first
  }
  class RenderCache {
    -mutex m
    -QHash~QString_QString~ done
    -QSet~QString~ inFlight
    +lookup(QString bundle) optional~QString~
    +tryClaim(QString bundle) bool
    +finish(QString bundle, QString mp4)
  }
  class SegmentTranscoder {
    +transcode(QString src, QString dst, stop_token st)$ bool
  }
  class ClaimGuard {
    -RenderCache* cache
    -QString bundle
    -bool committed
    +commit(QString mp4)
    +dtor() releases claim unless committed
  }
  class ProcessGuard {
    -unique_ptr~QProcess~ process
    +dtor() kill and wait
  }
  class FutureGuard~T~ {
    -QFuture~T~ future
    +dtor() cancel and wait
  }
  RenderQueue *-- RenderCache
  RenderQueue ..> SegmentTranscoder
  RenderCache ..> ClaimGuard : tryClaim returns
  SegmentTranscoder *-- ProcessGuard
```


## 2. Media: replay engines and GStreamer RAII (`media/`)

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
    +seek(double videoTime)
    +positionChanged(double videoTime) signal
    +stateChanged(State state) signal
    +errorOccurred(QString message) signal
  }
  class PrerenderedSource {
    -QMediaPlayer* player
    -RenderQueue* queue
    -QPointer~QVideoSink~ sink
  }
  class GstSegmentSource {
    -shared_ptr~RunData~ run
    -QPointer~QVideoSink~ sink
    -GstElement* concat
    -GstElement* appsink
    -mutex frameMutex
    -QVideoFrame latestFrame
    -atomic~bool~ framePending
    -PipelineGuard pipeline
    -jthread busThread
    +dtor() join bus, stop pipeline, then members
    -addBranch(int segment)
    -onNewSample(GstAppSink* sink, void* self)$ GstFlowReturn
    -deliverFrame()
    -busLoop(stop_token st)
  }
  class ReplaySourceFactory {
    +available()$ QList~Engine~
    +create(Engine e, RenderQueue* q, QObject* parent)$ unique_ptr~ReplaySource~
  }
  class GstRuntime {
    -once_flag once$
    +ensureInit()$
  }
  class GstPtr~T~ {
    unique_ptr with GstUnref deleter
    +dtor() gst_object_unref
  }
  class PipelineGuard {
    -GstElement* pipeline
    +dtor() set state NULL, wait, unref
  }
  class BufferMapGuard {
    -GstBuffer* buffer
    -GstMapInfo info
    +data() span~uchar~
    +dtor() gst_buffer_unmap
  }
  class GstSamplePtr {
    unique_ptr with gst_sample_unref
  }
  class GstVideoBuffer {
    -GstSamplePtr sample
    -BufferMapGuard map
    +map(QVideoFrame::MapMode mode) MapData
    +dtor() unmap, release sample
  }
  ReplaySource <|-- PrerenderedSource
  ReplaySource <|-- GstSegmentSource
  ReplaySourceFactory ..> ReplaySource : creates
  ReplaySourceFactory ..> GstRuntime
  GstSegmentSource *-- PipelineGuard
  GstSegmentSource ..> GstVideoBuffer : creates per frame
  GstVideoBuffer *-- GstSamplePtr
  GstVideoBuffer *-- BufferMapGuard
  PipelineGuard ..> GstPtr : bus via
```

Members of `GstSegmentSource` are listed in declaration order. They are destroyed in reverse,
and the destructor also does it explicitly: the bus thread is stopped and joined, then
`PipelineGuard` sets the pipeline to `NULL` (no streaming thread runs after that), then the
frame slot and the rest are destroyed (design fix D1). `concat` and `appsink` are non-owning:
the pipeline owns them (D3). `GstVideoBuffer` hands each frame to Qt without copying it (D16).


## 3. App: GUI (`app/`)

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
    -ReplayPlayer* player
  }
  class Session {
    -shared_ptr~RunData~ run
    -shared_ptr~LogIndex~ logs
    -TimeCursor* cursor
    +open(QString dir)
    +run() shared_ptr~RunData~
    +runChanged() signal
  }
  class TimeCursor {
    -double t
    +setTime(double simT, Source from)
    +timeChanged(double simT, Source from) signal
  }
  class LibraryView {
    -LibraryModel* model
  }
  class LibraryModel {
    -QList~BundleSummaryPtr~ rows
  }
  class SummaryPanel
  class SegmentTimeline {
    #paintEvent(QPaintEvent* e)
    #mousePressEvent(QMouseEvent* e)
  }
  class ChartsPanel {
    -QList~QChartView*~ views
    -updateCursorLine(double simT)
  }
  class LogConsole {
    -LogTableModel* model
    -LogFilter* filter
  }
  class ReplayPlayer {
    -QVideoWidget* video
    -Overlay* overlay
    -unique_ptr~ReplaySource~ engine
    +setEngine(Engine e)
    +releaseEngine()
  }
  class ReportView

  MainWindow *-- Impl
  Impl ..> RenderQueue : owns, outlives player
  MainWindow *-- Session
  MainWindow *-- LibraryView
  MainWindow *-- SummaryPanel
  MainWindow *-- SegmentTimeline
  MainWindow *-- ChartsPanel
  MainWindow *-- LogConsole
  MainWindow *-- ReplayPlayer
  MainWindow *-- ReportView
  Session *-- TimeCursor
  LibraryView *-- LibraryModel
  SummaryPanel --> Session
  SegmentTimeline --> Session
  ChartsPanel --> Session
  LogConsole --> Session
  ReplayPlayer --> Session
  ReportView --> Session
```

`~MainWindow()` starts with `shutdown()`: it waits for pending futures and destroys the replay
engine **before** `Impl` (and the `RenderQueue`) is destroyed. Member order alone is not
enough here, because C++ destroys `MainWindow`'s members before the `QWidget` base deletes
child widgets such as `ReplayPlayer` (D11). `BundleSummaryPtr` is `QSharedPointer<const BundleSummary>`. Every `shared_ptr~RunData~` is
`std::shared_ptr<const RunData>`. Panels get the run from `Session::runChanged` and keep their
own `shared_ptr`.


## 4. Threads, data and calls

Each box is a thread (or a pool of threads); each arrow is a call or a hand-off, labelled with
the function and the data that crosses.

```mermaid
flowchart LR
  subgraph GUI["GUI thread: Qt event loop"]
    MW["MainWindow / Session"]
    CUR["TimeCursor"]
    PANELS["Timeline / Charts / LogConsole / Report"]
    RP["ReplayPlayer + QVideoSink"]
  end

  subgraph POOL["QThreadPool::globalInstance"]
    LOAD["BundleLoader::load<br/>starts child tasks, does not wait"]
    PARSE["child tasks:<br/>segments / samples / events /<br/>video metadata / policy.log"]
    SIMLOG["QtConcurrent::mappedReduced<br/>SimLogParser::parseChunk / reduce"]
    SCAN["BundleLibrary::scan<br/>QtConcurrent::mapped"]
    FILT["LogFilter task<br/>shared_ptr#lt;const LogIndex#gt; by value<br/>checks QPromise::isCanceled"]
    JOIN[".then(): BundleLoader::assemble<br/>builds RunData"]
  end

  subgraph RQ["RenderQueue: std::jthread"]
    WL["workerLoop(stop_token)<br/>waits on condition_variable_any"]
  end

  subgraph RPOOL["render QThreadPool: N-1 threads"]
    FF["SegmentTranscoder::transcode<br/>ffmpeg via QProcess + ProcessGuard<br/>waitForFinished(100) + stop_token"]
  end

  subgraph GST["GStreamer streaming threads"]
    PIPE["filesrc → qtdemux → decodebin3<br/>→ concat → videoscale → appsink"]
    CB["onNewSample (noexcept)<br/>GstSamplePtr → GstVideoBuffer, no copy<br/>→ QVideoFrame into frame slot (mutex)"]
  end

  subgraph BUS["GstSegmentSource bus: std::jthread"]
    BL["busLoop(stop_token)<br/>gst_bus_timed_pop, 100 ms"]
  end

  MW -- "QtConcurrent::run(dir)" --> LOAD
  LOAD --> PARSE
  LOAD --> SIMLOG
  PARSE -- "QtFuture::whenAll" --> JOIN
  SIMLOG -- "QtFuture::whenAll" --> JOIN
  JOIN -- "QFutureWatcher::finished<br/>shared_ptr#lt;const RunData#gt;" --> MW
  MW -- "scan(dir)" --> SCAN
  SCAN -- "QList#lt;QSharedPointer#lt;BundleSummary#gt;#gt;" --> MW
  MW -- "runChanged(shared_ptr)" --> PANELS
  PANELS -- "request(FilterSpec)" --> FILT
  FILT -- "rows, dropped if stale" --> PANELS
  PANELS <-- "setTime / timeChanged" --> CUR
  RP <-- "PlaybackClock: sim ↔ video time" --> CUR
  RP -- "enqueue(RenderJob)<br/>pre-rendered engine" --> WL
  WL -- "one task per segment" --> FF
  WL -- "invokeMethod, queued:<br/>progress, mp4 path" --> RP
  RP -- "play / pause / seek<br/>GStreamer engine" --> PIPE
  PIPE --> CB
  CB -- "invokeMethod, queued, if !framePending:<br/>QVideoSink::setVideoFrame" --> RP
  BL -- "invokeMethod, queued:<br/>EOS, error, state" --> RP
```

What is shared between threads, and how it is protected:

| Data | Shared by | Protection |
| --- | --- | --- |
| `shared_ptr<const RunData>` | GUI, loader, filters, GStreamer engine | Immutable after loading: no lock |
| Filter generation counter | GUI, filter tasks | `std::atomic<uint64_t>` |
| `RenderCache` | GUI, `RenderQueue` worker | `std::mutex` + `std::scoped_lock`; claims released by `ClaimGuard` |
| `shared_ptr<const LogIndex>` | `Session`, filter tasks | Immutable after indexing: no lock; tasks hold copies |
| `RenderQueue` job queue | GUI (producer), worker (consumer) | `std::mutex` + `std::condition_variable_any` |
| GStreamer frame slot | streaming thread (writes), GUI (reads) | `std::mutex`, plus `std::atomic<bool> framePending` |
| Progress counters | workers (write), GUI (read) | `std::atomic<int>` |
