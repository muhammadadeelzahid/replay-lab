---
pretty_name: ReplayLab LEO NTN ABR runs
license: other
license_name: cc-by-4.0-with-video-licences
license_link: LICENSE
language:
- en
tags:
- adaptive-bitrate-streaming
- abr
- dash
- ns-3
- satellite
- non-terrestrial-network
- 5g
- iab
- video-quality
- vmaf
- llm
size_categories:
- n<1K
configs:
- config_name: default
  data_files: runs.csv
extra_gated_heading: Terms of use
extra_gated_prompt: >-
  The simulation outputs (logs, JSON files, metrics) are licensed under CC BY 4.0. The video
  segments keep the licences of their sources: Big Buck Bunny is CC BY 3.0 (Blender
  Foundation), the other four videos are under the Pexels License. Use the videos for
  research and reproduction only; do not sell them, offer them on a stock media platform, or
  use them to train or evaluate machine-learning models.
extra_gated_fields:
  I will use the videos for research and reproduction only, as described above: checkbox
extra_gated_button_content: Accept and download
---

# ReplayLab LEO NTN ABR runs

60 adaptive bitrate (ABR) video streaming sessions simulated in ns-3 over a 5G
non-terrestrial network: a LEO satellite gNB at 550 km backhauls an integrated access and
backhaul (IAB) node that serves three UEs. Four ABR algorithms stream the same five videos
over feeder links of 9, 18 and 36 Mbps.

Each run is a bundle: what the player requested and showed, segment by segment, its stalls,
the network samples it measured, the simulation and policy logs, and the encoded video
segments it played, with the VMAF, SSIM and PSNR of every segment at every quality. The
bundles can be replayed and compared side by side in
[ReplayLab](https://github.com/muhammadadeelzahid/replay-lab).

`runs.csv` has one row per run with its QoE: average bitrate, switches, startup delay,
stalls, stalled time and the VMAF of what was on screen.

## Contents

| | |
| --- | --- |
| Runs | 60: 5 videos × 4 algorithms × 3 feeder rates |
| Algorithms | `llm` (an LLM-based policy with a buffer guard), `robustmpc`, `mpc`, `bola` |
| Videos | Big Buck Bunny (4K), A Chef Cooking Foods, Peaceful Autumn Lake with Reflections (4K), Pexels videos 30886101 and 34797065 |
| Feeder link | 9, 18 and 36 Mbps, shared by three UEs |
| Quality ladder | 250 kbps (240p) to 12 Mbps (2160p), six rungs |
| Size | about 8.5 GB, 9,000 files (most video segments repeat across runs of the same video) |

## Layout

```
runs.csv                        one row per run
runs/<video>_<algorithm>_<rate>Mbps_run_<n>/
  run.json                      scenario, policy (name, family, parameters), network, player
  summary.json                  QoE as computed by the exporter
  segments.jsonl                one record per segment request: times, rung, bytes, decision
  samples.jsonl                 throughput, RTT and loss samples
  player_events.jsonl           stalls, from the simulated player in sim.log
  logs/sim.log                  ns-3 log of the session
  logs/policy.log               the LLM policy's decisions (llm runs only)
  videos/<video>/video.json     the video: title, source, licence, ladder, frame rate
  videos/<video>/segments.jsonl size, VMAF (mean and minimum), SSIM and PSNR per segment and rung
  videos/<video>/r<rung>/seg_<n>.mp4   the encoded segments (H.264, 2 s)
```

## Using the runs

Download the dataset (after accepting the terms) and open the `runs` folder in ReplayLab:

```sh
hf download AdeelZahid/replaylab-leo-ntn-abr --repo-type dataset --local-dir replaylab-runs
```

The files are plain JSON, JSON Lines, text logs and MP4, and can be read without ReplayLab.

## Things to know

- **Runs of the same video, rate and run number share a seed but are separate closed-loop
  simulations.** They see the same network up to the first ABR decision; after that each
  algorithm's requests change the load on the shared feeder link, so differences between
  them are not due to the algorithm alone.
- **One UE per bundle.** Each simulation had three UEs; the bundle holds the one that
  received the whole video (re-indexed as UE 0).
- **Rung shift.** In `segments.jsonl` the rung of record *n* is the rung chosen for segment
  *n + 1*; segment 0 plays at the lowest rung. ReplayLab applies this when it reads a bundle.
- **3.3 s player segments.** The encoded segments are 2.0 s long (the first segment of Big
  Buck Bunny is 4.0 s), but the simulated player buffered in 3.3 s units. Simulation times are
  copied as they are.
- **Stalls are many short pauses.** The simulated player resumes on single frames, so one
  freeze is logged as many stalls. `runs.csv` counts stall episodes (pauses less than 1 s of
  playback apart merged) and gives the raw count as `player_pauses`.
- **Big Buck Bunny is partial.** Every Big Buck Bunny session stops at about 151 s, when the
  satellite pass ends, at the same point for every algorithm (`cut_off` in `runs.csv`).
- **Edited logs.** Paths of the computing cluster in the logs are replaced by `$HOME`,
  `$PROJECT` and `$SCRATCH`. Nothing else in the logs was changed.

## Licences

- Simulation outputs (logs, JSON, JSON Lines, `runs.csv`): [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- *Big Buck Bunny*: © Blender Foundation, [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/), www.bigbuckbunny.org.
- The other four videos: from [Pexels](https://www.pexels.com), under the
  [Pexels License](https://www.pexels.com/license/). The segments here are re-encoded at six
  qualities for the simulation. They are shared for research and reproduction; do not sell
  them, offer them on a stock media platform, or use them to train or evaluate
  machine-learning models.

See `LICENSE` for the full terms.

## Citation

A paper describing the simulations is in preparation. Until it is published, please cite
this dataset by its Hugging Face page.
