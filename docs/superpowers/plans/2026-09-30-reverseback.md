# ReverseBack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build the Windows standalone ReverseBack application defined in the approved V1 specification.
**Architecture:** A framework-free C++20 library owns sample processing and frame-based transport. A JUCE desktop wrapper provides devices, asynchronous file services and the GUI; the audio callback consumes prepared buffers and bounded commands.
**Tech Stack:** C++20, CMake 3.22+, CTest, JUCE (exact release and license verified during dependency setup), Windows Visual Studio 2022.
**Spec:** spec/REVERSEBACK_V1.md
**Execution:** Native implementation in this session, sequentially; no delegated workers.

## Global Constraints

- Windows standalone first; VST3 is a later wrapper.
- float32 audio, 64-bit frame counters; mono/stereo only.
- No callback allocations, file I/O, logging, decode or blocking mutexes.
- Capture 0.25–60 s; wait 0–30 s; Live chunk 0.1–5 s.
- Capture defaults 5 s, wait 2 s; Live startup W + D, with device buffering additional.
- Take storage cap 256 MiB; decoded files cap 3 GiB; disk-backed cache above 256 MiB.
- Live speed fixed at 1×; clip speed 0.5–2×; fades never change duration.
- Local processing; Ready and monitoring Off on launch; no automatic capture or audio persistence.
- Default output −12 dB, peak protection −1 dBFS; export excludes output gain/monitor/protection.
- Preserve prior completed assets on failed capture/load/export; never silently clip PCM exports.

## Review Focus

- Commands arriving inside a callback: schedule at frame boundaries without duplicate starts (Tasks 2/3/5).
- Separate input/output devices and changed rates: stop and rebuild without destroying retained takes (Task 5).
- Large/corrupt files and insufficient disk: bounded cache, cancellation and retained prior asset (Task 4).
- Keyboard focus loss and held keys: no accidental recording while editing and no stuck hold (Task 6).
- Rapid asset replacement during preview/export: immutable ownership and safe reclamation outside callback (Tasks 4/5).

## File map and shared interfaces

