# ABR Simulation Viewer: plan

A desktop application (C++20, Qt 6) for exploring adaptive video streaming simulations from
the `ns3-ntn-iab-ai` research project. It loads a finished simulation run of the LLM ABR
policy and shows:

- which quality rung the policy chose for every video segment,
- the video as the simulated user would have seen it, including quality switches and stalls,
- the network behind those decisions: throughput, buffer level, RTT and losses over time,
- the simulation and policy logs, searchable and filterable,
- an end-of-run report with QoE metrics.

The viewer never links ns-3. It reads a **run bundle**: a folder with a fixed layout, built by
the simulation side's exporter. The bundle is the contract between the research code and the
viewer. Its format is defined in [DATA_SPEC.md](DATA_SPEC.md); what was actually delivered is
described in [viewer_bundles/README.md](../viewer_bundles/README.md).


## 1. Scope of the first version

### In
- Open one bundle, or a folder of bundles shown as a **library** (video x link rate grid,
  with stall count and completion for each cell). One bundle is open at a time.
- Run summary: scenario, link rate, simulated UEs (3) vs UEs in the bundle (1), seed,
  video (title, licence), ladder with resolution per rung.
- Per-segment timeline: chosen rung (coloured bar, labelled bitrate and resolution), stalls,
  fallback decisions and buffer-guard overrides marked.
- Time-series charts: throughput, throughput estimate, buffer level, chosen bitrate, RTT,
  loss rate, LLM decision delay. All charts share one time axis and one cursor.
- Log console: `sim.log` and `policy.log`, filter by level, UE/node and text, jump to the time
  under the cursor.
- Replay player: plays the "experienced" video (chosen rung per segment, frozen frame during
  stalls) with an overlay of segment number, rung, bitrate, resolution and buffer,
  synchronised with the cursor.
- End-of-run report: average bitrate, number of quality switches, total rebuffer time, number
  of stalls, startup delay, fallback rate, average decision delay, and the segment reached when
  the simulation stopped. Cross-checked against `summary.json`.

Quality is shown as **bitrate + resolution**. Per-segment VMAF is not in the bundles (the
upstream values are unreliable), so no VMAF is shown anywhere.

### Out (later versions)
- Comparing runs side by side.
- Several UEs in one bundle or on one chart (bundles are single-UE), a map of UE/IAB/satellite
  positions.
- Starting simulations from the viewer, and live mode while a simulation runs (section 8).
- Switching rungs inside a custom decoder instead of playing a pre-rendered file.


## 2. What the simulation side delivered

The bundles are produced by `scratch/ai_llm/export_viewer_bundle.py` in the research repo, from
the paper benchmark (`Quic_artifacts/AiDash/bench_down_scored/`, the `logs_bench/` ns-3 logs and
the encoded assets of the `abr/` pipeline). We do not write or run the exporter, and there
will be no re-runs or re-exports: the viewer works with these 14 bundles as they are.

### 2.1 Delivered bundles (`viewer_bundles/`, 714 MB as a tar)
- **14 bundles** = 5 videos x 3 feeder link rates, minus `big_buck_bunny @ 9 Mbps` (no UE got
  the whole video).
- Videos (all 3840x2160 source, 2.0 s segments, 6 rungs, H.264 MP4 that play on their own):
  `big_buck_bunny` (59 segments, CC-BY Blender), `pexels_34797065` (22), `peace_autumn_lake` (19),
  `chef_cooking` (11), `pexels_30886101` (9).
- Link rates: `run_2` = 36 Mbps, `run_3` = 18 Mbps, `run_4` = 9 Mbps, shared by 3 simulated UEs.
- Policy: the LLM ABR policy (`ns3::AiDashClient`). `decision.fallback` marks the decisions
  where the LLM did not answer and a built-in rule decided instead.
- **Single UE per bundle**: the UE that received the whole video, re-indexed to `ue 0`
  Its ns-3 node is `ues[0].node_id` in `run.json` (5, 6 or 7, depending on the bundle); in
  `sim.log` that is `ue-id: <node_id>` and `PlayerId: <node_id - 5>`.

