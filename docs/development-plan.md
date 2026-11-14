# Development plan

The application described in [architecture.md](architecture.md) is built in the steps below.
Each step ends with something that runs and is tested, and is committed on its own.

Scope: one bundle at a time, the 14 bundles in [simulation-data.md](simulation-data.md), two
replay engines (GStreamer and pre-rendered). Comparing runs side by side is not planned.


## Steps

| # | Work | Done when |
| --- | --- | --- |
| 0 | CMake project, presets (debug, release, nogst, asan, tsan), `ABR_WITH_GSTREAMER` option, empty window, Qt Test, CI | builds and tests pass with every preset |
| 1 | Data model, `MappedFile`, JSON Lines reading, bundle file parsers, `sim.log` and `policy.log` parsers, MP4 duration reader, `applySegmentShift`, `validate_bundle` tool | all 14 bundles validate; played rungs match the `sim.log` frame lines; parsed stalls match `player_events.jsonl` |
| 2 | `BundleLoader` (parallel parsing, progress, cancellation), `FutureGuard` | loading 14 bundles at once, with the pool limited to 2 threads, finishes; cancelling during a load is clean under TSan |
| 3 | `PlaybackClock`, `computeQoe`, `print_report` tool | metrics agree with `summary.json` where they measure the same thing; BBB stops in segment 54 |
| 4 | `MainWindow` and its shutdown order, `Session`, `TimeCursor`, `LibraryView` (parallel scan), `SummaryPanel`, `SegmentTimeline` | open the folder, open a bundle, see the timeline with stalls and guard overrides |
| 5 | `ChartsPanel` linked to the cursor | all charts follow the cursor; missing RTT values are gaps |
| 6 | `LogIndex`, `LogFilter`, `LogConsole` | typing a filter never blocks the GUI; results of older requests are discarded; closing a bundle during filtering is clean under ASan |
| 7 | `RenderQueue`, `RenderCache` and `ClaimGuard`, `SegmentTranscoder`, `ProcessGuard`, `render_experienced` tool | BBB renders using all cores; cancelling leaves no `ffmpeg` process; a failed render releases its claim |
| 8 | `ReplaySource`, `PrerenderedSource`, `ReplaySourceFactory`, `ReplayPlayer` with overlay | the pre-rendered replay follows the cursor both ways; the stall shows as a freeze |
| 9 | GStreamer wrappers (`GstPtr` and friends, `PipelineGuard`, `VideoFrameMapGuard`), `gst_init` under `std::call_once` | wrapper tests pass; builds with the option on and off |
| 10 | `GstSegmentSource` (`splitmuxsrc` pipeline, zero-copy NV12 frames, bus thread, stalls, seeking, cut-off), `--engine` flag, engine menu, fallback | both engines show the same segment and rung at the same cursor positions; deleting the engine during playback is clean under ASan and TSan |
| 11 | `ReportView`, profiling, README with measured numbers | report shown for every bundle; numbers in the README |


## Next

| Step | Work | Done when |
| --- | --- | --- |
| 12 | API documentation: Doxygen comments on every function, class and file; `docs` build target | `cmake --build --preset release --target docs` builds the HTML with no undocumented-member warnings |
| 13 | Let people run it without building: release packages for macOS (`.app`, zipped), Windows (`.exe` with Qt, zipped) and Linux (AppImage), built by `.github/workflows/release.yml` on a `v*` tag with the scripts in `packaging/`, each with ffmpeg inside and the pre-rendered engine; `viewer_bundles.tar` added to the draft release by hand; a short screen recording or GIF in the README | the draft release has the three packages and the data; the README shows the app in motion |
| 14 | Web build with Qt for WebAssembly: the `nogst` configuration compiled to `.wasm`; the pre-rendered engine plays videos rendered ahead of time with `render_experienced` and hosted next to the app; a few sample runs bundled or fetched at start (file reading without memory mapping); Qt's multithreaded WebAssembly build, with the COOP/COEP headers provided by a service worker on GitHub Pages | a sample run opens and plays in a browser from GitHub Pages |

Step 13 is about an hour of work; step 14 is two to four days, mostly data loading and the
video path. Qt Multimedia is less complete on WebAssembly (seeking may be less precise) and the
full 680 MB of bundles is too large for a web page, so the web build ships a few sample runs.


## Testing

