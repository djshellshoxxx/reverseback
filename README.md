# ReverseBack

A playful local audio toy for reversing your voice, live audio chunks and audio files.

**Status: specification and GUI mockup only. No application or binaries implemented yet.**

## Modes

- **Record & Reverse:** record for a chosen time, wait, then hear the complete take backwards. Five seconds of recording plus two seconds of wait starts playback at seven seconds.
- **Live Reverse:** continuously reverse completed chunks, with adjustable chunk size and additional delay.
- **Reverse File:** load WAV, AIFF or FLAC, choose a region, audition it and export.

## Extras

Forward/backward comparison, hold-to-record, replay, loops, ping-pong, tape-speed changes, live freeze, voice triggering, countdown, reversible silence trimming, presets and WAV export.

## Design documents

- [Product, engine and GUI specification](spec/REVERSEBACK_V1.md)
- [Main-screen GUI mockup](docs/GUI_MOCKUP.svg)

Initial target: Windows standalone, with a reusable engine for a later VST3 effect and other desktop platforms. License and pinned framework dependencies must be selected before implementation/distribution.
