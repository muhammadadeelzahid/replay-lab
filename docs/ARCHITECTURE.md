# abr-viz: architecture

**abr-viz** (repository `github.com/muhammadadeelzahid/abr-viz`, executable `abrviz`, C++
namespace `abrviz`) is a desktop application in **C++20 and Qt 6** that opens the run bundles described in
[bundle_info.md](bundle_info.md) and shows one LLM ABR session: the rung played per segment,
the experienced video, buffer/throughput/RTT/loss charts, the logs and a QoE report.

The design deliberately uses these where each does real work:
- **Qt 6** for the GUI, the event loop, Qt Multimedia and Qt Concurrent;
- **GStreamer** as the main replay engine, playing the segment files directly and switching
  quality live; a Qt Multimedia engine playing a pre-rendered file is the alternative, and a
  flag chooses between them (section 7);
- **C++ multithreading**, both Qt's (`QtConcurrent`, `QPromise`/`QFuture`, `QThreadPool`) and
  the standard library's (`std::jthread`, `std::stop_token`, `std::mutex`,
  `std::condition_variable_any`, `std::atomic`, `std::call_once`);
- **RAII and smart pointers**, both standard (`std::unique_ptr` with custom deleters,
  `std::shared_ptr`) and Qt's (`QSharedPointer`, `QPointer`, parent-child ownership,
  `QMutexLocker`, `QSignalBlocker`, `QTemporaryDir`, `QSaveFile`), plus small guards of our own.

Class and threading diagrams: [DIAGRAMS.md](DIAGRAMS.md).

The data quirks the code must handle are in [bundle_info.md](bundle_info.md): rungs shifted by
one segment (section 2.5), 3.3 s simulated vs 2.0 s media segments, the buffer every 33 ms from
`sim.log` frame lines, the `PAUSED` line at the end of the video, BBB cut off at segment 54,
and the bundle UE's `node_id` (5, 6 or 7).


## 1. Environment

Checked on the development Mac: Qt 6.9.2 (Homebrew) with Widgets, Charts, Multimedia,
MultimediaWidgets, Concurrent and Test; CMake; Apple clang 21 (C++20, `std::jthread`
available). To install:
- `brew install gstreamer` (GStreamer engine; includes the base, good, bad and libav plugins);
- `brew install ffmpeg` (pre-rendered engine and `render_experienced`);
- optionally `brew install ninja`.

Linux CI: `libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good
gstreamer1.0-plugins-bad gstreamer1.0-libav ffmpeg`.


## 2. Project layout

```
CMakeLists.txt            C++20, warnings as errors, presets debug / release / asan / tsan
                          option ABR_WITH_GSTREAMER (default ON)
core/                     static library, QtCore + QtConcurrent only (no widgets, no media)
  model/                  plain value types and RunData
  io/                     MappedFile, JsonlReader, file parsers
  load/                   BundleLoader, BundleLibrary
  logs/                   LogIndex, LogFilter
  playback/               PlaybackClock
  metrics/                QoeMetrics
  render/                 RenderQueue, RenderCache, SegmentTranscoder (ffmpeg)
  util/                   FutureGuard, ProcessGuard
media/                    static library, QtMultimedia (+ GStreamer when enabled)
  ReplaySource            the engine interface
  PrerenderedSource       Qt Multimedia engine
  ReplaySourceFactory     picks the engine
  gst/                    GstSegmentSource and the GStreamer RAII wrappers (only if enabled)
app/                      Qt Widgets executable: MainWindow and panels
tools/                    validate_bundle, print_report, render_experienced (use core/)
tests/                    Qt Test: unit, golden bundles, threading, pipeline, offscreen GUI
docs/                     this folder
```

`core/` never includes a widget or media header, so the command-line tools and the tests use
exactly the same loading and metrics code as the GUI. GStreamer headers appear only under
`media/gst/`.


## 3. Data model (`core/model`)