- Unit tests for every parser, `applySegmentShift`, `PlaybackClock` at its boundaries (before
  playback, inside a stall, between segments, at the cut-off), and the metrics.
- Small test bundles in `tests/data`, trimmed from `chef_cooking_llm_18Mbps_run_3` and
  `big_buck_bunny_llm_36Mbps_run_2` (a stall, log agreement below 1), without media files.
- Tests against all 14 bundles run when `viewer_bundles/` is present and are skipped
  otherwise.
- Threading tests: concurrent loads, cancellation at random points, destroying objects while
  their tasks run. They run under ThreadSanitizer and AddressSanitizer.
- RAII tests: `MappedFile` moves, `ProcessGuard` killing a running process, `ClaimGuard`
  releasing on failure, `static_assert`s on the copy and move rules.
- GStreamer tests: wrappers (reference counts, a pipeline with `fakesink` stopped by its
  guard, a mapped frame), and the engine on the synthetic run (spans, preroll, seeks into
  segments and the stall, real-time playback through the stall to the cut-off, pause inside a
  stall, deleting it while playing or prerolling). They run offscreen, also under TSan.
  `tst_replay_player` checks that both engines show the same picture at the same cursor
  positions of a full bundle (skipped without `viewer_bundles/`).
- The synthetic clip is 320 x 180: VideoToolbox refuses much smaller frames.
- GUI tests run offscreen: opening a bundle fills the panels; moving the cursor updates them.
  The tests that play video (`tst_replay_source`, `tst_replay_player`) use the Cocoa platform
  on macOS, because Qt's AVFoundation backend delivers media through the Cocoa run loop, which
  the offscreen platform does not run. They run as background-only applications
  (`runInBackground()`: never activated, no Dock icon) and their window is marked
  `Qt::WA_DontShowOnScreen`, so nothing appears and the keyboard focus stays in the app in use.
  `--screenshot`, `--profile` and `--record` do the same. They are disabled in the TSan build on macOS: the TSan
  runtime can deadlock inside itself when CoreMedia threads exit. Every test has a 300 s
  timeout.
- Tests keep rendered videos in the build folder (`ABRVIZ_RENDER_CACHE`); stand-in `ffmpeg`
  scripts (`ABRVIZ_FFMPEG`, or `RenderQueue::Options`) test failures and cancellation.


## Commands

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
cmake --preset tsan  && cmake --build --preset tsan  && ctest --preset tsan
cmake --preset nogst && cmake --build --preset nogst && ctest --preset nogst
./build/debug/tools/validate_bundle viewer_bundles/*
"./build/debug/app/Stream Inspector.app/Contents/MacOS/Stream Inspector" --engine=gstreamer viewer_bundles  # macOS
./build/debug/app/stream-inspector --engine=gstreamer viewer_bundles                                       # Linux
```


## Profiling

Measure with the release build. The numbers in the README come from:

```sh
B=build/release
APP="$B/app/Stream Inspector.app/Contents/MacOS/Stream Inspector"   # $B/app/stream-inspector on Linux
time $B/tools/print_report viewer_bundles > /dev/null
$B/tools/render_experienced --cache /tmp/abrviz-cold viewer_bundles/big_buck_bunny_llm_36Mbps_run_2
"$APP" viewer_bundles --bundle big_buck_bunny_llm_36Mbps_run_2 --engine gstreamer --time 50 --profile 10
"$APP" viewer_bundles --bundle big_buck_bunny_llm_36Mbps_run_2 --engine prerendered --time 50 --profile 10
```

`--profile` prints when the bundle and the log index were ready, how long the engine took to
show its first picture (rendering first if it has to), the frames shown per second over the
played seconds, CPU time as a share of one core, peak memory, and every gap of more than
150 ms between frames with the segment and rung it happened at. It stops early when the replay
ends, so `--time 0 --profile 300` plays a whole run; every run was checked this way with both
engines. The README pictures are made with `--screenshot` (the window is fully transparent).


## Setup

macOS: `brew install qt cmake ninja gstreamer ffmpeg`. Linux packages are listed in
`.github/workflows/ci.yml`.

If the macOS Command Line Tools are older than the OS, Homebrew's `libffi` refers to an SDK
that is not installed; CMake skips that include directory, and the linker prints harmless
"built for newer version" warnings. Updating the Command Line Tools removes both.
