# ReverseBack 1.0: product, audio engine and GUI specification

Specification date: 2026-09-30. Status: proposed implementation contract. This document defines the requested tool; it does not claim the tool exists.

## 1. Purpose and scope

ReverseBack is a local audio toy for hearing speech, music and other sounds backwards. A person should be able to open it, choose their microphone, press one button, speak, and hear the result without knowing audio engineering. It also serves musicians making reverse samples.

V1 is a Windows standalone desktop application. Design the audio engine without UI dependencies so a later VST3 stereo effect can share it. macOS/Linux and the plugin wrapper are follow-up deliverables, not requirements for the first standalone release. Processing, recording and file conversion run locally; no account, cloud upload, speech recognition or AI voice generation is required.

Three modes are mandatory: Record & Reverse, Live Reverse, and Reverse File. Preserve pitch and duration at normal speed. Reverse waveform samples, not word order or transcribed text. Stereo channels retain their identities and share the same time index.

## 2. Core mode behavior

### 2.1 Record & Reverse

User chooses recording length T and playback wait D. Pressing Start captures exactly round(T × sampleRate) frames after the optional countdown. Once capture ends, wait round(D × sampleRate) frames, then play the recorded clip backwards once.

Example: T = 5 seconds and D = 2 seconds, countdown off. Capture from 0 to 5 seconds; wait from 5 to 7; reverse playback runs from 7 to 12. The original final sample is the first sample played. Silence is retained. No automatic silence trimming or word detection may alter this example.

Default behavior ignores microphone input during Wait and Playback, disables direct microphone monitoring, and completes in Ready with the take retained. Replay does not record a new take. Record Again replaces the take only once recording begins successfully. Repeat Session runs capture → wait → playback → ready gap → capture until stopped, with a 0.5-second default ready gap and no concurrent capture/playback.

Hold to Record is an alternative to the timed capture. Pointer/key down starts capture; release closes the clip and starts the selected wait. Enforce the same maximum recording duration. A short tap producing less than 50 ms cancels with “Hold longer to record.” Losing focus releases the hold; a device failure cancels capture. During capture, Finish Early uses the recorded frames if at least 50 ms are present. Stop discards an unfinished capture and clears pending playback, while preserving the previous complete take.

### 2.2 Live Reverse

Continuous reverse audio must first collect a finite chunk. Capture successive non-overlapping chunks of length W, reverse each complete chunk, and play them in capture order. This reverses audio within each chunk; it does not reverse the chronological order of the entire conversation.

Additional delay D is measured from each chunk's completion to its output start. At speed 1×, chunk k captures [kW, (k+1)W) and plays [(k+1)W + D, (k+2)W + D). First output begins W + D seconds after Start, plus device/output buffering. With W = 0.5 s and D = 2 s, first output begins at 2.5 s and subsequent reversed chunks play continuously.

The source age varies across a reversed chunk. At position u within its playback, its source timestamp is approximately (k+1)W − u and output timestamp is (k+1)W + D + u, giving source age D + 2u. Consequently W + D is startup latency, not a constant delay for every sample. GUI labels must make this distinction without requiring the formula.

During startup, output silence with a Filling Buffer progress indicator. Capture continues during playback in this mode. A delay by itself does not prevent acoustic feedback. Show a persistent compact “Headphones recommended for Live Reverse” hint. There is no internal feedback/recirculation path in V1. Input monitoring defaults off in every mode.

Live runs indefinitely using bounded buffers. Stop fades output and empties live buffers. Freeze finishes the current capture chunk, retains that completed chunk, stops microphone capture, and repeats it until Resume; Resume starts fresh capture and a new buffer fill. Export Frozen Chunk writes that complete chunk. Live speed is fixed at 1× in V1 to avoid uncontrolled backlog or missing chunks. Clip speed controls are disabled with an explanation in Live.

### 2.3 Reverse File

Load or drop one WAV, AIFF or FLAC file. MP3/AAC are optional future decoder features, not advertised until shipped and tested. Accept mono/stereo audio, up to 30 minutes, up to 192 kHz, subject to the memory/disk checks below. Unsupported multichannel files get a clear error rather than silent downmixing.

Show source duration, channels and sample rate. Default selection is the whole file. User may drag the selection handles or type start/end times; enforce start < end and at least 50 ms. Play Selection previews forward; Reverse plays the same selected frames backwards. Direction toggle changes playback direction at the current position with a short fade. Play from Start explicitly restarts at the appropriate selection boundary.

