# Stream Inspector

<img src="app/resources/app-icon.svg" width="96" align="right" alt="">

Replays ns-3 simulations of adaptive video streaming over a satellite 5G network with
integrated access and backhaul (IAB). For each run it shows the video the viewer saw, segment
by segment, with its stalls, the bitrate decisions of the LLM ABR policy, the buffer,
throughput, RTT and loss, and the simulation logs.

Network: a LEO satellite gNB at 550 km (28 GHz, 100 MHz) backhauls an IAB relay, which serves
three UEs. The video server reaches the satellite over a feeder link of 9, 18 or 36 Mbps.

Built with C++20, Qt 6 and GStreamer.

![Replay of Big Buck Bunny at 36 Mbps](docs/images/replay.jpg)

## Features

- Library of runs grouped by video and feeder link rate, with stalls and cut-off playback marked
- Replay of the video as the viewer saw it, with the network path and the segment in flight
- Charts of buffer, throughput, bitrate, RTT, loss and decision delay
- Indexed, filterable `sim.log` and policy log
- QoE report checked against `summary.json`
- Segment timeline shared by all views

| All Runs | Analysis | Report |
| --- | --- | --- |
| ![All Runs](docs/images/library.png) | ![Analysis](docs/images/analysis.png) | ![Report](docs/images/report.png) |

## Replay engines

- **GStreamer** (default): plays the segment files directly.
- **Pre-rendered**: renders the experienced video once with `ffmpeg`, then plays it with Qt
  Multimedia. Used when GStreamer is not available.

Select the engine in Settings or with `--engine gstreamer|prerendered`.

## Performance

Apple M4, release build, Big Buck Bunny at 36 Mbps (4K top rung, 111 s):

| | |
| --- | --- |
| Open the run | 0.11 s |
| GStreamer playback | 30 fps, 60 % of one core |
| Pre-rendered playback | 30 fps, 57 % of one core |
| Render the experienced video | 13.7 s |

## Requirements

- CMake 3.24+, Ninja, a C++20 compiler
- Qt 6.8+ (Widgets, Charts, Multimedia, Svg, Concurrent, Test)
- GStreamer 1.22+ with the base, good, bad and libav plugins (optional)
- ffmpeg

On macOS:

```sh
brew install qt cmake ninja gstreamer ffmpeg
```

## Build

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Other presets: `debug`, `nogst` (without GStreamer), `asan`, `tsan`.

## Run

```sh
open "build/release/app/Stream Inspector.app"
```

From the command line:

```sh
APP="build/release/app/Stream Inspector.app/Contents/MacOS/Stream Inspector"
"$APP" viewer_bundles --bundle big_buck_bunny_llm_36Mbps_run_2 --time 57
```

On Linux the executable is `build/release/app/stream-inspector`.

| Option | Effect |
| --- | --- |
| `--bundle <name>` | open a run of the folder |
| `--time <s>` | move the cursor to a simulation time |
| `--engine gstreamer\|prerendered` | replay engine |
| `--page library\|replay\|analysis\|logs\|report` | view to show |
| `--size WxH` | window size |
| `--screenshot <file.png>` | save a screenshot and quit |
| `--profile <s>` | play for some seconds and print timings |

| Key | Action |
| --- | --- |
| Space | play / pause |
| ⌥← / ⌥→ | back / forward 5 s |
| ⌘[ / ⌘] | previous / next segment |
| ⌘1 … ⌘5 | All Runs, Replay, Analysis, Logs, Report |
| ⌃⌘S / ⌥⌘I | sidebar / inspector |
| ⌘O / ⇧⌘O | open a folder / a run |
| ⌘R | render the video |
| ⌘, | settings |

Command-line tools in `build/release/tools/`: `validate_bundle`, `print_report`,
`render_experienced`.

## Simulation data

The 14 run bundles (about 714 MB) are not in git. Download `viewer_bundles.tar` from the
Releases page and extract it into the repository root as `viewer_bundles/`.

## Documentation

- [Architecture](docs/architecture.md)
- [Diagrams](docs/diagrams.md)
- [Development plan](docs/development-plan.md)
- [Simulation data](docs/simulation-data.md)

## Credits

- *Big Buck Bunny* © Blender Foundation, [CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/)
- Other videos from [Pexels](https://www.pexels.com), [Pexels License](https://www.pexels.com/license/)

## Licence

MIT, see [LICENSE](LICENSE).
