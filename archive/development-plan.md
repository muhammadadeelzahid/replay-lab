# Development plan

The application described in [architecture.md](../docs/architecture.md) is built in the steps below.
Each step ends with something that runs and is tested, and is committed on its own.

Scope: one bundle at a time, the 14 bundles in [simulation-data.md](../docs/simulation-data.md), two
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


## Comparing ABR algorithms (ReplayLab)

Play one ABR run on its own, or several side by side, on the same video and network. The
viewer re-implements no ABR algorithm and simulates nothing: it reads, aligns and shows what
the bundles contain. What it asked of the ns-3 side is in
[ns3-data-requirements.md](ns3-data-requirements.md).

**Data (received 2026-11-20):** `viewer_bundles_phasef.tar`, 7.9 GB, 60 version 1 bundles,
one per run: 5 videos × 4 algorithms (`llm` abr-llm-v3, `robustmpc`, `mpc`, `bola`) × 3 feeder
rates (9, 18, 36 Mbps). Runs with the same video, rate and `run_N` share a seed (202, 203,
204) but are separate simulations (`conditions: independent`, stated in `run.json` notes).
Every segment and rung has VMAF (mean and minimum), SSIM and PSNR. Non-LLM runs have no
`policy.log` and no guard or inference-time fields. All 12 Big Buck Bunny runs stop at about
151 s, around segment 42 to 45 of 59. Computed QoE agrees with `summary.json` in all 60.

| Step | Work | Done when |
| --- | --- | --- |
| 15 | Data requirements for the ns-3 side; first delivery received and checked | done |
| 16 | Read the new runs: `policy.log` and LLM-only fields optional; stalls less than 1 s of playback apart merged into rebuffering episodes; each run's own algorithm, family and parameters shown instead of "LLM ABR"; VMAF, SSIM and PSNR read from the ladder; runs grouped by video, rate and `run_N` into experiments; the new set as the default data | all 60 runs and the old 14 load without errors; `validate_bundle` passes them |
| 17 | Comparison clock: one master time mapped to each run, by session time or by media time; QoE per run with the VMAF of what was shown | fast unit tests |
| 18 | Replay page layouts: Single (as now), Side by side (2), Grid (3 or 4); one transport; alignment toggle (same moment, same frame) | panes stay within one frame of each other over 60 s |
| 19 | Wipe view: two runs in one frame with a divider and a zoom loupe | works at 4K in light and dark |
| 20 | Charts with every run overlaid; scoreboard of QoE and VMAF per algorithm | numbers match each run's summary |
| 21 | Library grouped by experiment, Play and Compare on every run, home page and welcome updated | selection and grouping work in both appearances |
| 22 | Rename to ReplayLab, new icon, network text taken from the data, settings carried over | old settings carry over; Dock icon correct |
| 23 | README with a comparison GIF and screenshots | the LLM compared with RobustMPC, MPC and BOLA side by side |

Answers from the ns-3 side (2026-11-20):

- **Stall counts.** The ns-3 player resumes on a single frame (`src/dash/model/mpeg-player.cc`,
  lines 139 to 147), so while a slow download trickles in, the log alternates pause, one frame,
  pause. Each of the three cases is one rebuffering episode (RobustMPC, Big Buck Bunny
  18 Mbps: 5.4 to 15.4 s, 7.65 s stalled; LLM, chef_cooking 9 Mbps: 12.1 to 23.5 s, 9.12 s;
  BOLA, pexels_34797065 18 Mbps: 23.1 to 27.0 s, 2.80 s). The total stall time is real and is
  the only number the paper uses. **Decision:** the viewer merges stalls separated by less
  than 1 s of playback into one rebuffering episode (part of step 16), shows the episode count
  as "stalls" and the per-frame count as "player pauses"; the bundles stay as they are.
- **Big Buck Bunny.** The sessions end at 150 s with the satellite pass (no handover in the
  scenario); the full video needs about 195 s at 3.3 s per segment. **Decision:** keep it
  partial. The viewer already ends the replay where the simulation stopped, at the same point
  for every algorithm, and new simulations would no longer match the paper.


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
"./build/debug/app/SatReplay.app/Contents/MacOS/SatReplay" --engine=gstreamer viewer_bundles  # macOS
./build/debug/app/satreplay --engine=gstreamer viewer_bundles                                       # Linux
```


## Profiling

Measure with the release build. The numbers in the README come from:

```sh
B=build/release
APP="$B/app/SatReplay.app/Contents/MacOS/SatReplay"   # $B/app/satreplay on Linux
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
