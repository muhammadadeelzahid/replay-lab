# abr-viz: build plan

How the application in [ARCHITECTURE.md](ARCHITECTURE.md) gets built, step by step. Class and
threading diagrams are in [DIAGRAMS.md](DIAGRAMS.md). What the data contains, and the quirks
the viewer must handle, are in [bundle_info.md](bundle_info.md).

Scope: one LLM run at a time, the 14 delivered bundles as they are (no re-runs, no comparison
between runs). Two replay engines in v1: GStreamer (default) and pre-rendered Qt Multimedia,
chosen by a flag (ARCHITECTURE.md section 7).


## 1. Steps

Each step ends with something that runs and is tested. The "Design fixes" column refers to
section 2; each fix must be in place before its step is done.

| # | Work | Threading / RAII it introduces | Design fixes | Done when |
| --- | --- | --- | --- | --- |
| 0 | `git init`, CMake skeleton with presets (debug, release, asan, tsan), option `ABR_WITH_GSTREAMER`, empty window, Qt Test, CI on Linux (both flag values) | — | D13, D14 | Builds and tests run locally and in CI |
| 1 | Model types, `MappedFile`, `JsonlReader`, the file parsers, `applySegmentShift`, `validate_bundle` tool | `MappedFile`, `std::unique_ptr` | D4, D12, D13 | All 14 bundles validate; played rungs match the `sim.log` frame lines |
| 2 | `BundleLoader` (parallel parsing, progress, cancellation), `FutureGuard` | `QPromise`, `QtFuture::whenAll`, `mappedReduced`, `shared_ptr<const RunData>` | D5, D10, D12, D15 | A test loads all 14 bundles concurrently; cancelling mid-load is clean under TSan |
| 3 | `PlaybackClock`, `QoeMetrics`, `print_report` tool | — | — | Metrics match `summary.json`; BBB stops at segment 54 |
| 4 | `MainWindow` (with `Impl` owning the long-lived services), `Session`, `TimeCursor`, `LibraryView` (parallel scan), `SummaryPanel`, `SegmentTimeline` | `QtConcurrent::mapped`, `QFutureWatcher`, `QSharedPointer`, `QPointer` | D10, D11, D14, D15 | Open the folder, open a bundle, see the timeline with stalls |
| 5 | `ChartsPanel`, linked to the cursor | `QSignalBlocker` | — | All charts move together with the cursor |
| 6 | `LogIndex`, `LogFilter`, `LogConsole` | generation counter, cancellation, debounce | D9, D10, D14 | Typing a filter never blocks the UI; stale results are dropped |
| 7 | `RenderQueue`, `RenderCache`, `SegmentTranscoder`, `render_experienced` tool | `std::jthread`, condition variable, `QThreadPool`, `ProcessGuard`, `ClaimGuard`, `QTemporaryDir`, `QSaveFile` | D2, D7, D8, D11, D12 | BBB renders using all cores; cancelling or closing the app mid-render leaves no `ffmpeg` running |
| 8 | `media/`: `ReplaySource`, `PrerenderedSource`, `ReplaySourceFactory`, `ReplayPlayer` with overlay, two-way cursor sync | `std::unique_ptr<ReplaySource>`, `QPointer` in callbacks | D11, D14 | The pre-rendered replay scrubs both ways; the stall shows as a freeze |
| 9 | GStreamer foundation: `pkg-config` detection, `GstRuntime` (`std::call_once`), RAII wrappers (`GstPtr`, `GstCapsPtr`, `GstSamplePtr`, `GstMessagePtr`, `BufferMapGuard`, `PipelineGuard`) with tests | custom deleters | D3, D12, D13 | Builds with the flag ON and OFF; wrapper tests pass; `GST_TRACERS=leaks` reports nothing |
| 10 | `GstSegmentSource`: `concat` pipeline, `appsink` → `QVideoSink`, bus `std::jthread`, stalls, seeking, cut-off; `--engine` flag, Playback → Engine menu, `QSettings`, automatic fallback | GStreamer streaming threads, frame slot (`std::mutex` + `std::atomic<bool>`), `std::jthread` + `std::stop_token` | D1, D3, D6, D13, D14, D16 | Both engines pass the same replay tests on BBB 36 Mbps; TSan clean; deleting the engine during playback is clean under ASan |
| 11 | `ReportView`, profiling, README with measured numbers (load time with 1 vs N threads, render speed-up, GStreamer vs pre-rendered start-up time) | — | — | Numbers in the README, CI green for both flag values |