### 2.2 What we checked
- Every `segments.jsonl` byte count matches the video metadata for that (segment, rung), and
  all 6 rungs exist for every segment.
- Ladder resolutions: 240p, 360p, 480p, 720p, 1080p, 2160p.
- File pattern `r{rung}/seg_{segment:04d}.mp4`, no init segments.
- Three bundles have one short stall (0.09 to 0.36 s): BBB 36 Mbps, peace_autumn_lake 9 Mbps,
  pexels_34797065 36 Mbps. The others have none.
- What the simulated player really played, from the `PLAYING FRAME` lines in `sim.log`
  (section 2.5).

### 2.3 Answers to the earlier questions (DATA_SPEC section 9)
1. **Segment duration:** the simulated player uses 3.3 s, the media 2.0 s; times are copied from
   the simulator without rescaling (section 2.5).
2. **Segment index:** confirmed by the simulation side (`dash-client.cc`): when segment *k*
   finishes, the client picks the next rate, logs `segmentId: k newBitRate: <next>
   oldBitRate: <this>`, then requests segment *k + 1*. The bundle's rungs are therefore
   shifted by one (section 2.5). The three files number segments differently:

   | File | Index | Meaning |
   | --- | --- | --- |
   | `sim.log` `segmentId` | 0-based | segment just **completed** |
   | `decisions.jsonl` `segment_id` (not in the bundle) | 1-based | segment the decision is for |
   | bundle `segments.jsonl` `segment` | 0-based | meant as "segment being downloaded" |
3. **Videos per UE:** in ns-3 each UE has its own `VideoId`, but all use the same metadata;
   bundles carry one UE, so this no longer matters for v1.
4. **Stalls:** explicit, from the real ns-3 MPEG player in `sim.log` (`"source": "sim.log"`).
   A freeze that never resumed is one stall with `"terminal": true` up to session end.
   Every bundle's stalls match the `[MPEG_PLAYER]` pause/resume lines for its UE.
5. **Log timestamps and line types:** described by the simulation side (section 2.6).

### 2.4 Points the viewer must handle
- **`completed: true` means every segment was downloaded, not that it was played.** Only
  Big Buck Bunny is affected: its 59 segments need about 195 s of simulated playback
  (59 x 3.3 s), but the simulation stops at 180 s, so in both BBB bundles the player is in
  segment 54 (frame 49 at 18 Mbps, frame 38 at 36 Mbps) and segments 55 to 58 are never shown.
  The other 12 bundles play every segment to the last frame, and their `sim_stop_time_s` /
  `playback_end` is the time of that last frame. The viewer shows "playback cut off at
  segment 54" for BBB and ends the replay there.
- **Log agreement:** `run.json.sim_log_decision_agreement` < 1 means the attached `sim.log` (and
  therefore the stalls) comes from a re-run of the same cell whose decisions differ slightly
  (BBB 36 Mbps: 0.71, peace_autumn_lake 9 Mbps: 0.66). The viewer shows a warning badge with
  the agreement value.
- **Samples are per decision,** 2 to 3 s apart, with long gaps after the last download (up to
  42 s). No fixed grid, no CWND/SINR/MCS, no `link_samples.jsonl`. `rtt_ms` is `null` at
  startup. `null` is drawn as a gap, never as 0. The **buffer**, however, is available every
  33 ms from `sim.log` (section 2.6): `queueLength x 0.033 s` matches the bundle's `buffer_s`
  to within 0.04 s, so the buffer chart uses the frame lines and the samples only for the
  other series.
- **Logs contain all 3 simulated UEs,** not only the bundle's UE.
- Some `decision.loss_rate` values are unexpectedly high (0.90); shown as recorded.

### 2.5 Download time, play time and the 2.0 s vs 3.3 s mismatch

Three different durations are involved:

| What | Duration | Source |
| --- | --- | --- |
| Downloading a segment | varies, 0.08 s to several seconds | `done_s - request_s` |
| Playing a segment in the **simulation** | always 3.3 s (100 frames x 33 ms) | `sim.log` `PLAYING FRAME` |
| Playing a segment's **media file** | 2.0 s | `video.json` |

