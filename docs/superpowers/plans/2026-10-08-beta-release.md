# ReverseBack Beta Release Plan (supersedes the Windows-first sequencing of 2026-09-30)

**Goal:** Deliver `0.1.0-beta.1`: the complete V1 feature set as Standalone, VST3 and CLAP for Linux x86_64 (Windows via CI), with a modern GUI, then audit and fix.
**Specs:** `spec/REVERSEBACK_V1.md` (+ amendments), `ENGINE_DESIGN.md`, `PLUGIN_FORMATS.md`, `GUI_DESIGN.md`, `BUILD_RELEASE.md`.
**Execution:** sequential implementation in one session; the audit step uses independent reviewers with no access to the author's conclusions.

## Order of work

1. **Specs** (done first): engine, plugin, GUI, build/release documents.
2. **Core** (`Source/Core`, `Tests/Core`): types, clips, resampler, `ClipPlayer`, signal tools, queues, `RecordTransport`, `LiveTransport`, `Engine`, stages, presets. Tests written with the code, run in Release and under ASan/UBSan/TSan. Allocation hook around `process`.
3. **IO** (`Source/IO`): decode with limits and cancellation, disk cache + prefetch, export with atomic write, settings store. Integration tests with generated WAV/AIFF/FLAC fixtures.
4. **Processor + formats** (`Source/Audio`, `Source/Standalone`): parameters, state, bus layouts, bypass, standalone app, CMake plugin targets, packaging scaffolding.
5. **GUI** (`Source/UI`): look-and-feel, components, three modes, drawer, sheets, shortcuts; Xvfb screenshots and control tests.
6. **Audit:** sanitizers, stress tests, independent reviews (real-time safety, threading and ownership, DSP correctness, UI behaviour), fix, regression tests.
7. **Release:** Release build, validators, packaging, CI workflow, `docs/IMPLEMENTATION_STATUS.md` with evidence, README, user-facing build script.

## Completion rules
Unchanged from the 2026-09-30 plan: nothing is "complete" without evidence; absent hardware (real microphones, Windows machines, commercial DAWs) is reported as unverified; no mockup-only controls; no silent clipping; no automatic capture.