Source/Core/AudioClip.h: immutable planar audio with sampleRate, channels and frameCount.
Source/Core/ClipPlayer.{h,cpp}: reverse/forward clip preview, loop and ping-pong.
Source/Core/RecordTransport.{h,cpp}: exact capture/wait/playback scheduler.
Source/Core/LiveTransport.{h,cpp}: fixed-capacity chunk scheduling/freeze.
Source/Core/SignalTools.{h,cpp}: gain smoothing, envelopes, trigger and trim selection.
Source/Core/CommandQueue.h: bounded single-producer/single-consumer queue.
Source/IO/{FileService,ExportService,SettingsStore}.{h,cpp}: worker-thread services.
Source/Audio/AudioDeviceAdapter.{h,cpp}: JUCE callback, device lifecycle and output protection.
Source/UI/{MainComponent,WaveformView,AdvancedPanel,AudioSettingsPanel}.{h,cpp}: GUI.
Source/Main.cpp: application entry.
Tests/Core/*.cpp, Tests/Integration/*.cpp: CTest executables using real processing.
CMakeLists.txt and .github/workflows/build.yml: portable core tests and Windows build/artifacts.

Shared types: Frame = uint64_t; Mode = Record/Live/File; Direction = Forward/Reverse;
LoopPattern = Once/Loop/PingPong; Selection { Frame begin, end; } (end exclusive).
AudioClip::sample(channel, frame) returns float; AudioClip instances become immutable before publication.
Transport::process(const float* const* input, float* const* output, size_t frames) processes one block.
Settings hold validated seconds/dB; prepare converts timing into integer frames outside callback.
Snapshot contains state, frame positions, meter values and bounded error code; UI reads a published snapshot.
Prepared resources transfer through fixed-capacity queues; retired resources are reclaimed by the non-audio owner.

## Task 1: Exact clip processing and build foundation

Files: CMakeLists.txt; Source/Core/AudioClip.h; ClipPlayer.{h,cpp}; SignalTools.{h,cpp}; Tests/Core/ClipTests.cpp.
Interfaces: ClipPlayer::prepare(const AudioClip&, Selection, Direction, double speed, LoopPattern, Frame fadeFrames);
ClipPlayer::process(float* const* output, size_t frames); ClipPlayer::stop();
Selection findNonSilentSelection(const AudioClip&, double thresholdDb, double windowSeconds, double paddingSeconds).
- [ ] Write failing CTest cases for A01/A11: [1,2,3,4] -> [4,3,2,1], independent stereo and double reversal.
- [ ] Configure/build/run; verify missing behavior fails before implementation.
- [ ] Implement immutable clip access and exact 1× reader; add bounded windowed-sinc speed conversion with documented endpoint clamping.
- [ ] Add failing A10/A12 tests for output count round(N/speed), selections, 0.5×/2×, fades, loops and ping-pong at endpoints; implement.
- [ ] Add failing A19 tests for silence trimming/undo selection, silent audio and 50 ms padding; implement selection analysis off-thread.
- [ ] Run cmake --build build and ctest --test-dir build --output-on-failure; commit the passing core.

## Task 2: Record transport and voice trigger

Files: RecordTransport.{h,cpp}; CommandQueue.h; Tests/Core/RecordTests.cpp.
Interfaces: RecordTransport::prepare(double sampleRate, unsigned channels, const RecordSettings&);
start(bool held); finishCapture(); stop(); process(...); snapshot(); retainedTake().
RecordSettings defines captureSeconds, waitSeconds, countdownSeconds, autoStart, thresholdDb, repeatGapSeconds.
- [ ] Write failing A02/A03/A07 tests: capture 240000 frames, wait until frame 336000, output 240000; block sizes 1/64/127/256/512/1024 produce identical timeline.
- [ ] Verify failure; implement frame-based recording/wait/playback with prepared take buffers and retained prior take.
- [ ] Add failing A06/A08 cases for Stop in every state, early finish, <50 ms cancellation and hold limit; implement.
- [ ] Add failing A18 tests: 50 ms sustained threshold, 200 ms available pre-roll included within total duration, quiet input remains Armed.
- [ ] Implement trigger/countdown/repeat scheduling; exclude concurrent Record capture/playback and enforce Repeat/Loop exclusivity.
- [ ] Run full CTest suite with allocation instrumentation around process(); commit.

## Task 3: Bounded Live reverse and Freeze

Files: LiveTransport.{h,cpp}; Tests/Core/LiveTests.cpp.
Interfaces: LiveTransport::prepare(double sampleRate, unsigned channels, Frame chunkFrames, Frame delayFrames);
start(); requestFreeze(); resume(); stop(); process(...); snapshot(); frozenChunk().
- [ ] Write failing A04 tests for first output frame 120000 at 48 kHz, W=.5/D=2, reversed chunks in chronological order, and D=0 boundaries.
- [ ] Verify failure; implement ceil(D/W)+4 prepared immutable chunk slots and frame timestamps.
- [ ] Add failing A09 tests for mid-chunk Freeze, next output-boundary adoption, queue flush, repeat and fresh Resume fill; implement.
- [ ] Add A05 long-run simulation for 60 minutes, maximum delay/minimum chunk, constant capacity and slot ownership; force queue failure and verify silent stopped error.
- [ ] Add A06/A12 tests for Stop/edge fades and non-divisible callback sizes.
- [ ] Run full CTest suite and callback allocation checks; commit.

## Task 4: File import, cache, export and settings

Files: Source/IO/*.h/cpp; Tests/Integration/FileTests.cpp; SettingsTests.cpp.
Interfaces: FileService::load(path, cancellation, completion); ExportService::render(asset, Selection, ExportSettings, cancellation, completion);
SettingsStore::load(); save(Settings); savePreset(name, Settings).
Completion returns either an immutable asset/prepared resource or a typed error; all callbacks are delivered to the UI thread.
- [ ] Verify/pin JUCE dependency and record license decision before distribution; add optional GUI build so core tests remain dependency-free.
- [ ] Write failing WAV/AIFF/FLAC import tests for source rates 44.1/48/96/192 kHz and stereo; reject multichannel/over-limit/corrupt inputs.
- [ ] Implement cancellable decode and source-time selection; >256 MiB decoded files use a 3 GiB-limited disk PCM cache and bounded reverse prefetch.
- [ ] Add failing A14 tests for cancellation, disk-full, prefetch underrun and prior-asset preservation; implement typed errors and safe cache cleanup.
- [ ] Add failing A10/A11/A20 export tests for float sample order/over-unity values, PCM overflow refusal, optional normalization/dither and source-rate selection.
- [ ] Implement destination temporary file plus atomic rename where supported; overwrite requires UI consent; output volume never enters offline render.
- [ ] Add A17 settings tests for corrupt/versioned preferences, atomic save, named presets and monitor Off on restart.
- [ ] Run full CTest suite; commit.

## Task 5: Audio device integration and thread boundary

Files: Source/Audio/AudioDeviceAdapter.{h,cpp}; Tests/Integration/DeviceAdapterTests.cpp.
Interfaces: prepareDevice(DeviceSettings); post(Command); snapshot(); stopForDeviceChange();
device callbacks adapt planar input/output to the core transports and never release the last asset reference.
- [ ] Write failing integration tests for input channel mapping, mono-to-stereo, gain smoothing over 20 ms, non-finite sample sanitization and inactive transport silence.
- [ ] Implement bounded commands/snapshots and deferred retirement outside callback; test rapid replacement with active preview/export.
- [ ] Implement output-only peak protection at −1 dBFS; measure/document its buffering latency and test ceiling/no makeup gain.
- [ ] Add A13/A15 cases for disconnect, denied microphone, changed sample rate, separate devices, rapid commands and GUI stall; retained takes survive.
- [ ] Verify direct monitor defaults Off and mutes in Record playback; permit File mode without microphone permission.
- [ ] Run full CTest suite and sanitizer/core stress checks; manually test an actual Windows input/output device and record limitations; commit.

## Task 6: Complete GUI and Windows packaging

Files: Source/Main.cpp; Source/UI/*.h/cpp; .github/workflows/build.yml; docs/IMPLEMENTATION_STATUS.md; README.md.
Interfaces: MainComponent owns UI models and services, posts bounded commands and renders snapshots; transport changes never originate from repaint.
- [ ] Add A16 GUI integration tests that exercise real transport state/commands for each main control and Advanced control.
- [ ] Build the 960×700/minimum 820×620 interface from docs/GUI_MOCKUP.svg with all three modes, waveform/selection, meters, status and persistent device errors.
- [ ] Wire hold/focus-loss, countdown/voice arming, replay/direction, loops/ping-pong/speed, Freeze/Resume, trimming/undo and export summary.
- [ ] Test keyboard editing focus, key-repeat and disabled shortcuts; implement Space/R/F/Escape/Ctrl+O/Ctrl+S/H with equivalent UI/menu actions.
- [ ] Wire presets/Surprise/user presets; verify they never auto-record or randomize gain/devices; implement accessibility labels and reduced motion.
- [ ] Add asynchronous progress/cancel/retry and Audio Settings with actual device rate/buffer values plus explicit test sound.
- [ ] Configure Windows CI to build application and tests and upload artifacts only after tests pass; retain Linux core-test job.
- [ ] Run full suite, perform five-minute novice flow and Windows device/GUI smoke test, update A01–A20 status with evidence and unresolved gaps.
- [ ] Commit and publish the implementation branch; report build artifacts and actual verification without claiming untested Windows device behavior.

## Completion rules

All A01–A20 must have evidence before describing V1 as complete. If Windows hardware testing cannot run in the execution environment, clearly mark it pending. No mockup-only control counts as implemented. A successful core test run is not proof of a working microphone or desktop GUI.