**There is no gap in the simulation.** The ns-3 player plays segment after segment, 3.3 s each,
with no pause between them (BBB 36 Mbps: segment 0 at 1.156 to 4.423 s, segment 1 at 4.456 s,
segment 2 at 7.756 s, ...). The only pause is the real stall (0.39 s at 56.8 s). Downloads
happen in the background, ahead of playback, and fill the buffer.

The mismatch only appears when we show real video: the simulation treats each segment as 3.3 s
of content, but the clip that represents it lasts 2.0 s. A gap would appear only if the replay
played each 2.0 s clip and then waited out the rest of its 3.3 s slot; that would look like a
1.3 s freeze in every segment and be mistaken for stalls, so the viewer does not do that.

**Replay approach:** the replay plays the clips back to back at normal speed (2.0 s each),
with a frozen frame for each stall (stall length taken from `player_events.jsonl`). The video
is therefore shorter than the simulated session (2.0/3.3 of it, plus stalls). A `PlaybackClock`
in `core/` maps between the two timelines **per segment**: segment *n* plays in the simulation
from `playback_start + n x 3.3 s` (+ earlier stalls) and in the video from `n x 2.0 s`
(+ earlier stalls). Inside a segment the mapping is linear. Moving the cursor on a chart seeks
the video to the same segment and the same fraction of it, and the other way round. The same
clock gives the segment reached at simulation stop. The buffer chart shows simulator seconds,
with a toggle for media seconds (x 2.0/3.3).