- Plain structs:
  - `RunInfo`: run id, link rate, simulated UEs, seed, `node_id`, log agreement, notes.
  - `VideoInfo`: ladder, resolution per rung, segment count, file pattern, licence.
  - `Segment`: index, request and done times, **played rung** (after the shift fix), bytes
    from the video metadata, path of the media file at the played rung.
  - `Decision`: the inputs of bundle record *n* (buffer, throughput estimate, RTT, loss,
    fallback, decision delay), `reason` from `policy.log`, `del` from `sim.log`.
  - `Sample`: throughput, RTT, loss, with `std::optional<double>` for `null`.
  - `BufferPoint`: time and buffer level, every 33 ms from the frame lines.
  - `Stall`: start, end, terminal flag.
  - `Warning`: log agreement < 1, mismatch with `summary.json`, playback cut off.
- `RunData`: all of the above as sorted `std::vector`s. It is built once by the loader and then
  **immutable**. It is shared as `std::shared_ptr<const RunData>`, so any thread (loader,
  filters, GStreamer engine) can read it without locks, and it is freed when the last panel or
  worker lets go of it. Time lookups use `std::ranges::lower_bound`; ranges are handed out as
  `std::span<const T>`.
- Pure functions, with no threads and no Qt objects, so they are easy to test:
  - `applySegmentShift()`: segment 0 at rung 0, segment *k* at the rung of record *k - 1*,
    bytes looked up again, the last record kept as an unused decision.
  - `PlaybackClock`: simulation time ↔ (segment, offset) ↔ video time, including stalls and
    the cut-off at simulation stop. Both engines use it.
  - `QoeMetrics::compute()`: average bitrate, switches, rebuffer time, stalls, startup delay,
    fallback rate, decision delay, segment reached at stop.


## 4. Multithreading

**Rule:** widgets, and QObjects that have a parent, live on the GUI thread only. Workers get
immutable inputs (`shared_ptr<const …>` or plain values) and return new values through
`QFuture` or queued calls. The only mutable state shared between threads is listed at the
end of the table, and each item has its own lock.

| Where | Mechanism | Why |
| --- | --- | --- |
| **Bundle loading** | `QtConcurrent::run` with a `QPromise<LoadResult>`; the GUI watches a `QFutureWatcher` | Progress (`setProgressValue`), cancellation (`isCanceled()`), result delivered on the GUI thread |
| Parsing the bundle files | Child tasks (`QtConcurrent::run`) for `segments.jsonl`, `samples.jsonl`, `player_events.jsonl`, the video metadata and `policy.log`, joined with `QtFuture::whenAll(...)`; `RunData` is built in a `.then()` continuation, so no pool thread blocks waiting for another (D5) | Independent files, CPU-bound JSON parsing, no pool starvation when many bundles load at once |
| Parsing `sim.log` | The memory-mapped log is split into line-aligned chunks; `QtConcurrent::mappedReduced` (map: parse one chunk into partial vectors; reduce: append in order) | The largest file; scales if logs grow |
| **Library scan** | `QtConcurrent::mapped` over the bundle folders, reading `run.json` and `summary.json` | All folders read in parallel |
| **Log filtering** | `QtConcurrent::run` per filter request over chunks of the line index; each task captures a `std::shared_ptr<const LogIndex>` by value (D9) and takes a `QPromise` so `cancel()` works. A `std::atomic<uint64_t>` generation counter drops stale results, and a 150 ms `QTimer` debounces typing | The console stays responsive while typing |
| **GStreamer streaming threads** | Created and owned by GStreamer (one per queue/decoder). Our code runs there only in the `appsink` `new-sample` callback, which copies the frame and posts it to the GUI thread | Decoding and scaling 4K H.264 never touches the GUI thread |
| **GStreamer bus thread** | A `std::jthread` loops on `gst_bus_timed_pop()` (100 ms timeout) until its `std::stop_token` is set; forwards errors, end of stream and state changes as queued calls | Qt's macOS event loop is not a GLib main loop, so the bus is read by our own thread |
| GStreamer initialisation | `std::call_once` around `gst_init()` | Exactly once per process, from any thread |
| **Pre-rendering** (Qt Multimedia engine, `render_experienced`) | `RenderQueue`: one `std::jthread` consumer; the job queue is guarded by a `std::mutex` and a `std::condition_variable_any` waiting on the thread's `std::stop_token` | Producer/consumer; the `jthread` requests stop and joins on destruction |
| Per-segment transcoding | For each job, one task per segment on a dedicated `QThreadPool` (at most `idealThreadCount() - 1`); each runs `ffmpeg` via `QProcess`, looping on `waitForFinished(100)` and checking its copy of the job's `std::stop_token` in between; on stop, `ProcessGuard` kills the process (D7). Then one concat step | Segments are independent, so N cores give close to N times the speed; cancelling takes effect within 100 ms |
| Progress and frames back to the GUI | `QMetaObject::invokeMethod(receiver, …, Qt::QueuedConnection)`, receiver held as a `QPointer` | Safe cross-thread hand-off, even if the window has closed |
| **Shared mutable state** | `RenderCache` (bundle → rendered mp4, plus jobs in flight): `std::mutex` + `std::scoped_lock`. `GstSegmentSource` frame slot: latest `QVideoFrame` behind a `std::mutex`, plus a `std::atomic<bool>` "frame pending" flag so at most one queued call is in flight. Progress counters: `std::atomic<int>` | The only state written by one thread and read by another |