For microphone takes, exports and previews use the capture rate. For files, map selection times to source frame indices; reverse source frames before resampling for device playback. Export uses the source rate unless the user chooses 44.1 or 48 kHz. Never overwrite an imported file implicitly.

## 3. Fun and useful additions

| Feature | Behavior | Location |
| --- | --- | --- |
| Forward / Backward | Compare the retained take or file selection in either direction; labels always show direction. | Main |
| Replay | Replay the latest complete take without recording. | Main |
| Loop | Repeat a retained clip or file selection until stopped. | Main |
| Ping-pong | Alternate full forward and full reverse passes; no new recording. | Advanced |
| Speed | 0.5×–2× clip playback, including 0.5, 0.75, 1, 1.5 and 2× buttons. Tape-style pitch changes with speed. Export duration is source duration / speed. | Advanced |
| Freeze | Turn a live reversed chunk into a repeating sound; Resume returns to live input. | Live main |
| Hold to Record | A large push-to-talk button and keyboard shortcut for short experiments. | Record main |
| Countdown | Off, 1, 3 or 5 seconds; silent visual countdown by default so a beep cannot enter the recording. | Advanced |
| Auto Start on Voice | Arm until the microphone crosses a threshold, then record the configured duration. | Advanced |
| Trim Silence | Explicitly trim leading/trailing silence on a retained take; Undo restores the original. Never applied by default. | Advanced |
| Save WAV | Export the retained clip, file selection or frozen chunk with current direction/speed; choose destination. | Main |
| Surprise Settings | Pick sensible clip speed, direction pattern and loop settings; never randomize device, gain, recording permissions or start recording. | Advanced |
| Session presets | Quick defaults for voice, tiny syllables and long phrases; save named settings locally. | Main |

Presets: Say Something = Record, 5 s capture, 2 s wait, 1×, no repeat; Tiny Syllables = Live, 250 ms chunks, 250 ms extra delay; Backwards Conversation = Live, 1 s chunks, 500 ms extra delay; Long Phrase = Record, 10 s capture, 2 s wait. Preset selection stops active processing, applies settings and returns to Ready; it never automatically records. Surprise Settings applies only to a stopped clip mode and leaves the file selection/capture length unchanged.

Voice auto-start uses a configurable −45 dBFS default threshold, −65 to −15 dBFS range, 50 ms sustained crossing, and a 200 ms pre-roll ring. Captured clip includes pre-roll and has exactly the configured total duration; the triggered post-roll is total minus available pre-roll. Armed state lasts until Cancel or trigger. Do not imply background noise and speech are reliably distinguishable.

Trim Silence uses 20 ms RMS windows, a −50 dBFS default threshold, and 50 ms padding around detected non-silent audio. It creates a reversible selection, not destructive edits. If no non-silent region is detected, retain the original and show “No speech or sound detected.”

## 4. GUI specification

### 4.1 Structure

Minimum window 820 × 620 logical pixels; default 960 × 700. Resizable with OS scaling, readable labels and keyboard focus. The interface is cheerful and restrained: large transport controls, one accent color per current mode, clear waveforms, modest animation. No decorative animation on the audio thread.

Top bar: ReverseBack name, mode tabs, Settings gear. Immediately below: microphone/file source strip and input level meter. Center: waveform/selection panel and prominent status text. Bottom: mode controls, primary transport, Replay, Direction, Loop and Save. A collapsed Advanced drawer reveals optional controls. Device errors appear inline near the source strip with Retry and Settings actions, rather than recurring modal dialogs.

Record layout: “Record for [5.00 s]” and “Wait before playback [2.00 s]”, Start button, Hold to Record button, Repeat Session switch. Show “Ready”, “Recording — 3.2 s left”, “Waiting — 1.4 s”, or “Playing backwards”. Recorded waveform draws left to right; reverse playhead travels right to left. Default Start label is “Record & Reverse”.

Live layout: “Reverse chunk [500 ms]”, “Extra delay [2.00 s]”, computed “First sound after 2.50 s + device buffering”, Start/Stop and Freeze/Resume. Display the currently playing chunk waveform and a small capture progress indicator. Never label the total as an exact fixed per-sample delay.

