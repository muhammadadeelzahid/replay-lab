# What the viewer needs from the ns-3 side

ReplayLab plays one ABR run on its own, or several runs side by side, so that the picture
quality of different ABR algorithms can be compared. This page lists what the bundles have to
contain for that. How the data is produced is up to the ns-3 side; the field layout is in
[bundle-format-v2.md](bundle-format-v2.md).


## 1. Experiments: runs that belong together

- An **experiment** is one video streamed at one feeder link rate by several ABR algorithms.
- The runs of an experiment must be comparable: the same video, the same ladder, the same
  network setup (topology, feeder rate, number of users) and the same seed.
- The experiment says how far the network conditions were shared between its runs, as one of:
  - `"same"`: every run saw the same capacity over time;
  - `"independent"`: the runs are separate simulations with the same settings, so the network
    may differ between them.
- Each run says which ABR algorithm it used: a short key (`llm`, `fdash`, `svaa`, …), the
  ns-3 client name, the family (learned, rate, buffer or hybrid) and any parameters.


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

- **Algorithms:** the LLM policy and the classic clients (FDASH, SVAA, SFTM, RAAHS, AAASH,
  OSMP, BOLA), or whichever of them are available.
- **Videos:** the five in the current bundles.
- **Rates:** 9, 18 and 36 Mbps.

A first delivery of one video at one rate, with 2 to 4 algorithms, is enough to start and to
check the format.