**Not threaded on purpose:** building and painting the charts (at most about 6,000 points per
series). Downsampling is a pure function, so it can move to a worker later.

**Threading rules** (every one of them is checked in code review):
1. Shared data goes into a task **by value**: the lambda captures its own copy of
   `std::shared_ptr<const RunData>` or `std::shared_ptr<const LogIndex>`, never a reference
   to `Session`'s member (D15).
2. Every cancellable task takes a `QPromise` and checks `isCanceled()` between units of work;
   cancelling the future of a plain `QtConcurrent::run` would do nothing (D10).
3. `Qt::BlockingQueuedConnection` is never used: the GUI thread waits on futures at shutdown,
   so a worker blocking on the GUI thread would deadlock (D10).
4. No pool task blocks waiting for another pool task; join with `QtFuture::whenAll` and
   continue with `.then()` (D5).
5. `GstSegmentSource`: streaming threads never touch `sink` (a `QPointer`, not thread-safe);
   only `deliverFrame()` uses it, on the GUI thread. `setRun()` is allowed only while the
   pipeline is in state `NULL` (asserted). Streaming threads only read data captured when the
   pipeline was built (D6).
6. A class that owns a thread stops and joins it explicitly at the start of its destructor,
   before any member it uses is destroyed (D1, D2).

**Shutdown:** `MainWindow`'s `FutureGuard`s cancel and wait for every outstanding future. The
`ReplayPlayer` destroys its engine first: `GstSegmentSource` stops and joins its bus thread,
then its `PipelineGuard` sets the pipeline to `NULL` (which stops GStreamer's streaming
threads) and unrefs it. The `RenderQueue` destructor requests stop, kills running `ffmpeg`
processes and joins. There are no detached threads anywhere.


## 5. RAII and ownership