File layout: drop target/Open File, filename, source information, waveform, selection handles, start/end numeric inputs, Play/Stop, Direction, Loop and Save WAV. Recording controls disappear. A file remains selected when returning from another mode, but playback always stops on a mode change.

Common transport: one unmistakable Stop button while busy. Save/Replay disabled until a complete asset exists. A retained microphone take remains available after changing mode. Settings and About are separate from sound controls.

### 4.2 Control contract

| Control | Range/default | Change behavior |
| --- | --- | --- |
| Capture length | 0.25–60 s; default 5 s; 0.05 s UI step, numeric entry supported | Disabled while recording; applies next capture |
| Playback wait | 0–30 s; default 2 s; 0.05 s UI step | Applies next capture/live restart |
| Live chunk | 0.1–5 s; default 0.5 s; 0.01 s step | Changed while stopped; disabled while Live runs |
| Input gain | −24 to +24 dB; default 0 dB | Smoothed over 20 ms; affects new captures only |
| Output volume | −60 to 0 dB; default −12 dB | Smoothed over 20 ms; preview only |
| Edge fade | 0–10 ms; default 3 ms | Clip boundaries; live uses separate 2 ms default boundary fade |
| Speed | 0.5–2×; default 1× | Retained clips only; changing while playing fades/restarts at mapped position |
| Loop pattern | Once / Loop / Ping-pong; default Once | Applied at next playback boundary |
| Input monitor | Off by default; 0–100% when enabled | Advanced; standalone only; immediately smoothed |
| Tail gap | 0–2 s; default 0.5 s | Repeat Session only; next cycle |

Advanced groups: Recording (countdown, auto-start threshold, repeat gap), Playback (speed, ping-pong, fade), Audio (input gain, input monitor), and Take Tools (trim/undo). Group visibility follows the mode. Output volume remains on the main screen.

Audio Settings: input device, input channel choice, output device, supported sample rate and buffer size; display actual configured values after opening the device. Prefer the OS default devices, 48 kHz if supported, and 256-frame buffers if supported. Fall back to working device defaults. Do not imply a 256-frame buffer is universally available. Provide an output test sound at the current volume; never run it automatically.

### 4.3 Accessibility and interaction

Space toggles Start/Stop when focus is outside text inputs. R replays, F toggles direction, Escape stops/cancels, Ctrl+O opens a file, Ctrl+S opens export. Hold-to-record uses a configurable H key with key-repeat ignored. All shortcuts have menu equivalents and can be disabled. Prevent accidental recording while typing a filename or number.

Use text alongside state colors. Meters and countdowns have accessible summaries without flooding screen readers. All sliders accept numeric entry. Touch targets at least 40 logical pixels. Honor reduced-motion preferences. The waveform is informative but never the only way to select an interval.

## 5. Audio engine

### 5.1 Modules and ownership

AudioDeviceAdapter handles standalone device input/output. TransportController owns the state machine and schedules events by audio frame count. CaptureEngine writes preallocated take/live buffers. ReversePlayer reads immutable audio in descending frame order. FileService decodes and resamples off-thread. ExportService renders offline and atomically writes files. SettingsStore handles versioned preferences. UI observes compact state snapshots and meter summaries.

Use C++20 with CMake. JUCE is a proposed device/UI/file framework; pin an exact compatible release during implementation and review its current license before distributing. No framework dependency is needed to understand or test the core reversal/scheduling logic. A separate framework-free DSP/transport library is preferred.

### 5.2 Exact reversal and boundaries

For N frames, channel c, normal speed reverse reads y[c,n] = x[c,N−1−n], n = 0…N−1. Preserve channel order. “Exact” means exact sample order at 1× before optional edge fades, output gain and device conversion. Default 3 ms fade changes a small number of boundary amplitudes to suppress clicks. Provide Exact Samples (fade off) for offline export; keep the distinction explicit in the UI tooltip and tests. At 1× with fades off and source-rate float WAV export, the engine must produce an exact frame reversal within the file encoding's precision.

For speed changes use a bounded, tested resampler; avoid naïve linear interpolation as the final quality implementation. Higher speed raises pitch and shortens duration; pitch-preserving time stretching is outside V1. Export output frame count is round(N / speed), with documented endpoint clamping and consistent offline/playback mapping.

