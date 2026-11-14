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
- GStreamer tests: pipeline with `fakesink`, segment order and rungs, stalls, seeking.
- GUI tests run offscreen: opening a bundle fills the panels; moving the cursor updates them.
  The tests that play video (`tst_replay_source`, `tst_replay_player`) use the Cocoa platform
  on macOS, because Qt's AVFoundation backend delivers media through the Cocoa run loop, which
  the offscreen platform does not run. Their window is marked `Qt::WA_DontShowOnScreen`, so
  nothing appears on the desktop. They are disabled in the TSan build on macOS: the TSan
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
./build/debug/app/abrviz --engine=gstreamer viewer_bundles
```


## Setup

macOS: `brew install qt cmake ninja gstreamer ffmpeg`. Linux packages are listed in
`.github/workflows/ci.yml`.

If the macOS Command Line Tools are older than the OS, Homebrew's `libffi` refers to an SDK
that is not installed; CMake skips that include directory, and the linker prints harmless
"built for newer version" warnings. Updating the Command Line Tools removes both.