| Resource | Owner | Type |
| --- | --- | --- |
| Widgets, models, timers, `QMediaPlayer` | Qt parent-child tree | `new` with a parent; the parent deletes it |
| Long-lived non-QObject members (parsers, clock) | the owning class | `std::unique_ptr` or a plain member |
| `MainWindow` private state | PIMPL | `std::unique_ptr<Impl>` |
| The replay engine | `ReplayPlayer` | `std::unique_ptr<ReplaySource>` from the factory |
| The loaded run | every panel, worker and the engine | `std::shared_ptr<const RunData>` |
| Library entries | `LibraryModel` | `QList<QSharedPointer<const BundleSummary>>` |
| Widgets referenced from async callbacks | the callback | `QPointer<T>`, null once the widget is destroyed |
| QObjects created for another thread | the worker | `std::unique_ptr<T, DeleteLater>`; the deleter calls `deleteLater()` |
| Long-lived services: `RenderQueue` | `MainWindow::Impl` | `std::unique_ptr`. C++ destroys `MainWindow`'s members (so `Impl` and the queue) **before** the `QWidget` base deletes the child widgets (so `ReplayPlayer` and its engine). Therefore `~MainWindow()` starts with `shutdown()`: cancel and wait for the futures, destroy the replay engine (`ReplayPlayer::releaseEngine()`), then let `Impl` destroy the queue. Every engine is gone before the queue it points to (D11) |
| Log index | `Session` | `std::shared_ptr<const LogIndex>`; filter tasks hold copies (D9, D11) |
| Log filter | `LogConsole` | Qt child object (D11) |
| Memory-mapped log | `LogIndex` | `MappedFile`: holds `std::unique_ptr<QFile>` (a `QFile` cannot be moved) and the mapping; move-only, hand-written move constructor and move assignment (assignment unmaps first, the moved-from object is left empty); lines are `QByteArrayView`s into it (D4) |
| A bundle claimed for rendering | the render job | `ClaimGuard`: created by `RenderCache::tryClaim()`; releases the claim in its destructor unless `commit(mp4Path)` was called, so a failed job never leaves a bundle stuck "in flight" (D8) |
| Pending futures | `FutureGuard<T>` | cancels and calls `waitForFinished()` in its destructor |
| `ffmpeg` child process | `ProcessGuard` | kills and waits in its destructor |
| Render scratch files | the render job | `QTemporaryDir`, deleted whether the job succeeded or failed |
| Rendered video and cache index | the render job | `QSaveFile` (atomic write) |
| GStreamer elements, pads, bus | `GstSegmentSource` | `GstPtr<T>` = `std::unique_ptr<T, GstUnref>` (`gst_object_unref`). **Only wraps references we own:** `gst_element_factory_make()` returns a *floating* reference that `gst_bin_add()` takes over, so either call `gst_object_ref_sink()` before wrapping, or keep elements owned by the pipeline as plain non-owning pointers. Otherwise the element is released twice (D3) |
| GStreamer caps, samples, messages | the scope using them | `GstCapsPtr`, `GstSamplePtr`, `GstMessagePtr` (`unique_ptr` with `gst_caps_unref`, `gst_sample_unref`, `gst_message_unref`) |
| A mapped GStreamer buffer | the `appsink` callback | `BufferMapGuard` (`gst_buffer_map` / `gst_buffer_unmap`) |
| The pipeline | `GstSegmentSource` | `PipelineGuard`: sets state `NULL`, waits, then unrefs, in the order GStreamer requires |
| Locks | the enclosing scope | `std::scoped_lock`, `QMutexLocker`, `std::unique_lock` for the condition variable |
| Cursor ↔ player feedback loops | the enclosing scope | `QSignalBlocker` |

**Rules:** no naked `delete`; no `new` without either a Qt parent or immediate ownership by a
smart pointer; no GStreamer `*_unref` call outside the wrappers. Raw pointers are non-owning,
never null, and point to objects that outlive the holder (documented at each one).

**Copy and move rules for the guards (D12).** Every other class follows the rule of zero (no
hand-written copy, move or destructor). The tests check each row with `static_assert` on
`std::is_copy_constructible_v` / `std::is_move_constructible_v`.

| Class | Copyable | Movable |
| --- | --- | --- |
| `MappedFile` | no | yes |
| `GstPtr`, `GstCapsPtr`, `GstSamplePtr`, `GstMessagePtr` | no | yes |
| `ProcessGuard`, `FutureGuard<T>`, `ClaimGuard` | no | yes |
| `PipelineGuard`, `BufferMapGuard` | no | no |
| `RenderQueue`, `GstSegmentSource` (own threads) | no | no |


## 6. GUI (`app/`)

- `MainWindow` (`QMainWindow` with dock widgets). Menus: File (Open bundle, Open folder),
  Playback (engine choice, Render video), Help.
- `Session` (QObject): holds the current `shared_ptr<const RunData>` and the `TimeCursor`, and
  emits `runChanged`. Every panel connects to it.