V1 Live uses independent reversed chunks with short fade-out/fade-in envelopes contained inside each chunk. No overlap shortens or changes the scheduler's W-second intervals. This prevents abrupt discontinuities at the cost of brief amplitude dips. Describe it as “Smooth edges,” not perfect seamless speech. Exact Samples disables those fades. Overlapping grain synthesis can be explored later as a separate audible mode with its own timing contract.

### 5.3 State machine

Record states: Ready → Countdown(optional) → Armed(optional) → Recording → Waiting → Playing → Ready. In auto-start with countdown, countdown precedes Armed. Repeat Session substitutes ReadyGap → Recording after Playing, with countdown only before the first cycle and auto-start only before the first triggered capture. Loop and Repeat Session are mutually exclusive: enabling one disables the other with a visible state update.

Live states: Ready → Filling → Running → Frozen; Resume → Filling. Freeze completes the in-progress capture and begins repeating it at the next output boundary, flushing queued live chunks; show “Freezing…” until then. Stop is accepted in every non-Ready state and transitions through a short output fade to Ready. File states: Empty → Loading → Ready → Playing → Ready; Loading can be cancelled. Error transitions silence output and preserve the last valid complete take/file.

Count time using integer audio frames, not wall-clock timers or GUI ticks. Never start playback before capture completion plus wait. Device latency is additional and not included in the user wait control. Test events whose boundaries land inside a callback block.

### 5.4 Memory and thread rules

Use float32 internal audio and 64-bit frame counters. Allocate outside the callback; no callback heap allocations, file I/O, logging, blocking mutexes or file decode. Use bounded message queues and snapshot publication. All source buffers read by playback are immutable for that playback lifetime.

Maximum 60-second stereo take at 192 kHz needs approximately 88 MiB for one buffer; preserving the previous take plus a capture-in-progress doubles that. Check memory budget before starting and reserve storage off-thread. Cap active take storage at 256 MiB; fail cleanly when allocation is unavailable.

Live slots are fixed-size immutable chunks, with capacity ceil(D/W) + 4 at normal speed, bounded using the supported W/D limits. Store capture and scheduled output timestamps with each slot. Never overwrite a slot still scheduled or playing. If capacity or consumer progress fails, stop with “Audio processing fell behind”; do not silently emit stale audio or grow a queue. Control commands cannot allocate in the callback; a prepared configuration is adopted only at a safe boundary.

Files up to 256 MiB decoded float data may use RAM. Larger supported files use a cancellable disk-backed PCM cache populated off-thread, with prefetched playback blocks and a bounded cache. Enforce a 3 GiB decoded-file limit and available-disk check; loading succeeds only after required playback data is prepared. Prefetch underrun fades to silence and pauses with Retry, never blocks the callback. Closing removes temporary caches; startup cleans abandoned caches belonging to ReverseBack. Original files are never removed.

The Live “save” action exports only a completed frozen chunk. Full-session Live recording is outside V1; this avoids an unbounded recorder or ambiguous stream export.

### 5.5 Signal path

Input → selected channel mapping → smoothed input gain → capture. Stored float audio may exceed unity; display overload and preserve it without adding hidden normalization. Reverse reader → optional clip resampling → boundary envelopes → output gain → optional direct monitor sum → output peak protection.

Apply transparent peak protection at the device output only, with a −1 dBFS ceiling and no makeup gain. Specify and test its latency during implementation, and show added buffering in device status. The default wet signal has no dry mix. Mono microphone input goes equally to left/right playback; stereo files preserve stereo. Direct monitoring, if enabled, comes before output protection and is muted during Record-mode playback.

Export excludes output volume, direct monitor and device limiter. Export is 32-bit float WAV by default, with optional 24-bit PCM and dither; offer Peak Normalize to −1 dBFS as an explicit option, off by default. If PCM export would clip, offer float export or normalization before completing; do not silently hard-clip. Include direction, speed, selection and fade settings in an export summary.

## 6. Reliability, privacy and persistence

Microphone permission denial shows the cause and a route to OS settings; file mode remains usable. Disconnection immediately silences affected output and stops transport, retaining complete takes. Reconnection does not resume recording automatically. Sample-rate/device changes stop transport, clear Live buffers and rebuild prepared storage outside the callback.

Missing/corrupt/unsupported files fail before replacing the currently valid file. Export writes a temporary file in the destination directory, closes it, then renames it atomically where supported. Existing destinations require explicit overwrite confirmation. Show cancelable progress for decoding/export. Disk-full and permission errors preserve the in-memory take and offer a new destination.