**Rungs are shifted by one segment.** ns-3 logs each decision as
`<t> ue-id: N segmentId: k newBitRate: X`: segment *k* has finished downloading and *X* is the
bitrate for the **next** segment, *k + 1*. The exporter copied these lines one-to-one, so
`segments.jsonl` record *n* holds the timing of download *n* but the rung chosen for segment
*n + 1*. This is confirmed in all 14 bundles by the `PLAYING FRAME` lines: segment 0 is always
played at rung 0 (250 kbps, the client's fixed first request, with no decision), and segment
*k* at the rung of record *k - 1*. The last record is a decision for a segment after the end
of the video, which was never downloaded (in `chef_cooking`, for example, the server's highest
`SendSegment` is `seg=10`). The record's `bytes` are looked up with the shifted rung, so they
are wrong too.

The viewer corrects this when loading (`RunData`):
- played rung of segment 0 = 0; played rung of segment *k* = rung of record *k - 1*
  (equivalently `oldBitRate` on the `sim.log` decision line for segment *k*, and `Res` on its
  `PLAYING FRAME` lines, which the loader checks);
- bytes of segment *k* = `videos/<v>/segments.jsonl` at (*k*, played rung);
- download times stay as recorded (record *n* = download of segment *n*);
- the decision inputs of record *n* (buffer, throughput estimate, RTT, ...) are shown at the
  end of download *n*, as the decision that set segment *n + 1*;
- the last record is shown as an unused decision, not as a segment.
The replay therefore shows what the simulated user really saw.

### 2.6 `sim.log` line types (from the simulation side, checked against the bundles)

The bundle's UE is `ue-id: <node_id>` and `PlayerId: <node_id - 5>` (`VidId = node_id - 4`).

| Line | Fields | Viewer use |
| --- | --- | --- |
| `<t> ue-id: N videoId: V segmentId: k newBitRate: <next> oldBitRate: <this> estBitRate: <thr> interTime: <s> T: <buf> dT: <Δbuf> del: <s> fetch: <s> loss: <r>` | `t` = segment *k* finished (decision time for *k + 1*); `estBitRate` = throughput estimate; `interTime` = cumulative stall time so far; `T` = buffer (s); `dT` = buffer change since last decision; `del` = wait before the next request because the buffer is full; `fetch` = download time of segment *k*; `loss` = recent loss rate | Played rung (`oldBitRate`); "buffer full, waiting" marks on the timeline (`del` > 0); cross-check of the samples |
| `0x<ptr> <t> PLAYING FRAME: PlayerId: p VidId: v SegId: k Res: <bps> FrameId: f PlayTime: <s> ... queueLength: n` | One per rendered frame, 100 per segment, 33 ms apart; `Res` = bitrate playing; `queueLength` = frames buffered | Buffer chart every 33 ms (`n x 0.033 s`); played rung check; segment reached at stop |
| `[MPEG_PLAYER] No frames to play at t=<t>s. Player ID=p ... PAUSED. Interruptions=n` | Stall start | Stall check |
| `[MPEG_PLAYER] Play resumed at t=<t>s. Player ID=p Pause duration=<s> ...` | Stall end | Stall check |
| `[MPEG PLAYER] FinalizeInterruptionTime ... current interruption_time: X s` / `Updated interruption_time: Y s` | At simulation end | Not used (see below) |
| `ue-id: N InterruptionTime: <s> interruptions: <c> avgRate: <bps> minRate: <bps> AvgDt: <s> changes: <c> TotalPlaybackTime: <s>` | Per-UE end-of-run summary (`GetStats`) | Optional cross-check of `avgRate` and `changes` |
| `Simulator Time: <t>` | Progress marker every 0.25 s | Log time |
| Setup lines (`=== BANDWIDTH VERIFICATION ===`, node creation, DASH setup) | Scenario configuration echo, no time | Shown in the summary's "configuration" tab |

Two points from checking all 14 logs:
- **End of the video looks like a stall.** About 33 ms after its last frame, every UE logs
  `No frames to play ... PAUSED` with no `resumed`. That is the end of playback, not a
  freeze; the exporter already leaves it out, and the viewer's stall parser must too (a
  `PAUSED` after the last segment's last frame is ignored).
- **The end-of-run summary is incomplete and includes idle time.** The `GetStats` line exists
  for the bundle's UE in only 5 of 14 logs, and its `InterruptionTime` (e.g. 151 s) counts the
  idle time from the end of the video to the end of the simulation. The viewer takes rebuffer
  time from the stalls only.

Not in the bundles, and not needed: the cluster-side run files (`decisions.jsonl`,
`trace_decisions.jsonl`, `throughput_trace.jsonl`, `run_manifest.json`, `user_metrics.csv`) and
the network-stack traces (QUIC RTT/cwnd, BBR state, PDCP/RLC stats, PHY `RxPacketTrace` with
SINR/MCS). Charts for CWND, SINR or MCS are therefore out of scope.


## 3. Run bundle format

The complete format is defined in **[DATA_SPEC.md](DATA_SPEC.md)**. As delivered:

```
<video>_llm_<rate>_run_N/
  run.json              run description, plus notes and log agreement
  segments.jsonl        one record per decision, with the policy's decision inputs
  player_events.jsonl   stream start, playback start, stalls, playback end
  samples.jsonl         buffer, throughput, RTT, loss per decision
  summary.json          the simulator's own session numbers
  logs/sim.log          real ns-3 output for this cell (all 3 UEs)
  logs/policy.log       decision stream reconstructed from decisions.jsonl (all 3 UEs)
  videos/<name>/        video.json, per-segment sizes, r0..r5 encoded segments
```

Fields beyond the original spec that the viewer uses: `network.simulated_num_ues`,
`sim_log_source`, `sim_log_decision_agreement`, `notes`, `summary.json` `completed`,
`segments_available`, `total_segments`, `stall_source`, `video.json` `quality_source`,
stall `source` and `terminal`.

`bundle_version` lets the viewer reject bundles it doesn't understand, with a clear message.


## 4. Video assets

- **Delivered** inside each bundle, all six rungs. The same video is repeated in each of its
  three bundles (about 324 MB for BBB); a later loader can share one copy.
- **Licences:** `video.json` carries `source` and `license`; the viewer shows them. Check the
  Pexels licence terms before a public demo; BBB is CC-BY 3.0.
- **Experienced video, built on the viewer side** (the exporter does not render it):
  - `tools/render_experienced <bundle>` uses `ffmpeg` to take, for each segment, the file at the
    rung actually played (section 2.5), scale every rung to one output resolution (1080p) so
    quality differences stay visible, join them at normal speed, and insert a frozen last frame
    for each stall. The result, `experienced_ue0.mp4`, goes into a cache directory next to the
    bundle and plays with Qt Multimedia. `ffmpeg` is a build/dev dependency
    (`brew install ffmpeg`).
  - Fallback if `ffmpeg` is missing: the viewer plays the segment files directly, with two
    `QMediaPlayer`s alternating so the next segment is preloaded.


## 5. Viewer architecture

```
app/          Qt Widgets application: main window, panels, charts, player, report
core/         Qt Core only, no widgets: bundle loading, data model, metrics
tests/        Qt Test: parsers, model, metrics, golden bundles
tools/        command-line tools (validate a bundle, print a report, render experienced video)
```

### Core (`core/`)
- `BundleLoader`: reads and validates a bundle on a worker thread, reports progress, returns a
  `RunData` or a list of readable errors. Accepts the optional fields being absent or `null`.
- `BundleLibrary`: scans a folder, reads `run.json` and `summary.json` only, groups by video
  and link rate.
- `RunData`: segments, samples, stalls, manifest and summary, stored for fast lookup by time
  (sorted vectors and binary search). Holds the **played rung per segment** (section 2.5),
  checked against the `PLAYING FRAME` lines in `sim.log`.
- `PlaybackClock`: simulation time to (segment, offset) to video time, and back (section 2.5).
- `LogIndex`: loads a log into memory and builds a line-offset index (logs are 1 to 3 MB; the
  interface allows a memory-mapped version later). Assigns each line a time from the line
  types in section 2.6 (`<t> ue-id:`, `0x<ptr> <t> PLAYING FRAME`, `at t=<t>s`,
  `Simulator Time: <t>`, and `<t> ue-id:` in `policy.log`); other lines inherit the previous
  line's time, setup lines get time 0. Also extracts the UE a line belongs to (`ue-id`,
  `PlayerId`, `Player ID=`, `node=`), so the console can show only the bundle's UE or all three.
