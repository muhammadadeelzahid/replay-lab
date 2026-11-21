# Bundle format, version 2: experiments

Version 1 bundles hold one streaming session of one ABR policy (see
[simulation-data.md](../docs/simulation-data.md)). Version 2 holds an **experiment**: the same video
streamed over the same network by several ABR algorithms, so that the viewer can play them
alone or side by side. The viewer reads both versions; a version 1 bundle is shown as an
experiment with one run.

All times are simulation times in seconds, unless a field says otherwise. Segment indices are
0-based everywhere. Rates are in bits per second.


## Layout

```
<experiment_id>/                             e.g. chef_cooking_18Mbps_seed1
  experiment.json
  media/<video>/
    video.json
    segments.jsonl
    r0 .. r5/seg_NNNN.mp4
  runs/<algorithm>/                          e.g. runs/llm, runs/robustmpc, runs/mpc, runs/bola
    run.json
    segments.jsonl
    player_events.jsonl
    samples.jsonl
    summary.json
    logs/                                    optional: sim.log and any other logs
```

The encoded ladder is stored once per experiment in `media/`, not once per run. Runs never
contain media.


## experiment.json

```json
{
  "schema": 2,
  "experiment_id": "chef_cooking_18Mbps_seed1",
  "created_utc": "2026-12-01T10:00:00Z",
  "source_commit": "<ns-3 git commit>",
  "exporter_commit": "<exporter git commit>",
  "video": "chef_cooking",
  "conditions": "same",
  "seed": 1,
  "network": {
    "scenario": "ntn-iab-quic-dash",
    "kind": "ntn-iab",
    "feeder_link_rate_bps": 18000000,
    "simulated_num_ues": 3,
    "num_relays": 1,
    "congestion_control": "ns3::QuicBbr",
    "rlc_mode": "AM",
    "satellite_altitude_m": 550000,
    "carrier_frequency_ghz": 28,
    "bandwidth_mhz": 100
  },
  "runs": [
    { "algorithm": "llm",  "client": "ns3::AiDashClient", "family": "learned", "dir": "runs/llm" },
    { "algorithm": "robustmpc", "client": "<ns-3 client>", "family": "mpc",  "dir": "runs/robustmpc" },
    { "algorithm": "mpc",  "client": "<ns-3 client>",     "family": "mpc",     "dir": "runs/mpc" },
    { "algorithm": "bola", "client": "ns3::BolaClient",   "family": "buffer",
      "params": { "BufferTargetSeconds": 30, "Gp": 5 }, "dir": "runs/bola" }
  ],
  "notes": ""
}
```

| Field | Meaning |
| --- | --- |
| `conditions` | `"same"`: every run saw the same capacity over time. `"independent"`: separate simulations with the same settings, so the network may differ between runs. The viewer shows which |
| `network.kind` | `"ntn-iab"` draws the satellite path (server, LEO gNB, IAB relay, user). Any other value draws a generic server, network, user path |
| `network.*` | Shown in the app's network panel; any field may be missing |
| `runs[].algorithm` | Short, stable key used for colours and file names (`llm`, `robustmpc`, `mpc`, `bola`) |
| `runs[].family` | `learned`, `mpc`, `buffer`, `rate` or `hybrid` |
| `runs[].params` | Optional algorithm parameters, shown as given |


## media/&lt;video&gt;/

`video.json` and the `rN/seg_NNNN.mp4` files are as in version 1.

`segments.jsonl` has one record per (segment, rung), now with picture quality measured against
the source video:

```json
{"segment": 7, "rung": 3, "bytes": 812345, "actual_bitrate_bps": 3247000,
 "duration_s": 2.0, "vmaf": 81.4, "vmaf_min": 63.2, "ssim": 0.962, "psnr": 39.8}
```

| Field | Meaning |
| --- | --- |
| `duration_s` | The media file's real duration |
| `vmaf` | Mean VMAF over the segment's frames, model `vmaf_4k_v0.6.1` for 2160p sources (`vmaf_v0.6.1` otherwise), the rung upscaled to the source resolution with bicubic scaling |
| `vmaf_min` | Lowest frame VMAF in the segment |
| `ssim`, `psnr` | Mean over the segment's frames, luma, against the source |

Quality fields may be missing; the viewer then shows bitrate and resolution only.


## runs/&lt;algorithm&gt;/

`run.json`, `player_events.jsonl`, `samples.jsonl` and `summary.json` keep their version 1
fields, with these changes:

**run.json**

```json
"policy": { "name": "robustmpc", "client": "<ns-3 client>", "family": "mpc",
            "model": null, "params": {} }
```

`model` is set only for learned policies. `sim_log_decision_agreement` is optional.

**segments.jsonl**: one record per segment **downloaded**, with the rung it was downloaded
at. This removes the version 1 shift, where record `n` held the rung chosen for segment
`n + 1`.

```json
{"segment": 7, "rung": 3, "bitrate_bps": 3000000, "bytes": 812345,
 "request_s": 21.402, "done_s": 22.118,
 "decision": {"t": 21.400, "buffer_s": 9.8, "throughput_est_bps": 7600000,
              "rtt_ms": 112, "extra": {}}}
```

| Field | Meaning |
| --- | --- |
| `decision.t` | When the client chose this segment's rung |
| `decision.buffer_s`, `decision.throughput_est_bps` | What the client saw when it chose |
| `decision.rtt_ms` | Optional |
| `decision.extra` | Algorithm-specific inputs and outputs. For the LLM policy: `llm_rung` (its own choice), `guard` (true when the buffer guard replaced it), `decision_delay_s` (inference time) |

**player_events.jsonl**: as in version 1. Every segment that was played has a
`segment_start` event, so that the viewer does not have to rebuild the play order from
`sim.log`:

```json
{"t": 4.456, "event": "segment_start", "segment": 1, "rung": 2, "frames": 100}
```

**summary.json**: version 1 fields, plus `mean_vmaf_shown` and `min_vmaf_shown` (computed over
the segments that were played, weighted by their duration).

**logs/**: optional. Any logs the ns-3 side wants to include (for example `sim.log` and a
decision log); the viewer indexes `sim.log` for its Logs page when present. What the viewer
needs is listed in [ns3-data-requirements.md](ns3-data-requirements.md).


## Rules

- Segment `k` of a run is played from `media/<video>/r<rung>/seg_<k>.mp4`, with the rung from
  that run's `segments.jsonl`.
- Every run in an experiment streams the same video at the same feeder rate and seed.
- The experiment folder is self-contained; the viewer reads nothing outside it.