## 2. Design fixes from the C++ review

A review of ARCHITECTURE.md and DIAGRAMS.md against C++ and Qt practice (thread safety, RAII,
ownership) found the issues below. Each one is a change to the design documents **and** a rule
the code must follow. The documents are updated first (section 2.5), then each fix is
implemented in the step listed.

### 2.1 Bugs in the design (crash or undefined behaviour)

| # | Problem | Fix | Step | How it is tested |
| --- | --- | --- | --- | --- |
| D1 | **`GstSegmentSource` member order.** Members are destroyed in reverse order of declaration. As drawn, the frame slot (`frameMutex`, `latestFrame`, `framePending`) is destroyed *before* `pipeline` stops GStreamer, so a streaming thread can still be inside `onNewSample`, locking a mutex that no longer exists. | Declare members in this order: element pointers, frame slot, `pipeline`, `busThread` (last). Destruction then runs: bus thread stopped and joined → pipeline set to `NULL` (streaming threads stopped, no more callbacks) → frame slot → element pointers. Also write an explicit destructor doing those steps, so correctness does not depend on member order alone. | 10 | Delete the engine during playback 100 times under ASan and TSan |
| D2 | **`RenderQueue` member order.** `worker` (`std::jthread`) is declared first, so it is destroyed last: the mutex, condition variable, job queue and pool are destroyed while the worker thread still runs. | Declare `worker` last, and stop and join it explicitly at the top of the destructor. | 7 | Destroy the queue while jobs are queued and running, under ASan and TSan |
| D3 | **GStreamer floating references.** `gst_element_factory_make()` returns a floating reference and `gst_bin_add()` takes ownership of it. Wrapping the same pointer in `GstPtr` (which unrefs) releases the element twice. | `GstPtr` only wraps references we own: call `gst_object_ref_sink()` before wrapping, or keep elements owned by the pipeline as plain non-owning pointers. Write this rule in the wrapper's header comment. | 9, 10 | Wrapper unit tests for both cases; `GST_TRACERS=leaks`; ASan run of the pipeline tests |
| D4 | **`MappedFile` cannot be move-only as drawn.** It holds a `QFile` by value; `QFile` is a `QObject`, which can be neither copied nor moved. | Hold `std::unique_ptr<QFile>`. Write the move constructor and move assignment by hand: move assignment unmaps the current mapping first, and the moved-from object is left empty so its destructor does nothing. Copy is deleted. | 1 | Unit tests: move construct, move assign over a live mapping, destroy moved-from object |

### 2.2 Thread-safety gaps

| # | Problem | Fix | Step | How it is tested |
| --- | --- | --- | --- | --- |
| D5 | **Thread-pool starvation in the loader.** `BundleLoader::run` runs on the global pool and then blocks waiting for its child tasks in the same pool. With many bundles loading at once, the waiting parents can occupy every thread. Qt usually runs a not-yet-started task itself when you wait on it, but the design should not rely on that. | Start the child tasks, join them with `QtFuture::whenAll(...)`, and build `RunData` in a `.then()` continuation, so no pool thread ever blocks waiting. | 2 | Load 14 bundles concurrently with the pool limited to 2 threads; it must finish |
| D6 | **`GstSegmentSource::run` and `sink` used across threads.** Copying a `shared_ptr` member on one thread while another thread reassigns it is a data race; `QPointer` is not thread-safe at all. | Streaming threads never touch `sink`; only `deliverFrame()` uses it, on the GUI thread. `setRun()` is allowed only while the pipeline is in state `NULL` (asserted). Streaming threads only read data captured when the pipeline was built. Write these rules in the class comment. | 10 | TSan run of the pipeline tests, including `setRun()` between two playbacks |
| D7 | **Render cancellation does not reach running `ffmpeg` processes.** `QProcess::waitForFinished()` blocks, so a stop request is not seen until each segment finishes. | Each segment task gets a copy of the `std::stop_token` and loops on `waitForFinished(100)`, checking the token in between; on stop, `ProcessGuard` kills the process. | 7 | Cancel a BBB render after 1 s; no `ffmpeg` process is left and the call returns within 0.5 s |
| D8 | **A failed render can leave a bundle claimed forever.** If a job fails or throws between `RenderCache::tryClaim()` and `finish()`, the bundle stays "in flight" and is never rendered again. | Add `ClaimGuard`: created by a successful `tryClaim()`, it releases the claim in its destructor unless `commit(mp4Path)` was called. | 7 | Unit test: a job that fails releases its claim; the next render of the bundle starts |
| D9 | **The `LogIndex` lifetime behind filter tasks is not guaranteed.** Filter tasks read `QByteArrayView`s into the mapped log, but `LogFilter` holds a plain pointer to `LogIndex`. | `LogIndex` is created once and shared as `std::shared_ptr<const LogIndex>`; every filter task captures a copy, the same pattern as `RunData`. | 6 | Close the bundle while a filter is running, under ASan |
| D10 | **Two Qt threading rules missing from the design.** (a) `Qt::BlockingQueuedConnection` would deadlock at shutdown, when the GUI thread is waiting on futures. (b) `QFuture::cancel()` on a plain `QtConcurrent::run` does nothing unless the task checks for it. | (a) `Qt::BlockingQueuedConnection` is never used. (b) Every cancellable task takes a `QPromise` and checks `isCanceled()` between units of work. Both rules go into ARCHITECTURE.md section 4. | 2, 4, 6 | Code review rule; the cancel tests of steps 2 and 6 |

