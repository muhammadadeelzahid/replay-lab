# Exporting ABR comparison experiments from ns-3

**Optional.** ReplayLab compares ABR algorithms without new simulations, by replaying each
existing run's capacity through ports of the ns-3 clients (see
[development-plan.md](development-plan.md), "Comparing ABR algorithms"). The exports described
here would add what replay cannot: algorithms that change the network through their own
traffic, and VMAF measured against the source video.

What the `ns3-ntn-iab-ai` repository on Grex needs, so that the viewer can compare ABR
algorithms. The result is a set of experiments in the format of
[bundle-format-v2.md](bundle-format-v2.md).

The notes below were made against the local copy of the repository, which is older than the
one on Grex; file and line references may have moved.


## 1. One decision log for every ABR client

Today only `AiDashClient` writes a structured `decisions.jsonl` (through
`scratch/run_ai_dash_llm.py`). The classic clients (`FdashClient`, `SvaaClient`, `SftmClient`,
`RaahsClient`, `AaashClient`, `OsmpClient`, `BolaClient`) only print
`ue-id … newBitRate … estBitRate …` lines (`src/dash/model/dash-client.cc`, around line 762).

Add a decision logger to the `DashClient` base class, enabled by an attribute such as
`DecisionLog` (file path), so that every client writes one JSON line per decision:

```json
{"ue": 0, "t": 21.400, "segment": 7, "rung": 3, "bitrate_bps": 3000000,
 "buffer_s": 9.8, "throughput_est_bps": 7600000, "rtt_ms": 112, "extra": {}}
```

- `segment` is the 0-based segment the decision is **for**, and `rung` the rung it will be
  requested at.
- Log the download of each segment too: `{"ue": 0, "segment": 7, "rung": 3, "bytes": …,
  "request_s": …, "done_s": …}`.
- `extra` is filled by the subclass (for the LLM client: `llm_rung`, `guard`,
  `decision_delay_s`).
- Log `segment_start` events from the player (segment, rung, time, frames) next to the
  existing `[MPEG_PLAYER]` stall lines.


## 2. Make the simulated segment duration match the media

The player plays every segment for 3.3 s (100 frames of 33 ms), while the media files are
2.0 s. Set the player's segment duration from the metadata CSV, so that a played segment lasts
as long as its file. Without this, the side-by-side replay has to stretch time per segment.


## 3. Two kinds of comparison

**Trace-driven (default for picture comparison).** Every algorithm sees exactly the same
capacity.
1. Run the reference algorithm (the LLM policy) and record each user's throughput over time
   (`throughput_trace.jsonl`, already written for the benchmark).
2. Replay that trace through every algorithm. Either:
   - offline, as `scratch/ai_llm/benchmark/perceptual_qoe.py` does (simplest, and already
     used for the paper's scores); or
   - in ns-3, with the feeder link's rate driven by the trace.
3. Export each replay as a run, with the same player model (buffer, startup, stalls) for
   every algorithm.

**Live.** Each algorithm runs in its own simulation with the same `--run`, video,
`--linkRate` and `--numUes`. Before exporting these, make the randomness repeatable:
- call `AssignStreams` on the scenario's random variables (none is called today);
- give the per-request random variable in `src/dash/model/dash-server.cc` (around line 486) a
  fixed stream per user instead of creating a new variable per request.

The network still differs between runs, because every user runs the same algorithm and the
scheduler and channel react to their traffic. `experiment.json` says `"conditions": "live"`.

Select the client with `--algorithm=ns3::<Name>Client`, and pass `--aiEnable=false` for every
client except `AiDashClient`.


## 4. Run matrix

| | First batch | Full |
| --- | --- | --- |
| Algorithms | llm, svaa, sftm, bola | llm, fdash, svaa, sftm, raahs, aaash, osmp, bola |
| Videos | big_buck_bunny, chef_cooking, peace_autumn_lake, pexels_30886101, pexels_34797065 | same |
| Feeder rates | 9, 18, 36 Mbps | same |
| Conditions | trace | trace and live |
| Sessions | 60 | 240 |

Please also copy back the slurm job scripts and `build_job_matrix.py` settings used, so that
the runs can be repeated.


## 5. Picture quality per segment and rung

For every video, segment and rung, measure against the source (mezzanine) video with ffmpeg's
`libvmaf`:
- VMAF, mean and minimum over the segment's frames (`vmaf_4k_v0.6.1` for 2160p sources);
- SSIM and PSNR on luma;
- the rung upscaled to the source resolution with bicubic scaling before comparison.

Store them in `media/<video>/segments.jsonl` (`vmaf`, `vmaf_min`, `ssim`, `psnr`). This is done
once per video and shared by every run and experiment.


## 6. Export and hand-over

- Extend `scratch/ai_llm/export_viewer_bundle.py` (or write `export_experiment.py`) to write
  one experiment folder per (video, rate, conditions, seed), with the media once and one
  `runs/<algorithm>/` folder per algorithm.
- Fill `source_commit` and `exporter_commit` in `experiment.json`.
- Copy to the laptop with, for example:
  `rsync -av grex:<path>/viewer_experiments/ viewer_experiments/`
- A smaller first hand-over is welcome: one video at one rate with 2 to 4 algorithms, to check
  the format before running the full matrix.