Reject non-finite samples at decoder/capture boundaries and replace them with silence while recording a bounded diagnostic count. A DSP fault silences output and stops transport. UI stalls do not halt the engine. Diagnostic logs contain device/errors/timing counters, never raw audio. No telemetry by default.

Persist versioned settings and user presets atomically. Do not persist microphone recordings or imported audio automatically. Relaunch in Ready with monitoring off. Never auto-start microphone capture after launch, crash recovery or preset selection. Store last folder and device choice; file history is optional and clearable.

## 7. Acceptance tests

| ID | Test and pass criterion |
| --- | --- |
| A01 | Reverse [1,2,3,4] becomes [4,3,2,1] with fades/gains disabled. Stereo channel identities remain unchanged. |
| A02 | 5 s capture + 2 s wait at 48 kHz captures 240,000 frames, starts playback at frame 336,000, and outputs exactly 240,000 frames. |
| A03 | Scheduler correctness for block sizes 1, 64, 127, 256, 512, 1024; transitions occur at the same frame indices. |
| A04 | Live W=0.5 s, D=2 s starts output at frame 120,000 at 48 kHz; chunks reverse independently and remain in chronological chunk order. |
| A05 | Run Live for 60 minutes; bounded memory and queue capacity, no unexplained missing/repeated chunks or growth. |
| A06 | Stop during every state emits no later queued playback; outputs reach silence within the selected short stop fade plus device buffering. |
| A07 | Record ignores microphone data during wait/playback; Repeat Session starts a fresh capture only after playback plus ready gap. |
| A08 | Hold release/focus loss/limit closes a valid take; key repeat cannot start additional captures. Short taps do not replace a prior take. |
| A09 | Freeze safely adopts a complete chunk at a boundary, loops it, exports it, and Resume fills a fresh live stream. |
| A10 | Selection/export agrees at source rate, including mono/stereo, non-integer duration, 44.1/48/96/192 kHz and 0.5×/2× speeds. |
| A11 | Normal speed export with Exact Samples yields reversed sample order; double reversal reconstructs the original within encoding precision. |
| A12 | Fades never change duration; no out-of-range reads at short selections, loops or ping-pong turns. |
| A13 | Missing device, permission denial, disconnect and rate change stop safely and preserve complete takes. |
| A14 | Corrupt file, cancelled decode, cache underrun and disk-full export leave previous valid assets intact. |
| A15 | Instrument callback allocation/blocking checks; stress rapid Stop/Start/mode changes and retain immutable-buffer ownership. |
| A16 | Every GUI control changes the intended engine property; disabled controls explain why; keyboard entry cannot trigger recording accidentally. |
| A17 | Presets/Surprise never start recording or alter gain/devices; reset/relaunch restores Ready and monitor Off. |
| A18 | Voice trigger includes pre-roll within total capture duration; quiet input remains Armed and can be cancelled. |
| A19 | Trim is reversible, preserves padding, and handles all-silent takes without destructive edits. |
| A20 | Float export preserves over-unity samples; PCM export cannot silently clip; output volume has no effect on export level. |

Manual user acceptance: an unfamiliar user can select a microphone, make the 5-second/2-second example, replay it forward/backward, load a file, select a region and export in under five minutes without consulting technical documentation.

## 8. Delivery sequence and exclusions

Build the independently testable reversal and frame scheduler first; add Record transport/device UI, then Live bounded-buffer playback, then file selection/export, then fun controls and accessibility. Run targeted engine tests and manual device/GUI tests before releasing binaries. Document actual tested devices and hardware rather than promising a universal latency.

V1 excludes pitch-preserving stretch, true acoustic echo cancellation, automatic feedback identification, unlimited live session recording, multichannel files, cloud services, MIDI, beat sync and plugin-host latency compensation. Delay is not advertised as feedback cancellation. A later VST3 wrapper must explicitly specify host routing, transport behavior, state persistence and latency policy before implementation.

## 9. Suggested repository layout

README.md; spec/REVERSEBACK_V1.md; docs/GUI_MOCKUP.svg; Source/Core; Source/Audio; Source/UI; Source/IO; Tests/Core; Tests/Integration; CMakeLists.txt; .github/workflows. Only documentation and the mockup are delivered with this specification. Implementation should use meaningful tests from section 7 and avoid tests that merely duplicate setter code.