### 2.3 C++ standards and ownership

| # | Problem | Fix | Step | How it is tested |
| --- | --- | --- | --- | --- |
| D11 | **No owner named for `RenderQueue`, `LogIndex` and `LogFilter`.** `PrerenderedSource` and the factory take a raw `RenderQueue*`. | `MainWindow::Impl` owns `RenderQueue` as `std::unique_ptr`. Member order cannot protect the engines here, because C++ destroys `MainWindow`'s members before the `QWidget` base deletes child widgets such as `ReplayPlayer`; so `~MainWindow()` starts with `shutdown()`, which waits for pending futures and destroys the replay engine before `Impl` destroys the queue. `Session` owns the `shared_ptr<const LogIndex>`; `LogConsole` owns its `LogFilter`. Raw pointers are documented as non-owning and never null. | 4, 7, 8 | ASan run of "close the window during render and playback" |
| D12 | **Copy and move rules for the guards are not written down.** | All guards are non-copyable. Movable: `MappedFile`, `GstPtr` and the other `unique_ptr` wrappers, `ProcessGuard`, `FutureGuard`, `ClaimGuard`. Neither copyable nor movable: `PipelineGuard`, `BufferMapGuard`, `RenderQueue`. Every other class follows the rule of zero (no hand-written copy, move or destructor). | 1, 2, 7, 9 | `static_assert(!std::is_copy_constructible_v<…>)` (and the move traits) for every guard, in the tests |
| D13 | **No error-handling policy.** Qt is not exception-safe, and an exception escaping a `QtConcurrent` task becomes a generic `QUnhandledException`, losing the original error. | Parsers and the loader return errors as values (`LoadResult::errors`; per-parser result structs). Callbacks called from C (GStreamer `appsink` and bus) and all slots are `noexcept`. No exception crosses Qt or GStreamer. If the project moves to C++23 (Apple clang 21 supports it), use `std::expected` for results. | 0, 1, 9, 10 | Truncated and malformed bundle tests return readable errors and never crash |
| D14 | **Types in queued signals are not registered.** Queued connections fail at run time, not compile time, for types Qt does not know. | `Q_ENUM` for `ReplaySource::State` and `TimeCursor::Source`; `Q_DECLARE_METATYPE` for `FilterSpec`, the filter row list and `std::shared_ptr<const RunData>` when it is sent through a signal. | 0, 4, 6, 8, 10 | A test emits each queued signal across threads and checks it arrives |
| D15 | **Passing shared data into tasks.** Capturing `Session`'s `shared_ptr` member by reference in a lambda is a data race the moment the GUI opens another bundle. | Rule: `std::shared_ptr<const RunData>` (and `LogIndex`) is always passed into tasks **by value**, copied into the lambda. | 2, 4 | TSan run of "open another bundle while a filter is running" |

### 2.4 Performance

| # | Problem | Fix | Step | How it is tested |
| --- | --- | --- | --- | --- |
| D16 | **Copying full 1080p RGBA frames is expensive:** about 8 MB per frame, roughly 240 MB/s at 30 fps, plus a software `videoconvert` to RGBA. | Deliver NV12 (supported by `QVideoFrameFormat`, about 2.7 times smaller). Better: wrap the `GstSample` in a custom video buffer (Qt 6.8+ `QAbstractVideoBuffer`) that releases the sample in its destructor, so frames are never copied. | 10 | CPU use and dropped frames during BBB playback, recorded in the README (step 11) |