- `TimeCursor` (QObject): `setTime(double simTime, Source)`. A panel ignores updates it caused.
- `LibraryView`: `QTableView` with a `LibraryModel` (video × link rate, stalls, cut-off flag).
- `SummaryPanel`: run information, ladder, warnings (log-agreement badge, BBB cut off).
- `SegmentTimeline`: custom `paintEvent` with rung bars, stalls, fallbacks, guard overrides and
  buffer-full waits. Click or drag to move the cursor.
- `ChartsPanel`: `QChartView`s sharing one x-axis: buffer (every 33 ms), throughput and its
  estimate, bitrate (steps), RTT and loss (gaps at `null`), decision delay; a cursor line.
- `LogConsole`: `QTableView` with a `LogTableModel` over `LogIndex`. Filter bar: text, level,
  this UE or all UEs, "follow cursor".
- `ReplayPlayer`: a `QVideoWidget`, an overlay (segment, rung, bitrate, resolution, buffer,
  engine name) and the engine (`ReplaySource`). Position ↔ cursor through `PlaybackClock`.
- `ReportView`: QoE metrics, the comparison with `summary.json`, warnings.


## 7. Media playback: two engines, one flag

### 7.1 Choosing the engine

| Level | Flag | Effect |
| --- | --- | --- |
| Build | CMake `-DABR_WITH_GSTREAMER=ON` (default) / `OFF` | `ON`: `media/gst/` is compiled and linked; configuring fails with a clear message if `pkg-config` cannot find GStreamer. `OFF`: only the Qt Multimedia engine exists, no GStreamer dependency |
| Run | `--engine=gstreamer` or `--engine=prerendered` on the command line | Overrides the saved setting for this launch |
| Run | Playback → Engine menu, saved with `QSettings` | The user's choice, remembered |
| Fallback | automatic | If the GStreamer pipeline cannot be built (a missing plugin such as `qtdemux`, `vtdec` or `avdec_h264`), the factory logs which element is missing, shows it in the status bar and creates the pre-rendered engine instead |

Default: GStreamer when compiled in, otherwise pre-rendered. `ReplaySourceFactory::available()`
lists the engines in this build; the menu and `--engine` only offer those.

### 7.2 The `ReplaySource` interface

```cpp
class ReplaySource : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Preparing, Playing, Paused, Stalled, Ended, Error };
    virtual QString name() const = 0;
    virtual void setRun(std::shared_ptr<const RunData> run) = 0;
    virtual void setVideoSink(QVideoSink* sink) = 0;   // the QVideoWidget's sink
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double videoTime) = 0;           // PlaybackClock converts sim time
signals:
    void positionChanged(double videoTime);
    void stateChanged(ReplaySource::State state);
    void errorOccurred(QString message);
};
```

Both engines write frames into the same `QVideoSink`, so the widget, overlay and cursor sync
are shared code. `ReplayPlayer` owns its engine as `std::unique_ptr<ReplaySource>`; switching
engine destroys the old one (stopping its threads) before creating the new one.

### 7.3 GStreamer engine (`GstSegmentSource`, default)

Plays `r{rung}/seg_XXXX.mp4` directly at the played rungs, switching resolution live at each
segment boundary. No render step and no `ffmpeg`: the replay is ready as soon as the bundle
loads, and every frame's timestamp maps exactly to a segment.

**Pipeline.** A `concat` element joins one branch per segment, so playback is gapless across
files with different resolutions:

```
segment k:    filesrc(r{rung_k}/seg_k.mp4) ! qtdemux ! h264parse ! decodebin3 ─┐
segment k+1:  ...                                                              ├─ concat
...                                                                            ┘
concat ! videoconvert ! videoscale ! capsfilter(video/x-raw,format=NV12,width=1920,height=1080)
       ! appsink(sync=true, max-buffers=2, drop=false, emit-signals=false)
```

- Branches are added a few segments ahead of the playing one, using `Segment::playedRung`.
- `videoscale` + `capsfilter` scale every rung to 1080p, so low rungs look as blurry as they
  should.
- `decodebin3` picks `vtdec` (VideoToolbox, hardware) on macOS and `avdec_h264` on Linux.
- **Stalls:** when `PlaybackClock` reaches a stall, the pipeline pauses for its duration; the
  last frame stays on screen and the state is `Stalled`.
