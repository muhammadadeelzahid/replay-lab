# What the viewer needs from the ns-3 side

ReplayLab plays one ABR run on its own, or several runs side by side, so that the picture
quality of different ABR algorithms can be compared. This page lists what the bundles have to
contain for that. How the data is produced is up to the ns-3 side; the field layout is in
[bundle-format-v2.md](bundle-format-v2.md).


## 0. Today's bundle, and the closest form that works

### What the LLM bundles look like today (version 1)

One folder per run, for example `chef_cooking_llm_18Mbps_run_3/`:

```
run.json              run description
segments.jsonl        one record per decision, with the inputs the policy saw
player_events.jsonl   stream_start, playback_start, stall_start/stall_end, playback_end
samples.jsonl         buffer, throughput, RTT and loss at each decision
summary.json          session numbers
logs/sim.log          ns-3 output for the run
logs/policy.log       the decision stream, rebuilt from decisions.jsonl
videos/<name>/        video.json, segments.jsonl (bytes per segment and rung), r0..r5/seg_NNNN.mp4
```

`run.json` today:

```json
{
  "bundle_version": 1,
  "run_id": "chef_cooking_llm_18Mbps_run_3",
  "source_commit": "9cb7c95",
  "scenario": "ntn-iab-quic-dash-ai",
  "policy": { "name": "llm", "client": "ns3::AiDashClient", "model": "abr-llm-v2",
              "description": "…" },
  "network": { "num_ues": 1, "simulated_num_ues": 3, "num_relays": 1,
               "feeder_link_rate_bps": 18000000, "congestion_control": "ns3::QuicBbr",
               "rlc_mode": "AM", "rng_run": 1 },
  "player": { "segment_duration_s": 3.3, "ladder_bps": [250000, …, 12000000] },
  "sim_stop_time_s": 37.4231,
  "ues": [ { "ue": 0, "node_id": 6, "video": "chef_cooking", "stream_start_s": 0.124912 } ],
  "sim_log_decision_agreement": 1.0,
  "notes": "…"
}
```

A `segments.jsonl` record today:

```json
{"type": "segment", "ue": 0, "segment": 1, "request_s": 0.228408, "done_s": 1.024508,
 "rung": 4, "bitrate_bps": 6000000, "bytes": 1671884,
 "decision": {"throughput_estimate_bps": 7992855.2, "buffer_s": 5.54, "rtt_ms": 24.0,
              "loss_rate": 0.0, "fallback": true, "decision_delay_s": 0.0306}}
```

Things the viewer works around today: `rung` in record `n` is the rung chosen for segment
`n + 1`, not the one segment `n` was downloaded at; the played order of segments is rebuilt
from `PLAYING FRAME` lines in `sim.log`; the player played 3.3 s per segment while the files
are 2.0 s; there is no picture-quality score.


### The closest acceptable form

**The same version 1 bundle, once per algorithm.** Nothing about the layout or the files
has to change. For every algorithm, write the bundle exactly as for the LLM runs today, and:

1. **Name the algorithm** in `run.json`: `policy.name` (`llm`, `robustmpc`, `mpc`,
   `bola`) and `policy.client` (the ns-3 client's type name). Use
   `policy.model` only for learned policies.
2. **Keep the folder name pattern** with the algorithm in place of `llm`, for example
   `chef_cooking_robustmpc_18Mbps_run_3/`, and the same `run_N` for runs that belong together.
3. **Leave LLM-only fields out or null** where an algorithm has none: `decision.fallback`,
   `decision.decision_delay_s`, `decision.rtt_ms`, and `logs/policy.log`. The viewer treats
   them as optional.
4. **Keep the same conventions as the LLM bundles**, including the rung shift in
   `segments.jsonl` and the 3.3 s player, so that every algorithm's bundle is read the same
   way. Whatever the convention, it must be the same for all algorithms.
5. **Say in `run.json` `notes`** whether the runs of the same video, rate and `run_N` saw the
   same capacity or were separate simulations (see section 1).

The viewer groups bundles with the same video, feeder rate and `run_N` into one experiment.
With only this, it can play any run alone or side by side with the others of its group.

**Wanted, in order of value, when easy:**
- picture quality per segment and rung (`vmaf`, `vmaf_min`, `ssim`, `psnr` in
  `videos/<name>/segments.jsonl`, measured against the source);
- a `segment_start` event in `player_events.jsonl` for every segment that played (segment,
  rung, time, frames), so the viewer does not need `sim.log` for the play order;
- the rung in each `segments.jsonl` record being the one that segment was downloaded at.

**Ideal (not required):** the experiment layout of [bundle-format-v2.md](bundle-format-v2.md),
with the media stored once per experiment instead of once per run. The viewer will read both.


## 1. Experiments: runs that belong together

- An **experiment** is one video streamed at one feeder link rate by several ABR algorithms.
- The runs of an experiment must be comparable: the same video, the same ladder, the same
  network setup (topology, feeder rate, number of users) and the same seed.
- The experiment says how far the network conditions were shared between its runs, as one of:
  - `"same"`: every run saw the same capacity over time;
  - `"independent"`: the runs are separate simulations with the same settings, so the network
    may differ between them.
- Each run says which ABR algorithm it used: a short key (`llm`, `robustmpc`, `mpc`, `bola`), the
  ns-3 client name, the family (learned, mpc, buffer, rate or hybrid) and any parameters.


## 2. Per run: what was downloaded

For every segment the client downloaded:
- its index (0-based);
- the rung it was downloaded at, which is also the rung it was played at;
- its size in bytes;
- when the request was sent and when the download finished.

And for every ABR decision, what the algorithm saw when it chose:
- the time of the decision and the segment it was for;
- the buffer level;
- the throughput estimate;
- RTT and loss where known;
- anything algorithm-specific, in a free-form object (for the LLM policy: its own choice,
  whether the buffer guard replaced it, and its inference time).


## 3. Per run: what the user saw

- When the stream started and when playback started.
- Every stall, with its start and end, and whether it never ended.
- When each segment started playing and how long it played, or a clear rule for both. The
  viewer shows each segment's media file for that long.
- When playback ended, and whether the simulation stopped before the video finished.
- A summary: average bitrate, switches, stall count and time, startup delay. Version 1's
  `summary.json` fields are enough.

All times in simulation seconds, on one clock shared by every file of the run.


## 4. Media and picture quality

- The encoded ladder (every rung of every segment, playable on its own), once per
  experiment. All runs of the experiment use it.
- For every segment and rung, its real duration.
- **Wanted:** VMAF (mean and minimum over the segment), SSIM and PSNR, measured against the
  source video. Without these the viewer can only compare by bitrate and resolution.


## 5. Network description

What the viewer shows about the network, where known:
- the kind of network (the satellite and IAB path, or something else);
- feeder rate, number of simulated users, relays, congestion control and RLC mode;
- satellite altitude, carrier frequency and bandwidth.


## 6. Provenance

- The ns-3 commit and the exporter commit.
- Free-text notes on anything unusual: a re-run, missing data, a known difference from the
  paper's numbers.


## 7. What to compare

- **Algorithms:** the LLM policy, RobustMPC, MPC and BOLA.
- **Videos:** the five in the current bundles.
- **Rates:** 9, 18 and 36 Mbps.

A first delivery of one video at one rate, with 2 to 4 algorithms, is enough to start and to
check the format.
