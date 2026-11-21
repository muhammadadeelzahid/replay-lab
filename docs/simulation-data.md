# Simulation data

ReplayLab reads run bundles exported from the `ns3-ntn-iab-ai` simulations (satellite 5G with
IAB relays, DASH over QUIC). The set-up lines of every `sim.log` describe the same network: a
satellite gNB (cell 1, node 3) at 550 km, 28 GHz with 100 MHz; one IAB relay (node 4) 10 m
above the ground, attached to the satellite at 549,990 m; three UEs within 100 m of the relay,
each attached to the relay (in BBB 36 Mbps the bundle's UE, node 7, is 85 m away); the DASH
server on the remote host behind the core. Each bundle is one streaming session of the LLM ABR policy
(`ns3::AiDashClient`). This page describes the bundles and the quirks the viewer has to handle.

The bundles are produced by `scratch/ai_llm/export_viewer_bundle.py` in the research
repository. The 14 bundles listed below are final; the viewer works with them as they are.


## Comparison bundles

`viewer_bundles_phasef` holds 60 bundles in the same format: the 5 videos at 9, 18 and 36 Mbps,
each streamed by four ABR algorithms (`policy.name` in `run.json`: `llm`, `robustmpc`, `mpc`,
`bola`). The differences from the bundles below:

- Big Buck Bunny is included at 9 Mbps; every Big Buck Bunny session stops at about 151 s, at
  the end of the satellite pass, so its playback is cut off for every algorithm.
- `videos/<name>/segments.jsonl` has `vmaf`, `vmaf_min`, `ssim` and `psnr` for each segment
  and rung.
- Only the LLM runs have `logs/policy.log` and the guard and decision-delay fields; the viewer
  treats them as optional.
- Runs with the same video, rate and `run_N` share a seed but are separate simulations.

## Bundles

14 bundles: 5 videos at 3 feeder link rates, without `big_buck_bunny` at 9 Mbps (no UE received
the whole video at that rate).

| Video | Segments | Frame rate | Source, licence |
| --- | --- | --- | --- |
| `big_buck_bunny` | 59 | 30 | Blender Foundation, CC-BY 3.0 |
| `pexels_34797065` | 22 | 30 | Pexels, Pexels License |
| `peace_autumn_lake` | 19 | 29.97 | Pexels, Pexels License |
| `chef_cooking` | 11 | 25 | Pexels, Pexels License |
| `pexels_30886101` | 9 | 59.94 | Pexels, Pexels License |

Link rates (S1-U feeder, shared by 3 simulated UEs): `run_2` = 36 Mbps, `run_3` = 18 Mbps,
`run_4` = 9 Mbps.

Each bundle contains one UE: the one that received the whole video, re-indexed to `ue 0`. Its
ns-3 node is `ues[0].node_id` in `run.json` (5, 6 or 7 depending on the bundle). In `sim.log`
the same UE appears as `ue-id: <node_id>`, `PlayerId: <node_id - 5>` and
`VidId: <node_id - 4>`.

The bundles are about 714 MB in total and are not stored in git.


## Layout

```
<video>_llm_<rate>_run_<n>/
  run.json              run description
  segments.jsonl        one record per ABR decision, with the inputs the policy saw
  player_events.jsonl   stream start, playback start, stalls, playback end
  samples.jsonl         buffer, throughput, RTT and loss at each decision
  summary.json          the exporter's session numbers
  logs/sim.log          ns-3 output for the run (all 3 simulated UEs)
  logs/policy.log       the decision stream (all 3 simulated UEs)
  videos/<name>/
    video.json          ladder, resolution per rung, frame rate, segment count
    segments.jsonl      size and bitrate of every (segment, rung)
    r0 .. r5/seg_NNNN.mp4   H.264 segments, each playable on its own
```

Ladder: 250 kbps (240p), 750 kbps (360p), 1.5 Mbps (480p), 3 Mbps (720p), 6 Mbps (1080p),
12 Mbps (2160p). The bundles have no per-segment VMAF; quality is shown as bitrate and
resolution.

Fields that matter beyond the obvious ones:

| Field | Meaning |
| --- | --- |
| `run.json` `network.simulated_num_ues` | UEs sharing the feeder in the simulation (3) |
| `run.json` `sim_log_decision_agreement` | How closely the decisions in `sim.log` match `segments.jsonl`. Below 1, the log (and the stalls taken from it) comes from a re-run of the same configuration |
| `run.json` `notes` | Free text from the exporter |
| `player_events.jsonl` stall `source`, `terminal` | Stalls come from the ns-3 player log; `terminal` marks a freeze that never ended |
| `summary.json` `completed` | Every segment was downloaded. It does not mean every segment was played (see below) |
| `video.json` `segment_duration_s` | Nominal media segment length, 2.0 s |
| `run.json` `player.segment_duration_s` | Segment length in the simulated player, 3.3 s |


## Three segment durations

| What | Duration | Where it comes from |
| --- | --- | --- |
| Downloading a segment | 0.08 s to several seconds | `done_s - request_s` in `segments.jsonl` |
| Playing a segment in the simulation | always 3.3 s (100 frames of 33 ms) | `PLAYING FRAME` lines in `sim.log` |
| Playing the segment's media file | usually 2.0 s, see below | the MP4 file |

The simulated player plays segments back to back with no gap. In BBB at 36 Mbps, segment 0
plays from 1.156 s to 4.423 s, segment 1 starts at 4.456 s, segment 2 at 7.756 s, and so on.
The only pauses are real stalls. Downloads run ahead of playback and fill the buffer.

Media files are not all 2.0 s long:
- the last segment of every video is shorter (0.8 to 1.8 s);
- `seg_0000` is 4.0 s long in `big_buck_bunny` and `pexels_34797065`.

The viewer therefore reads each file's real duration from its MP4 header instead of assuming
2.0 s.

All times in the bundle are simulation times. The replay plays the media at normal speed, so
it is shorter than the simulated session; the playback clock maps between the two timelines
segment by segment (see [architecture.md](architecture.md), "Playback clock").


## Rungs are shifted by one segment

ns-3 logs each decision as `segmentId: k newBitRate: X oldBitRate: Y`: segment `k` has just
finished downloading, `Y` is the rate it was downloaded at, and `X` is the rate chosen for
segment `k + 1` (`dash-client.cc`, lines 745 to 792). The exporter copied these one to one, so
`segments.jsonl` record `n` holds the timing of download `n` but the rung chosen for segment
`n + 1`.

The `PLAYING FRAME` lines confirm this in all 14 bundles: segment 0 is always played at
rung 0 (250 kbps, the client's fixed first request, with no decision), and segment `k` is
played at the rung of record `k - 1`. The last record is a decision for a segment after the
end of the video, which was never downloaded. The record's `bytes` were looked up with the
shifted rung, so they are wrong too.

The loader corrects this:
- segment 0 is played at rung 0; segment `k` at the rung of record `k - 1`;
- bytes are looked up again in `videos/<name>/segments.jsonl` with the played rung;
- download times are kept (record `n` is the download of segment `n`);
- the inputs of record `n` are shown at the end of download `n`, as the decision that set
  segment `n + 1`;
- the last record is kept as a decision but not as a segment.

Segment numbering differs between files:

| File | Index | Meaning |
| --- | --- | --- |
| `sim.log` `segmentId` | 0-based | segment just completed |
| `decisions.jsonl` `segment_id` (not in the bundle) | 1-based | segment the decision is for |
| `segments.jsonl` `segment` | 0-based | download `n` (but see above for the rung) |


## Playback that was cut off

The simulation stops at 180 s. Big Buck Bunny needs about 195 s of simulated playback
(59 x 3.3 s), so in both BBB bundles the player is still in segment 54 when the simulation
stops (frame 49 at 18 Mbps, frame 38 at 36 Mbps), and segments 55 to 58 are never shown.
`summary.json` still reports `completed: true`. The other 12 bundles play every segment to its
last frame, and their `sim_stop_time_s` is the time of that frame.

The viewer reports where playback stopped and ends the replay there.


## A skipped segment

In `pexels_34797065` at 36 Mbps the simulated player never shows segment 13: its download in
`segments.jsonl` takes no time (`request_s` equals `done_s`), `sim.log` has no decision after
segment 13, and playback goes from segment 12 straight to segment 14. No simulation time is
lost (segment 14's first frame follows segment 12's last frame after 33 ms, at 44.45 s), but
the `PlayTime` field of the `PLAYING FRAME` lines jumps by 3.3 s, from 42.867 to 46.2. The viewer
marks segment 13 as not played and leaves it out of the replay.


## Stalls

Three bundles have one short stall each: BBB 36 Mbps (0.36 s at 56.86 s), `peace_autumn_lake`
9 Mbps (0.18 s at 11.56 s) and `pexels_34797065` 36 Mbps (0.09 s at 37.73 s). The others have
none. The stalls in `player_events.jsonl` match the `[MPEG_PLAYER]` pause and resume lines in
`sim.log`.

About 33 ms after its last frame, every UE logs `No frames to play ... PAUSED` without a
matching `resumed`. That is the end of playback, not a stall, and the stall parser ignores a
pause that comes after the last segment's last frame.

In two of the three stall bundles `sim_log_decision_agreement` is below 1 (0.71 and 0.66), so
the stall comes from a re-run of the same configuration. The viewer shows the agreement value
next to the stall data.


## What `summary.json` measures

The exporter's numbers describe the policy's choices as recorded, not what was played:
`avg_bitrate_bps` is the mean bitrate over all records of `segments.jsonl` (including the
decision for the segment after the end), `switch_count` counts rung changes between records,
`avg_throughput_bps` is the mean of `samples.jsonl`, and `startup_delay_s` is
`playback_start - stream_start`. The viewer reproduces these exactly as a check, and also
reports the values as played: only the segments that were shown, at their played rungs. For BBB
at 18 Mbps, for example, the policy chose 8.12 Mbps on average with 21 switches, while the user
saw 7.92 Mbps with 22 switches.


## Samples

`samples.jsonl` has one sample per decision, usually 2 to 3 s apart, with gaps of up to 42 s
after the last download. `rtt_ms` is `null` before the first RTT measurement. `null` values are
drawn as gaps, never as 0. Some early `loss_rate` values are close to 0.9; they are shown as
recorded.

The buffer is available at a much finer grain from `sim.log`: every `PLAYING FRAME` line has a
`queueLength` (frames buffered), and `queueLength x 0.033 s` matches `buffer_s` to within
0.04 s. The buffer chart uses the frame lines.

There is no congestion window, SINR, MCS or feeder-link data in the bundles.


## `sim.log` line types

`sim.log` contains all 3 simulated UEs. Only some lines carry a time.

| Line | Fields | Used for |
| --- | --- | --- |
| `<t> ue-id: N videoId: V segmentId: k newBitRate: X oldBitRate: Y estBitRate: E interTime: I T: B dT: D del: W fetch: F loss: L` | `t`: segment `k` finished. `E`: throughput estimate. `I`: cumulative stall time. `B`: buffer (s). `D`: buffer change since the last decision. `W`: wait before the next request because the buffer is full. `F`: download time of segment `k`. `L`: recent loss rate | played rung (`Y`), buffer-full waits, cross-checks |
| `0x<ptr> <t> PLAYING FRAME: PlayerId: p VidId: v SegId: k Res: R FrameId: f PlayTime: s ... queueLength: n` | one per rendered frame, 100 per segment, 33 ms apart; `R`: bitrate playing; `n`: frames buffered | buffer every 33 ms, played rung check, where playback stopped |
| `[MPEG_PLAYER] No frames to play at t=<t>s. Player ID=p ... PAUSED. Interruptions=n` | stall start | stall check |
| `[MPEG_PLAYER] Play resumed at t=<t>s. Player ID=p Pause duration=<s> ...` | stall end | stall check |
| `[MPEG PLAYER] FinalizeInterruptionTime ...` | at simulation end | not used |
| `ue-id: N InterruptionTime: s interruptions: c avgRate: r minRate: r AvgDt: s changes: c TotalPlaybackTime: s` | per-UE end-of-run summary | not used: present for the bundle's UE in only 5 of 14 logs, and `InterruptionTime` includes idle time after the video ends |
| `Simulator Time: <t>` | progress marker every 0.25 s | log times |
| setup lines (node creation, DASH setup, bandwidth check) | configuration echo, no time | shown at time 0 |

`policy.log` has one line per decision for all 3 UEs:
`<t> ue-id: N seg(1-idx): k rung: r bitrate: b thr_est: e buf: s infer_ms: m reason: <reason>`.
The reason is either `llm_policy_model` or `llm_policy_model+guard(buf<B>:r<from>->r<to>)`
when the buffer guard lowered the LLM's choice. `policy.log` is 1-indexed: line `k` belongs to
`segments.jsonl` record `k - 1`.


## The `fallback` flag means "guard override"

`segments.jsonl` `decision.fallback` is `true` exactly when `policy.log` shows a `+guard(...)`
reason for the same decision (298 of 301 decisions; the remaining 3 have no `policy.log`
line). No decision in the bundles fell back from the LLM to another policy. The viewer labels
these decisions as guard overrides and shows the rung the LLM wanted and the rung the guard
chose, from the reason text.