### 2.5 Document updates (done)

The design documents were updated to match the fixes above:

- **ARCHITECTURE.md**
  - Section 4 (multithreading): the loader uses `QtFuture::whenAll` + `.then()` (D5); the
    `GstSegmentSource` cross-thread rules (D6); render cancellation through the `stop_token`
    (D7); filter tasks capture `shared_ptr<const LogIndex>` (D9); the two Qt rules (D10);
    pass shared data by value (D15).
  - Section 5 (RAII): `MappedFile` holds `std::unique_ptr<QFile>` (D4); `ClaimGuard` (D8);
    the floating-reference rule for `GstPtr` (D3); owners of `RenderQueue`, `LogIndex`,
    `LogFilter` (D11); the copy/move table for every guard (D12).
  - Section 7.3: corrected destruction order including the frame slot (D1); NV12 or zero-copy
    frames (D16).
  - New section 8, "Error handling": errors as values, `noexcept` boundaries (D13), and the
    metatype registration rule (D14).
- **DIAGRAMS.md**
  - Diagram 1: `MappedFile` holds `unique_ptr~QFile~`; `RenderQueue` with `worker` last;
    `ClaimGuard` next to `RenderCache`; `LogFilter` holds `shared_ptr~LogIndex~`.
  - Diagram 2: `GstSegmentSource` members in the corrected order (frame slot before
    `pipeline`, `busThread` last).
  - Diagram 3: `MainWindow::Impl` owning `RenderQueue` before `ReplayPlayer`; `Session` owning
    the `LogIndex`.
  - Diagram 4: the loader's child tasks joined by `whenAll` (no blocking wait).


## 3. Testing

- **Unit tests:** every parser, `applySegmentShift`, `PlaybackClock` at its boundaries (before
  playback start, inside a stall, between segments, at the cut-off), the metrics.
- **Golden data:** trimmed copies of `chef_cooking_llm_18Mbps_run_3` and
  `big_buck_bunny_llm_36Mbps_run_2` (a stall, log agreement < 1) in `tests/data`, without media.
- **Threading tests:** load the same bundle from 8 threads; load 14 bundles with a 2-thread
  pool (D5); cancel at random points; destroy the window while loading, filtering, rendering
  or playing (D1, D2, D9, D11). All of them run under **ThreadSanitizer** and
  **AddressSanitizer**.
- **RAII tests:** `MappedFile` move and unmap (D4); `ProcessGuard` kills a long-running child;
  `ClaimGuard` releases on failure (D8); the `QTemporaryDir` is gone after a failed render;
  `static_assert`s on the copy/move rules of every guard (D12).
- **GStreamer tests:** headless pipeline tests with `fakesink` (gapless order, played rung per
  segment, stall pause, seeking, cut-off); wrapper tests for floating references (D3);
  `GST_TRACERS=leaks` clean.
- **Engine parity:** both engines show the same segment and rung at the same cursor positions
  on BBB 36 Mbps.
- **GUI tests (offscreen):** opening a bundle fills every panel; moving the cursor updates every
  panel.
- **Full-data test** (runs only if `viewer_bundles/` exists): all 14 bundles load and validate.


## 4. Verification

1. `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`
2. The same with the `tsan` and `asan` presets, and with `-DABR_WITH_GSTREAMER=OFF`.
3. `validate_bundle viewer_bundles/*`
4. Launch the app, open `viewer_bundles/`, open BBB 36 Mbps, and check the timeline, charts,
   logs, replay and report with each engine (`--engine=gstreamer`, `--engine=prerendered`).


## 5. Before starting

- `brew install gstreamer` (default replay engine) and `brew install ffmpeg` (pre-rendered
  engine, `render_experienced`); optionally `brew install ninja`.
- The project folder is not a git repository yet; step 0 starts with `git init`.
- Publication and licences (settled): the paper is expected to be published soon, so sharing
  the viewer, screenshots and bundles is fine. The Pexels videos are under the Pexels License
  (free use, modification allowed, no attribution required; not allowed: selling unaltered
  copies or redistributing them on stock sites), which a research viewer does not conflict
  with. Big Buck Bunny is CC-BY 3.0: the README credits "Big Buck Bunny © Blender Foundation,
  CC-BY 3.0", and the viewer shows each video's `source` and `license`.
