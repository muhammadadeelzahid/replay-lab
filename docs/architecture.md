# Architecture

ReplayLab is a desktop application in C++20 and Qt 6. It opens one simulation run bundle (see
[simulation-data.md](simulation-data.md)) and shows the session of one UE: the rung played for
each segment, the video as the user saw it, the buffer, throughput, RTT and loss over time,
the logs, and a QoE report.

Names: repository `replay-lab`; application ReplayLab (`ReplayLab.app` on macOS, `replaylab` elsewhere); CMake target `abrviz`, C++ namespace `abrviz`.

Class and thread diagrams are in [diagrams.md](diagrams.md); the build order is in
[development-plan.md](../archive/development-plan.md).


## Goals

- Qt 6 for the GUI, the event loop, Qt Multimedia and Qt Concurrent.
- GStreamer as the default replay engine, playing the segment files directly at the rungs that
  were played. A Qt Multimedia engine that plays a pre-rendered file is the alternative; a
  build option and a command-line flag choose between them.
- Loading, parsing, log filtering and rendering run on worker threads. The GUI thread only
  paints and handles input.
- Every resource (files, mappings, processes, threads, GStreamer objects) is owned by an object
  that releases it in its destructor.


## Project layout

```
CMakeLists.txt, CMakePresets.json   build; presets debug, release, nogst, asan, tsan
core/        static library, QtCore and QtConcurrent only
  model/     value types and RunData
  io/        MappedFile, JSON Lines reading, bundle file parsers, log parsers, MP4 duration
  load/      segment shift, BundleLoader, BundleLibrary
  playback/  PlaybackClock
  metrics/   QoeMetrics
  logs/      LogIndex, LogFilter
  render/    RenderQueue, RenderCache, SegmentTranscoder
  util/      FutureGuard, ProcessGuard
media/       static library, Qt Multimedia, plus GStreamer when ABR_WITH_GSTREAMER is ON
  ReplaySource, PrerenderedSource, ReplaySourceFactory
  src/gst/   GstSegmentSource, the GStreamer wrappers and GstRuntime (private headers)
app/         Qt Widgets: MainWindow, Session, TimeCursor and the panels
tools/       validate_bundle, print_report, render_experienced
tests/       Qt Test suites, run by CTest
docs/        documentation
```

`core` includes no widget, media or GStreamer header, so the command-line tools and the tests
use the same loading and metrics code as the GUI. GStreamer headers appear only in
`media/src/gst/`.


## Data model

`RunData` holds everything loaded from one bundle:

| Type | Content |
| --- | --- |
| `RunInfo` | run id, link rate, UEs in the simulation, seed, ns-3 node id, log agreement, notes |
| `VideoInfo` | name, title, licence, frame rate, ladder (bitrate and resolution per rung) |
| `Segment` | index, download start and end, played rung, bytes, media file path, media duration |
| `Decision` | the inputs of one ABR decision (buffer, throughput estimate, RTT, loss, decision delay), whether the buffer guard changed the choice, the rung the LLM wanted, the buffer-full wait |
| `Sample` | throughput, RTT and loss at a decision; `std::optional<double>` for missing values |
| `BufferPoint` | buffer level every 33 ms, from the `sim.log` frame lines |
| `SegmentPlayback` | simulation time of the first and last frame of each played segment |
| `Stall` | start, end, whether it never ended |
| `Warning` | log agreement below 1, playback cut off, mismatches found while loading |

`RunData` is built once by the loader and never changed afterwards. It is shared as
`std::shared_ptr<const RunData>`: any thread can read it without a lock, and it is freed when
the last panel or task releases it. Lookups by time use `std::ranges::lower_bound` on sorted
vectors; ranges are returned as `std::span<const T>`.

Pure functions, with no threads and no QObjects:
- `applySegmentShift()` turns the bundle records into played segments and decisions
  ([simulation-data.md](simulation-data.md), "Rungs are shifted by one segment").
- `PlaybackClock` maps between simulation time and replay time (below).
- `computeQoe()` computes the report numbers, both as chosen by the policy (the definitions
  `summary.json` uses, checked against it) and as played.


## Playback clock

The simulated player shows each segment for 3.3 s; the media file for that segment usually
lasts 2.0 s but not always. The replay plays the media at normal speed, so its timeline is
shorter than the simulation's. `PlaybackClock` maps between three timelines:

- simulation time: the times in the bundle and the logs;
- media time: the segments' media played back to back, without stalls (the GStreamer engine's
  timeline);