- `SimLogParser`: for the bundle's UE, reads the decision lines, the frame lines (buffer every
  33 ms, played rung, last frame) and the stall lines (section 2.6) into `RunData`.
- `PolicyLogParser`: reads `reason` per decision (`llm_policy_model`, `+guard(...)`, fallback)
  for the bundle's node, joined to segments.
- `QoeMetrics`: average bitrate, switches, rebuffer time, stall count, startup delay, fallback
  rate, decision delay, segment reached at stop. Pure functions. Differences of more than 1 %
  from `summary.json` show a warning.

### GUI (`app/`)
- `MainWindow` with docked panels and one shared **time cursor** (`TimeCursor`).
- `LibraryView`: grid of bundles, double-click to open one.
- `SegmentTimeline`: custom-painted strip of rungs, stalls, fallbacks and guard overrides;
  click to move the cursor.
- `ChartsPanel`: Qt Charts series for the time series. Samples are irregular: drawn as steps
  (values hold until the next decision) or lines, with gaps at `null`.
- `LogConsole`: `QTableView` with a custom model over `LogIndex`.
- `ReplayPlayer`: `QMediaPlayer` plus an overlay widget; follows and drives the cursor through
  `PlaybackClock`.
- `ReportView`: QoE metrics for the open run.

### Threading
Loading, log indexing and rendering the experienced video run off the GUI thread. Results come
back through queued signals. The GUI thread only paints and handles input.


## 6. Step-by-step plan

Each step ends with something that runs and can be tested. The data steps of the old plan
(encoding, exporter) are done by the simulation side.

