# ReverseBack

A playful local audio toy for reversing your voice, live audio chunks and audio files.

**Status: active early development. The framework-free C++20 reversal and frame-scheduling core is implemented with acceptance tests; standalone audio devices and GUI are next.**

## Modes

- **Record & Reverse:** record for a chosen time, wait, then hear the complete take backwards. Five seconds of recording plus two seconds of wait starts playback at seven seconds.
- **Live Reverse:** continuously reverse completed chunks, with adjustable chunk size and additional delay.
- **Reverse File:** load WAV, AIFF or FLAC, choose a region, audition it and export.

## Implemented so far

- C++20 framework-free core library
- Exact per-channel frame reversal
- Frame-count-based Record & Reverse scheduler
- Callback-block boundary event reporting
- Acceptance coverage for A01, A02 and the playback-boundary portion of A03
- CMake/CTest build
- Windows and Linux CI

## Build the current core

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Extras planned

Forward/backward comparison, hold-to-record, replay, loops, ping-pong, tape-speed changes, live freeze, voice triggering, countdown, reversible silence trimming, presets and WAV export.

## Design documents

- [Product, engine and GUI specification](spec/REVERSEBACK_V1.md)
- [Main-screen GUI mockup](docs/GUI_MOCKUP.svg)

Initial target: Windows standalone, with a reusable engine for a later VST3 effect and other desktop platforms. The core intentionally has no JUCE dependency. The desktop framework dependency will be pinned and its license reviewed before application distribution.