- replay time: media time with a frozen frame inserted for each stall (the pre-rendered file's
  timeline, and the time shown to the user).

The clock is a list of pieces in playback order. A segment piece lasts from its first frame to
the next piece in simulation time, and for its real media duration in media and replay time.
A stall piece lasts the stall duration in simulation and replay time and zero in media time.
Within a piece the mapping is linear. Before playback starts every time maps to 0; after the
last frame it is clamped to the end. If playback was cut off in the middle of a segment, that
segment's media duration is scaled by the fraction of its frames that were played. A stall
before the first frame of a downloaded segment is start-up and not replayed (the rendered file
has no frame to freeze), and a stall ends at the next piece at the latest in simulation time, so
the pieces never overlap and every mapping is monotonic.


## Threads

Widgets and QObjects with a parent live on the GUI thread. Tasks get immutable inputs and return
new values through `QFuture` or queued calls.

| Work | Thread | Mechanism |
| --- | --- | --- |
| Loading a bundle | global `QThreadPool` | `loadBundle()` reads `run.json`, the video description and the segment records in one task, then starts one task per remaining file, per `sim.log` chunk and per media file (for its duration). An `std::atomic<int>` counts the unfinished tasks; the task that brings it to zero assembles `RunData`, so no thread ever waits for another. A shared `QPromise<LoadResult>` carries progress, cancellation and the result |
| Parsing `sim.log` | global pool | the memory-mapped log is cut into line-aligned chunks, one task each; every task writes its own slot, and the slots are merged in order when the load completes |
| Scanning a folder of bundles | global pool | one task per bundle reads `run.json` and `summary.json`, joined the same way as a load |
| Filtering the log | global pool | one task per request, holding a `std::shared_ptr<const LogIndex>`; it checks its `QPromise` for cancellation. An atomic generation counter discards results of older requests; a 150 ms timer debounces typing |
| Rendering the experienced video | `RenderQueue` thread | a `std::jthread` takes jobs from a queue guarded by `std::mutex` and `std::condition_variable_any`, waiting on its `std::stop_token` |
| Transcoding segments | `RenderQueue`'s own `QThreadPool` | one task per segment runs `ffmpeg` through `QProcess`, polling `waitForFinished(100)` and the job's stop token, so cancelling takes effect within 100 ms |
| Decoding and scaling video | GStreamer's streaming threads | none of our code runs there: they fill the `appsink` queue (a few frames) and block when it is full; the GUI thread takes frames from it |
| GStreamer bus messages | `GstSegmentSource` thread | a `std::jthread` pops bus messages with a 100 ms timeout until stopped, and forwards them as queued calls (Qt's event loop on macOS is not a GLib main loop) |
| GStreamer initialisation | any | `gst_init()` runs once, under `std::call_once` |

State written by one thread and read by another, and how it is protected:

| State | Protection |
| --- | --- |
| `RunData`, `LogIndex` | immutable after construction; shared through `shared_ptr<const T>` |
| state prepared before a load's tasks start | `std::atomic<bool>` stored with release, loaded with acquire by each task |
| results of a load's tasks | each task writes its own member or slot; the `std::atomic<int>` countdown (acq_rel) orders them before assembly |
| `QPromise` progress and result | thread-safe by design (`QFutureInterface`); progress values go through a `std::mutex` so they only increase |
| log filter generation, render progress | `std::atomic` |
| `RenderQueue` job queue | `std::mutex` and `std::condition_variable_any` |
| `RenderCache` (rendered files and jobs in progress) | `std::mutex`; claims released by `ClaimGuard` |

Rules:
1. A task receives shared data by value: the lambda captures its own `shared_ptr`, never a
   reference to a member of a GUI object.
2. A task that can be cancelled takes a `QPromise` and checks `isCanceled()` between units of
   work. (Cancelling the future of a plain `QtConcurrent::run` has no effect on the task.)
3. `Qt::BlockingQueuedConnection` is not used. The GUI thread waits for tasks at shutdown, and
   a task blocking on the GUI thread at that moment would deadlock.
4. A pool task never blocks waiting for another pool task. Groups of tasks are joined with an
   atomic countdown: the last task to finish does the follow-up work.
5. A class that owns a thread stops and joins it at the start of its destructor, before any
   member the thread uses is destroyed.
6. In `GstSegmentSource`, streaming threads run no code of ours; frames cross through the
   `appsink` queue, which is thread-safe. The video sink (a `QPointer`, which is not
   thread-safe) is used only on the GUI thread, and the run can only be replaced while the
   pipeline is stopped.

ThreadSanitizer. Qt is not built with TSan, so TSan cannot see the locks inside `QThreadPool`
and `QFuture`. Our own synchronisation therefore uses standard atomics and mutexes, which TSan
understands, instead of Qt continuations. The TSan preset leaves Qt's header code
uninstrumented (`cmake/tsan-ignorelist.txt`) and suppresses the two Qt hand-offs that remain:
creating a task and reading a future's result (`cmake/tsan.supp`). Our own functions, including
every task body, are fully checked; a race injected into the loader is reported.

Shutdown: `~MainWindow()` first calls `shutdown()`, which cancels and waits for pending loads,
then destroys the replay engine, then lets its private state (and the `RenderQueue`) be
destroyed. This order matters: C++ destroys `MainWindow`'s members before the `QWidget` base
class deletes child widgets, so without `shutdown()` the replay engine would outlive the queue
it points to. No thread is ever detached.


## Ownership and RAII

| Resource | Owner | How it is released |
| --- | --- | --- |
| widgets, models, timers, `QMediaPlayer` | Qt parent | deleted by the parent |
| `MainWindow` private state | `MainWindow` | `std::unique_ptr<Impl>` |
| `RenderQueue` | `MainWindow::Impl` | `std::unique_ptr` |
| replay engine | `ReplayPlayer` | `std::unique_ptr<ReplaySource>` |
| loaded run | panels, tasks, engine | `std::shared_ptr<const RunData>` |
| log index | `Session`, filter tasks | `std::shared_ptr<const LogIndex>` |
| library entries | `LibraryModel` | `QList<QSharedPointer<const BundleSummary>>` |
| widgets used in async callbacks | the callback | `QPointer<T>`, null once the widget is gone |
| memory-mapped file | `MappedFile` | holds a `std::unique_ptr<QFile>` (a `QFile` cannot be moved) and unmaps in its destructor |
| pending task | `FutureGuard<T>` | cancels and waits in its destructor |
| `ffmpeg` process | `ProcessGuard` | kills and waits in its destructor |
| bundle claimed for rendering | `ClaimGuard` | releases the claim unless the job committed a result |
| render scratch files | render job | `QTemporaryDir` |
| rendered video in the cache | render job | written through `QSaveFile`, so a failed job never leaves a partial file |
| GStreamer objects we own | `GstPtr<T>`, `GstCapsPtr`, `GstSamplePtr`, `GstMessagePtr` | `std::unique_ptr` with a deleter calling the matching `*_unref` |
| GStreamer pipeline | `PipelineGuard` | sets the state to `NULL` (stopping the streaming threads), then unrefs |
| mapped video frame | `VideoFrameMapGuard` | `gst_video_frame_unmap` |
| strings allocated by GLib | `std::unique_ptr<gchar, decltype(&g_free)>` | `g_free` |
| locks | the enclosing scope | `std::scoped_lock`, `std::unique_lock` |

GStreamer elements need care: `gst_element_factory_make()` returns a floating reference, which
`gst_bin_add()` takes over. Elements added to the pipeline are kept as plain non-owning
pointers; `GstPtr` only holds references we own (for example from `gst_element_get_bus()` or
after `gst_object_ref_sink()`). Wrapping a floating reference and adding it to a bin would
release it twice.

There is no naked `delete`, and no `new` without a Qt parent or an immediate owner. Raw
pointers never own.

Copy and move rules. Classes not listed follow the rule of zero. The tests check this table
with `static_assert`.

| Class | Copyable | Movable |
| --- | --- | --- |
| `MappedFile`, `FutureGuard<T>`, `ProcessGuard`, `ClaimGuard` | no | yes |
| `GstPtr<T>`, `GstCapsPtr`, `GstSamplePtr`, `GstMessagePtr` | no | yes |
| `PipelineGuard`, `VideoFrameMapGuard` | no | no |
| `RenderQueue`, `GstSegmentSource` | no | no |


## GUI

- `MainWindow`: laid out as a macOS app. A source-list sidebar (`SourceList`) shows All Runs
  and, per video, its runs by link rate (with a note for stalls or a cut-off); selecting a run
  opens it. A toolbar holds the sidebar toggle, Open Folder, a `SegmentedControl` for the views
  of the open run (Replay, Analysis, Logs, Report) and the inspector toggle. The pages are a
  `QStackedWidget`: Library (welcome screen or all runs), Replay (the player), Analysis (charts),
  Logs, Report (report and summary). The `MomentPanel` is the inspector on the right; the
  `TransportBar` is under every view. There is no status bar: loading, errors and the engine
  fallback show in a banner over the content. Menus: File (open folder, open run, open recent,
  close window, settings, quit), View (sidebar, inspector, views, overlay, full screen),
  Playback (play, 5 s and segment steps, render video, engine), Window, Help. Settings (⌘,)
  hold the engine, the overlay and the render cache. Every command is a `QAction` shared by
  the menu, its shortcut and its button. Recent folders, the window geometry, the panes, the
  overlay and the engine are kept with `QSettings`.
- The application is called ReplayLab (`app/branding.h`); the project, its libraries
  and its targets keep the name abrviz. The macOS bundle is `ReplayLab.app`.
- The toolbar is not unified with the title bar: Qt's unified title and toolbar drew the
  window black around custom widgets.
- `Session`: the open run (`shared_ptr<const RunData>`), its `LogIndex`, and the `TimeCursor`.
  Emits `runChanged`; every panel connects to it.
- `TimeCursor`: the current simulation time. `setTime(t, source)`; a panel ignores changes it
  caused itself.
- `LibraryView`: a welcome screen (open a folder or a bundle, recent folders) or the table of
  the bundles in a folder (video, link rate, stalls, cut-off) with a search filter and the
  details of the selected run.
- `SummaryPanel`: run information, ladder, warnings.
- `SegmentTimeline`: painted strip of played rungs, stalls, guard overrides and buffer-full
  waits; clicking moves the cursor.
- `ChartsPanel`: buffer (33 ms), throughput and estimate, bitrate, RTT and loss, decision
  delay; one x-axis and a cursor line.
- `LogConsole`: table over `LogIndex` with text, UE and level filters; follows the cursor.
- `ReplayPlayer`: a `QGraphicsVideoItem` in a scene, an overlay drawn on top of it (segment,
  rung, bitrate, resolution, buffer, state, engine) and the replay engine. Moving the cursor
  elsewhere seeks the engine; while the engine plays, its position moves the cursor. Both go
  through `PlaybackClock`. (A `QVideoWidget` renders into a native child window, which other
  widgets cannot draw over, so the overlay could not be shown on top of it.)
- `ReportView`: QoE numbers, compared with `summary.json`.


## Replay engines

### Choosing the engine

| Level | Setting | Effect |
| --- | --- | --- |
| build | `-DABR_WITH_GSTREAMER=ON` (default) or `OFF` | `ON` builds `media/src/gst/`; configuring fails with a clear message if GStreamer is not found. `OFF` builds only the pre-rendered engine |
| launch | `--engine=gstreamer` or `--engine=prerendered` | overrides the saved choice |
| menu | Playback, Engine (saved with `QSettings`) | the user's choice |
| fallback | automatic | if the GStreamer pipeline cannot be built, the reason is shown in the status bar and the pre-rendered engine is used |

The default is GStreamer when it is built in.

### `ReplaySource`

```cpp
class ReplaySource : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Preparing, Playing, Paused, Stalled, Ended, Error };
    Q_ENUM(State)

    virtual QString name() const = 0;
    virtual void setRun(std::shared_ptr<const RunData> run) = 0;
    virtual void setVideoSink(QVideoSink* sink) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double replayTime) = 0;

signals:
    void positionChanged(double replayTime);
    void stateChanged(abrviz::ReplaySource::State state);
    void errorOccurred(const QString& message);
};
```

Both engines write into the `QVideoSink` of the same `QGraphicsVideoItem`, so the overlay and
the cursor synchronisation are shared. Switching engines destroys the old one before creating the
new one.

### GStreamer engine (`GstSegmentSource`)

```
splitmuxsrc ! decodebin3 ! videoconvert ! videoscale
            ! video/x-raw,format=NV12,width=1920,height=1080 ! appsink sync=false max-buffers=8
```

- `splitmuxsrc` plays a list of files as one stream. Its `format-location` signal returns the
  played segments in order, each at its played rung; seeking across files is handled by
  `splitmuxsrc`. Resolution changes between files are handled by the decoder.
- `decodebin3` uses VideoToolbox (`vtdec`) on macOS and `avdec_h264` on Linux.
- Every rung is scaled to 1080p, so low rungs look as soft as they should.
- Frames are NV12. Each sample is wrapped in a `GstVideoBuffer` (a `QAbstractVideoBuffer`)
  that maps it with `VideoFrameMapGuard` and releases it in its destructor, so frames are
  handed to Qt without copying.
- Timing is done by the engine, not by the sink. The appsink does not sync to a clock; the
  pipeline stays in `PLAYING` and decodes until the appsink's queue of 8 frames is full. While
  playing, a 5 ms timer on the GUI thread advances a replay clock (`QElapsedTimer` plus the
  replay time it started from), maps it to media time through the spans, and shows the newest
  queued frame that is due; late frames are skipped.
- The replay timeline is cut into spans: runs of consecutive segments, and the stalls between
  them. Spans are contiguous in media time, so a stall needs no seek: while the replay clock
  is inside a stall, the media target is the frame before it, the last frame stays on screen,
  and the state is `Stalled`. Playback ends when the replay clock reaches the replay duration,
  which is where a cut-off run stopped (the seek stop is the end of the played media, so
  nothing beyond it is decoded).
- Seeks are flushing and accurate. The first frame after a seek is shown at once, also while
  paused. Seeking into a stall shows the frame just before the stall's media time.
- Why not let the sink sync. The first version synced the appsink to the pipeline clock and
  stopped each span with a seek stop and EOS. When `splitmuxsrc` moved to the next part at a
  rung change, the running time could jump ahead, and the sink then waited for it: the picture
  froze for seconds, sometimes for over a minute, at random places. With the clock on our
  side, a timestamp glitch can at worst show one frame early or late.
- Some decoders fail without posting an error (VideoToolbox refuses very small frames, such as
  64 x 36). If the pipeline has not prerolled 8 s after it starts, the engine reports an
  error. An error before the engine has shown any picture makes the player switch to the
  pre-rendered engine and say why in the status bar.

Member order. Members are destroyed in reverse order of declaration; the bus thread posts to
the engine and the pipeline owns the appsink:

```cpp
class GstSegmentSource final : public ReplaySource {
    // ...
private:
    std::shared_ptr<const RunData> run_;   // replaced only while the pipeline is stopped
    std::vector<Span> spans_;
    QPointer<QVideoSink> sink_;            // GUI thread only
    QTimer* ticker_;                       // presents frames while playing
    QElapsedTimer playClock_;              // replay time = anchor_ + elapsed
    std::optional<Pending> pending_;       // the next queued frame, not due yet
    GstElement* appsink_ = nullptr;        // owned by the pipeline
    PipelineGuard pipeline_;               // stops the streaming threads, then unrefs
    std::jthread busThread_;               // destroyed first
};
```

The destructor also does this explicitly: stop and join the bus thread, then stop the
pipeline. The bus thread holds its own reference to the bus and never reads `pipeline_`; only
plain values (message kind and text), not `GstMessage`s, are posted to the GUI thread. Calls
still queued are dropped by Qt because they are posted with the engine as the context object.

### Pre-rendered engine (`PrerenderedSource`)

Plays `experienced.mp4` through `QMediaPlayer`. The file is built by the `RenderQueue`: each
played segment is transcoded to 1080p at the video's frame rate, the segment before a stall
is extended with its last frame for the stall duration (`tpad`), a cut-off segment is trimmed,
and the parts are joined with the concat demuxer. Qt Multimedia uses AVFoundation on macOS and
its FFmpeg backend on Linux.

The render is not started by opening a run: it uses every core for several seconds (12 s for
Big Buck Bunny at 36 Mbps on 10 cores), so it is started from Playback, Render Video. Finished
files stay in the cache (`QStandardPaths::CacheLocation`/`renders`, keyed by the run id and a
hash of the render plan) and are used at once the next time.

AVFoundation shows no picture for a seek while paused. After such a seek the engine plays until
the first frame arrives (at most 500 ms) and pauses again, keeping its own state `Paused`, so
the cursor does not follow these internal plays.


## Errors

- Parsers return their result together with a list of errors (file, line, message). The loader
  collects them into `LoadResult::errors`, which the GUI shows.
- Exceptions do not cross Qt or GStreamer. Callbacks called from C and Qt slots are
  `noexcept`.
- Broken invariants are checked with `Q_ASSERT`.
- Types sent through queued connections are registered: `Q_ENUM` for enums,
  `Q_DECLARE_METATYPE` for the others.