| Step | Work | Done when |
| --- | --- | --- |
| 0 | Project skeleton: CMake, Qt 6, `core` + `app` + `tests` + `tools`, CI on Linux. | Empty window builds in CI |
| 1 | `BundleLoader`, `SimLogParser`, `RunData` (with played rungs and 33 ms buffer), `tools/validate_bundle`. Golden data: a trimmed copy of `chef_cooking_llm_18Mbps_run_3` (no media). | All 14 bundles validate; played rungs match `sim.log`; broken copies give clear errors |
| 2 | `PlaybackClock` and `QoeMetrics` with tests. | Metrics match `summary.json` for all 14 bundles; segment at stop is 54 for BBB |
| 3 | Main window, `LibraryView`, summary panel, `SegmentTimeline`, time cursor. | Open the folder, open a bundle, see rungs, stalls and fallbacks |
| 4 | `ChartsPanel`, linked to the cursor. | All charts move with the cursor, `null` RTT shown as a gap |
| 5 | `LogIndex`, `PolicyLogParser`, `LogConsole`. | Clicking a chart shows the log lines at that time, UE filter works |
| 6 | `tools/render_experienced`, then `ReplayPlayer` with overlay. | Stall in BBB 36 Mbps visible as a freeze, no freeze anywhere else; scrubbing moves the video and back |
| 7 | `ReportView`. | Report shown for every bundle, warnings where `summary.json` or log agreement disagree |
| 8 | Profile (startup, memory, chart redraw), fix the worst hotspot, README with screenshots and measured numbers. Stretch: `LogIndex` benchmark on a generated 2 GB log. | Numbers in the README, CI green |


## 7. Testing

- **Core unit tests:** every bundle file, invalid and truncated input, `null` fields, metrics
  on known values, time lookups at boundaries.
- **`PlaybackClock` tests:** before playback start, inside a stall, at the boundary between
  segments, simulation stop in the middle of the video, round trip sim time to video time.
- **Played-rung test:** segment 0 is rung 0 and segment *k* is the rung of record *k - 1*,
  matching the `PLAYING FRAME` lines in `sim.log` for every bundle.
- **Stall parser test:** parsed stalls equal `player_events.jsonl` in all 14 bundles; the
  `PAUSED` line after the last frame is not counted as a stall.
- **Golden bundles:** trimmed copies of two or three real bundles in `tests/data` (one with a
  stall and agreement < 1), so tests never need ns-3 or the media files.
- **All delivered bundles:** a test that runs only when `viewer_bundles/` is present validates
  all 14 and compares metrics with `summary.json`.
- **GUI tests (Qt Test, offscreen):** opening a bundle fills the panels; moving the cursor
  updates every panel; the agreement warning appears when expected.
- **CI:** Linux build with warnings as errors, unit tests, and one AddressSanitizer job.


## 8. Where things run

- **Simulations and bundle export run on the cluster,** where ns-3 and the `abr/` pipeline
  already work. Bundles come to the desktop as a tar or with `rsync`.
- **The viewer runs on a desktop** (Linux or macOS). Cluster compute nodes have no display.
- **Live mode (later):** run one simulation in an interactive job, stream the same records over
  a TCP socket through an SSH tunnel, and let the viewer append them as they arrive. The
  line-based records make this reuse the same model.


## 9. Known limitations and open questions

The delivered bundles are **enough for the first version**, and they are final. The viewer
lives with these limitations:

1. **Segment shift** (section 2.5): corrected by the viewer when loading.
2. **Decision reason** (LLM, guard override, fallback) is not in `segments.jsonl`: read from
   `policy.log`.
3. **Big Buck Bunny is cut off at segment 54 of 59** (section 2.4): computed from `sim.log`
   and shown in the report; the replay ends there.
4. **Samples are per decision** (section 2.4): throughput, RTT and loss charts hold values
   between decisions; the buffer chart uses the 33 ms frame lines. No feeder-link, CWND, SINR
   or MCS chart.
5. **`loss_rate` values near 0.9** at early segments are shown as recorded.
6. **`GetStats` end-of-run lines** are missing for most UEs and include idle time: not used
   for rebuffer time (section 2.6).

Settled: the paper is expected to be published soon, so the viewer, screenshots and bundles
can be shared. Pexels License and CC-BY 3.0 (Big Buck Bunny, credit required) allow a public
demo; see PLAN.md "Before starting".