- **Seeking:** flush, rebuild the `concat` chain from the target segment, then an accurate seek
  inside that segment's file.
- **Cut-off (BBB):** no branch after segment 54; end of stream at the cut-off frame.

**Frames into Qt.** `appsink` callbacks (`gst_app_sink_set_callbacks`, `noexcept`) run on a
GStreamer streaming thread: pull the sample (`GstSamplePtr`), wrap it in a `GstVideoBuffer`
(our `QAbstractVideoBuffer` subclass, Qt 6.8+) that maps it with `BufferMapGuard` and releases
the sample in its destructor, so frames are **not copied** (D16). The frame is NV12 (about
3 MB at 1080p instead of 8 MB for RGBA). The callback stores the `QVideoFrame` in the
mutex-guarded frame slot and, if no call is already pending (`std::atomic<bool>`), queues one
to the GUI thread that calls `QVideoSink::setVideoFrame()` and emits `positionChanged` from the
frame's PTS. Fallback if zero-copy causes trouble: copy into an NV12 `QVideoFrame`.

**Member order and destruction (D1).** Members are destroyed in reverse order of declaration,
so they are declared as:

```cpp
class GstSegmentSource final : public ReplaySource {
    // ...
private:
    std::shared_ptr<const RunData> run_;     // only changed while the pipeline is NULL
    QPointer<QVideoSink> sink_;              // GUI thread only
    GstElement* concat_ = nullptr;           // owned by the pipeline (non-owning)
    GstElement* appsink_ = nullptr;          // owned by the pipeline (non-owning)
    std::mutex frameMutex_;                  // frame slot: written by streaming threads
    QVideoFrame latestFrame_;
    std::atomic<bool> framePending_{false};
    PipelineGuard pipeline_;                 // stops streaming threads, then unrefs
    std::jthread busThread_;                 // declared last: stopped and joined first
};
```

The destructor also does it explicitly: request stop and join `busThread_`, then
`pipeline_.stop()` (state `NULL`, which waits until no streaming thread is running), and only
then let the remaining members be destroyed. No callback can run into a destroyed mutex or
frame slot. Queued calls to `deliverFrame()` that are still pending are dropped by Qt, because
they are posted with `this` as the context object.

### 7.4 Pre-rendered engine (`PrerenderedSource`, Qt Multimedia)

Plays `experienced_ue0.mp4`, built by the `RenderQueue` with `ffmpeg` (section 4), through
`QMediaPlayer`. Qt Multimedia picks the platform backend: AVFoundation on macOS (the only one
in Homebrew's Qt), FFmpeg on Linux. Used when GStreamer is not built in, not working, or chosen
by the user; also the reference for checking that both engines show the same thing.


## 8. Error handling and Qt type registration

**Errors are values, never exceptions across Qt or GStreamer (D13).** Qt is not exception-safe,
and an exception escaping a `QtConcurrent` task reaches the caller only as a generic
`QUnhandledException`, losing the original error.
- Parsers return a result struct (`value` + list of `ParseError { file, line, message }`);
  the loader collects them into `LoadResult::errors`, which the GUI shows as a readable list.
- Functions called from C (GStreamer `appsink` and bus callbacks) and all Qt slots are
  `noexcept`. Anything that could throw inside them (allocation, `std::` containers) is caught
  at that boundary and turned into an `errorOccurred` signal.
- Programming errors (broken invariants, `setRun()` while playing) use `Q_ASSERT` in debug
  builds.
- If the project moves to C++23 (supported by Apple clang 21), results become
  `std::expected<T, Error>`.

**Types sent through queued connections are registered (D14).** Otherwise a queued signal
fails at run time, not at compile time:
- `Q_ENUM` for `ReplaySource::State` and `TimeCursor::Source`;
- `Q_DECLARE_METATYPE` for `FilterSpec`, the filter row list (`QList<int>`),
  `LoadResult`, and `std::shared_ptr<const RunData>` wherever it is sent through a signal.
A test emits each queued signal across threads and checks that it arrives.
