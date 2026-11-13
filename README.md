# abr-viz

A desktop viewer for ns-3 adaptive video streaming simulations. It opens a simulation run
bundle and shows what the simulated user saw, segment by segment: the quality the LLM ABR
policy chose, the experienced video with its stalls, the buffer, throughput, RTT and loss over
time, and the simulation logs.

Built with **C++20**, **Qt 6** and **GStreamer**.

> Status: early development. See [docs/PLAN.md](docs/PLAN.md) for the step-by-step plan.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): design, threading, ownership and RAII
- [docs/DIAGRAMS.md](docs/DIAGRAMS.md): class and threading diagrams
- [docs/PLAN.md](docs/PLAN.md): build steps, design fixes, testing
- [docs/bundle_info.md](docs/bundle_info.md): what the simulation bundles contain

## Requirements

- CMake 3.24+, Ninja, a C++20 compiler (Apple clang 15+, GCC 13+)
- Qt 6.8+ with Widgets, Charts, Multimedia, Concurrent and Test
- GStreamer 1.22+ with the base, good, bad and libav plugins (optional, see below)
- ffmpeg (for the pre-rendered replay engine)

macOS (Homebrew):

```sh
brew install qt cmake ninja gstreamer ffmpeg
```

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/app/abrviz
```

Presets: `debug`, `release`, `nogst` (without GStreamer), `asan` (AddressSanitizer + UBSan),
`tsan` (ThreadSanitizer).

The GStreamer replay engine is built by default. Configure with `-DABR_WITH_GSTREAMER=OFF`
(or use the `nogst` preset) to build only the Qt Multimedia engine.

## Simulation data

The 14 run bundles (about 714 MB) are not stored in git. Download `viewer_bundles.tar` from
the GitHub Releases page and extract it into the repository root as `viewer_bundles/`.

## Credits

- *Big Buck Bunny* © Blender Foundation, licensed under
  [CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/).
- The other test videos are from [Pexels](https://www.pexels.com), under the
  [Pexels License](https://www.pexels.com/license/).

## Licence

MIT, see [LICENSE](LICENSE).
