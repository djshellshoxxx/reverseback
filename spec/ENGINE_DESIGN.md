# ReverseBack engine design (implementation contract)

Companion to `REVERSEBACK_V1.md` (the product contract). This document pins every algorithm, rounding rule, ownership rule and state transition the engine needs so the code can be written and tested without further decisions. Where this document and V1 disagree, the "Clarifications" in section 1 are the intended reading. Section numbers in `[V1 §x]` refer to the V1 spec.

All engine code lives in `Source/Core`, namespace `rb`, C++20, with no JUCE dependency. JUCE glue lives in `Source/Audio`, `Source/IO`, `Source/UI`.

## 1. Clarifications of V1

| # | Topic | Decision |
| --- | --- | --- |
| C1 | Platforms | V1 targets Windows, Linux (x86_64). The engine is platform independent. Formats: Standalone, VST3, CLAP. See `PLUGIN_FORMATS.md`, `BUILD_RELEASE.md`. |
| C2 | "Record Again replaces the take only once recording begins successfully" | The retained take is replaced when the new capture **completes** with at least 50 ms of audio. Countdown, Armed, Stop, device failure and short taps never replace it. |
| C3 | Edge fade vs Live fade | Edge fade (default 3 ms) applies to clip passes (Record playback, File, ping-pong, loops). Live boundary fade (default 2 ms) applies to each live chunk. "Exact Samples" disables both. |
| C4 | Direction in Live | Live always plays chunks reversed. The Direction control is disabled in Live with an explanation. |
| C5 | Frame rounding | `framesFor(seconds, rate) = llround(seconds * rate)`, minimum 1 where a duration is required. |
| C6 | Stop semantics in File mode | Stop keeps the playhead. Play resumes from the playhead; "Play from Start" restarts at the boundary for the current direction. Record mode Replay always starts at the pass start. |
| C7 | Speed vs sample-rate | One resampling ratio `r = (sourceRate / outputRate) * speed` (source frames consumed per output frame). Take, file preview and export all use it, so offline and live mapping agree. |
| C8 | Input channel choice | `Input 1` (mono, default), `Input 2` (mono), `Mix 1+2` (mono), `Stereo 1+2`. Mono takes have one channel and play equally to L/R. |
| C9 | Plugin defaults | Output volume default is 0 dB in plugin formats and -12 dB in the standalone; direct monitor and output limiter exist only in the standalone. |
| C10 | Hold maximum | Hold to Record is limited to 60 s (the capture maximum). |
| C11 | Repeat Session buffers | Each cycle needs a fresh take buffer allocated off the audio thread; the engine requests it with a `NeedSpareTake` event (section 9). |
| C12 | Loop and PingPong vs Repeat Session | All three are mutually exclusive: choosing Loop or PingPong clears Repeat Session; enabling Repeat Session sets Loop Pattern to Once. |
| C13 | Armed trigger | The detector measures a peak envelope; "50 ms sustained" means the envelope stayed at or above the threshold for 50 ms without a single dip below it. |

## 2. Types and frame arithmetic

- `Frame = uint64_t`; signed positions use `int64_t`. Audio is `float` planar.
- `Mode { Record, Live, File }`, `Direction { Forward, Backward }`, `LoopPattern { Once, Loop, PingPong }`.
- `Selection { Frame begin, end; }` end exclusive, `length() = end - begin`; valid when `end - begin >= minSelectionFrames` (50 ms) and `end <= clip frames`.
- All timeline counters are integer frame counts of processed audio. Wall-clock time never schedules audio events [V1 §5.3].
- `maxBlockFrames` is declared at `prepare`. A host block larger than that is processed in consecutive sub-blocks, so every internal scratch buffer is bounded.

## 3. Audio storage

### 3.1 `AudioClip`
Planar `std::vector<float>` per channel (1 or 2), `sampleRate`, `frameCount`, `capacity`. Lifecycle: `create(rate, channels, capacityFrames)` (zero-filled, mutable) -> writers use `channel(c)` -> `seal(frames)` sets the final `frameCount` (<= capacity) and makes the clip immutable. A sealed clip is never written again. Clips are shared as `std::shared_ptr<const AudioClip>`.

### 3.2 `ClipSource`
Abstract read interface used by `ClipPlayer` and the exporter:
```
channels(), sampleRate(), frameCount()
bool read(int64 first, size_t count, float* const* dst)   // real-time safe, clamps first/count to [0,frameCount); returns false on a cache miss
```
`AudioClip` implements it with `memcpy`. `DiskClipSource` (IO layer) implements it with a bounded block cache (section 11). `read` never blocks, never allocates.

## 4. Resampler (`Resampler.h`)

Bounded windowed-sinc interpolation. Used whenever `r != 1`; at `r == 1` with integer positions samples are copied exactly (exact reversal [V1 §5.2]).

- Prototype `P(x) = sinc(x) * kaiser(x / 16, beta = 9.0)` for `|x| <= 16`, tabulated at 2048 points per unit with linear interpolation (built once, thread-safe static, forced during `prepare`).
- For a source position `p` (double) and ratio `r`: cutoff `fc = min(1, 1/r)`; half-width `H = 16 / fc`; taps `k = ceil(p - H) .. floor(p + H)`; weight `w_k = fc * P(fc * (k - p))`; output `y = sum(w_k * x[clamp(k)]) / sum(w_k)`. Weight normalisation preserves DC gain at every phase.
- **Endpoint clamping:** source indices are clamped to the selection `[begin, end-1]` (edge hold). Playback of a selection therefore never reads outside it, and results at the ends are well-defined.
- Maximum half-width is 16 / (1/4.35) ~ 70 taps (192 kHz -> 44.1 kHz). Scratch for the gathered window is preallocated in `ClipPlayer` for `maxBlockFrames * max(r) + 2H + 8` frames.
- Quality acceptance: a 1 kHz sine at `r = 2` has THD+N below -80 dB; energy above `fc` Nyquist is suppressed by at least 80 dB; DC gain error below 1e-6 (tests T-RES-*).

## 5. `ClipPlayer`

Owns playback of one `ClipSource` selection in either direction at ratio `r`, with loop pattern and fades. All methods except `configure` are real-time safe. `configure(maxBlock, maxRatio)` allocates scratch.

### 5.1 Pass model
For a selection of `L` frames, play-coordinate `u in [0, L)` maps to source position `p = begin + u` (Forward) or `p = end - 1 - u` (Backward). A **segment** is `(u0, n, M)`: `u(n) = u0 + n * r`, `M = round((L - u0) / r)` output frames. A fresh pass has `u0 = 0`, so it emits `M = round(L / r)` frames [V1 §5.2: export frames = round(N / speed)]. At `r = 1`, Backward gives `y[n] = x[end - 1 - n]`.

Pass end behaviour: Once -> finished; Loop -> next pass in the same direction; PingPong -> flip direction, next pass. Loop pattern changes take effect at the next pass boundary [V1 §4.2].

### 5.2 Runtime changes (declick)
Direction flip, speed change or seek while playing: ramp gain to 0 over `declickFrames = max(1, round(2 ms * rate))`, apply the change, ramp back up. Direction flip preserves the source position: with current `u_c = u0 + n*r`, new `u0' = clamp(L - 1 - u_c, 0, L)`, `n = 0`, `M' = round((L - u0') / r)`. Speed change rebases `u0 = u_c`, `n = 0`, new `r`, new `M`.

### 5.3 Fades and stop
Edge fade of `F` frames at both ends of each pass (`F_eff = min(F, floor(M/2))`). Fade-in gain for pass frame `n < F_eff`: `g = 0.5 * (1 - cos(pi * (n + 1) / (F_eff + 1)))`; fade-out mirrors it counted from the last frame. Fades never change `M` [V1 §5.2, A12]. `F = 0` disables ("Exact Samples").
`stop(stopFadeFrames)` ramps gain linearly to 0 over `stopFadeFrames` (default 10 ms) then goes idle; the position is retained (C6). `process` returns the number of frames it produced into `out`; callers treat fewer-than-requested as "finished".

### 5.4 Output
`process(float* const* out, size_t outChannels, size_t frames)` **adds** into `out` (so the caller can zero-fill once and mix several sources). A mono clip feeds every output channel equally; a stereo clip feeds channel `c` from clip channel `c`.
If `ClipSource::read` fails (disk cache miss) the player fades out over the declick length, pauses, and raises `Underrun` for the caller (section 11); it never waits.

## 6. Signal tools (`SignalTools.h`)

- `GainSmoother`: linear ramp, 20 ms, `set(target)`, `next()`.
- `LevelFollower`: `e = max(|x|, e * decay)` with decay set for 5 ms.
- `VoiceTrigger(thresholdDb, sustainFrames = 50 ms)`: feeds a `LevelFollower`; counts consecutive samples with `e >= threshold`; fires when the count reaches `sustainFrames`; any dip below resets to 0.
- `PreRollRing`: 200 ms per channel; `available() = min(filled, 200 ms)`; chronological copy-out.
- `findNonSilentSelection(source, thresholdDb = -50, window = 20 ms, padding = 50 ms)`: RMS per window over the channel-mean-square; first/last window at or above the threshold define `[first*W - pad, (last+1)*W + pad)` clamped to the clip; none found -> empty `Selection` (UI shows "No speech or sound detected"). Selection only; never modifies audio. Undo = restore the previous selection (A19).
- `OutputLimiter` (standalone only): look-ahead `Lh = ceil(1 ms * rate)` frames, ceiling -1 dBFS (0.891251), no make-up gain. Per sample target gain `t = min(1, ceiling / |x|)` (max over channels); `m[n] = min t over the window [n-Lh, n]`; release smoothing `g_r = min(m, g_r + (1 - g_r) * a_rel)` with 80 ms release; the applied gain is the mean of `g_r` over `Lh + 1` samples; the signal is delayed by `Lh` frames. Below ceiling the gain is exactly 1.0. Guarantee: `|out| <= ceiling` for finite input. Latency `Lh` is reported (snapshot `limiterLatencyFrames`) and shown in device status.
- `sanitize(x)`: non-finite -> 0, counted.

## 7. `RecordTransport`

### 7.1 State table [V1 §5.3]
`Ready, Countdown, Armed, Recording, Waiting, Playing, ReadyGap`.

| From | Trigger | To | Notes |
| --- | --- | --- | --- |
| Ready | `start()` | Countdown if `countdown>0`; else Armed if autoStart; else Recording | Take buffer provided with the command |
| Ready | `startHold()` | Recording (held) | No countdown or arming |
| Countdown | `countdownFrames` elapsed | Armed or Recording | Output silent |
| Armed | trigger fires | Recording | Pre-roll copied; clip total length stays exactly `T` |
| Recording | `captured == T` | Waiting | `T = round(captureSec*rate)` |
| Recording (held) | `releaseHold()` or `captured == 60 s` | Waiting if `captured >= 50 ms`, else Ready + event `TooShort` | |
| Recording | `finishEarly()` | Waiting if `captured >= 50 ms`, else ignored (event `TooShort`) | Take length = captured |
| Waiting | `waitFrames` elapsed | Playing | `waitFrames=0` -> Playing on the next sample |
| Playing | pass finished (Once) | Ready, or ReadyGap if Repeat Session | |
| Playing | Loop/PingPong | stays Playing until `stop()` | |
| ReadyGap | `tailGapFrames` elapsed and spare take present | Recording | No countdown, no re-arm |
| any non-Ready | `stop()` | Ready | Playing fades over stop-fade; capture buffer discarded; previous take kept |
| Ready | `replay()` (take exists) | Playing | No wait |

Timeline (countdown 0, relative to start): capture `[0,T)`, wait `[T,T+D)`, playback `[T+D, T+D+M)`. A02: 48 kHz, T=5 s, D=2 s -> 240,000 / 336,000 / 240,000 frames. Transitions land on exact sample indices inside a callback block (the block loop is split at each event), so results are identical for any block size (A03).

### 7.2 Capture
Input is already channel-mapped and gain-applied (`InputStage`). Recording copies input frames into the take; Waiting/Playing/Ready ignore input (A07). Non-finite samples were removed upstream. Capture channels fixed at `start()`.

### 7.3 Pre-roll and auto-start
While Armed every input sample goes into the pre-roll ring and the trigger. When the trigger fires at sample `i`: `avail = ring.available()`; the first `avail` take frames are the ring's chronological contents (ending at sample `i`); capture continues for `T - avail` further frames. The take has exactly `T` frames (A18). If Armed lasts forever the engine stays Armed until `stop()`.

### 7.4 Take publication
When a capture closes the take is sealed (`seal(captured)`), posted as event `TakeCompleted(shared_ptr<const AudioClip>)`, and becomes `retained`. The previous retained reference is moved into the retire queue. `replay()` uses `retained`.

### 7.5 Repeat Session
After Playing -> ReadyGap. At `start` and after every `TakeCompleted` the transport emits `NeedSpareTake(frames, channels, rate)`. The control side answers with `ProvideSpareTake(take)`. If no spare is present when ReadyGap ends the gap is extended, up to 2 s, then the session stops with `Error::SpareTakeUnavailable`.

## 8. `LiveTransport`

Fixed parameters at start: `W = framesFor(chunkSec)`, `D = framesFor(delaySec)`, `S = ceil(D / W) + 4` slots of `W` frames and `captureChannels`, preallocated in `prepare` (maximum: `W=5 s, D=30 s` -> 10 slots; `W=0.1 s, D=30 s` -> 304 slots).

### 8.1 Closed-form schedule
`t` = frames since the current run epoch. Chunk `k` is captured during `[kW, (k+1)W)` into slot `(base + k) mod S`. Output at time `t`: let `j = floor((t - D) / W)` for `t >= D`; if `j >= 1` the output is chunk `j-1` at playback offset `u = (t - D) - jW`, sample value `slot[W - 1 - u]` (reversed read; no in-place reversal). So chunk `k` plays during `[(k+1)W + D, (k+2)W + D)` and first sound is at `W + D` (A04: 120,000 frames at 48 kHz). The block loop splits at every multiple of `W` (capture) and every `jW + D` (output). Each slot stores its chunk index; a mismatch at playback means the schedule broke: output silence, stop, event `Error::FellBehind` ("Audio processing fell behind"). Unit tests force `S` too small to prove it.

Chunk fade envelope by playback offset `u`: `min(fadeIn(u), fadeOut(W-1-u))`, `F` = live fade frames (C3). Source age at offset `u` is `D + 2u + 1` frames [V1 §2.2].

### 8.2 States
`Ready -> Filling -> Running -> (Freezing) -> Frozen -> (Resume) Filling`. `Filling` while `t < W + D` (snapshot reports `fillProgress = t / (W + D)`).
- `freeze()` (Running/Filling): `c = floor(t_r / W)`; capture continues to `t_c = (c+1)W`; state `Freezing`. At `t_c`, slot `c` is pinned as the frozen chunk and capture stops. Adoption frame `A`: if output has begun (`t_c >= W + D`) the smallest `A >= t_c` with `A = mW + D`, `m >= 1`; otherwise `A = t_c`. At `A` state becomes `Frozen`: slot `c` repeats reversed forever with the same envelope; queued chunks beyond `A` are dropped. Event `Frozen` published.
- `resume()` (Frozen/Freezing): output tail-fades (stop fade), epoch resets, all slots invalidated, state `Filling`. The slot cursor continues (`base += S/2`-safe: `base = (base + chunks_used) mod S`) so a fading tail never reads a slot the new run is writing.
- `stop()`: any state -> Ready; output tail-fades over the stop fade reading the retained slot; capture stops; slots invalidated.
- Frozen chunk export: the UI thread copies the pinned slot while state is `Frozen` (generation counter checked before/after the copy); it is immutable in that state.

## 9. `Engine`

`Engine` composes input stage, `RecordTransport`, `LiveTransport`, `ClipPlayer` for File mode, the event queue and the snapshot. It is the unit tested end to end and what the JUCE processor drives.

### 9.1 Real-time API (audio thread only)
`prepare(rate, maxBlock, inChannels)` (non-RT), `applySettings(const Settings&)`, `handle(const Command&)`, `process(const float* const* in, float* const* out, size_t frames)` (`in` has the captured channels; `out` is stereo), `stopImmediate()`.
`process` zero-fills `out`, runs every transport that is non-idle (so tails complete after a mode change), and feeds input only to the active mode. Mode change stops the previous mode with the stop fade.

### 9.2 Commands (control thread -> audio thread)
`StartRecord{take}`, `StartHold{take}`, `ReleaseHold`, `FinishEarly`, `Replay`, `StartLive`, `Freeze`, `Resume`, `Stop`, `SetMode`, `SetFile{source, selection}`, `SetSelection`, `PlayFile{fromStart}`, `ProvideSpareTake{take}`, `ClearTake`. Commands that carry resources hold `shared_ptr`s allocated on the control thread. `StartRecord` while not Ready is rejected with an event.

### 9.3 Settings and when they apply
| Setting | Applies |
| --- | --- |
| capture, wait, countdown, auto-start, threshold, tail gap, repeat | latched at `StartRecord` (tail gap: next cycle) |
| live chunk, delay | latched at `StartLive` / `Resume`; control disabled while Live runs |
| speed, direction, loop pattern, edge fade | live; speed/direction via declick, loop at pass boundary, fade at next pass |
| input gain, output volume, monitor | smoothed over 20 ms; gain affects new captures only |

### 9.4 Events and snapshot
Events (audio -> control, bounded SPSC, drop-oldest never; capacity 128): `TakeCompleted`, `CaptureCancelled{reason}`, `TooShort`, `FrozenReady`, `PlaybackFinished`, `NeedSpareTake`, `Underrun`, `Error{code}`, `Message{code}`.
Snapshot (trivially copyable, published with a sequence lock, polled by the UI at 30-60 Hz): `mode`, `recordState`, `liveState`, `filePlaying`, `stateFrame`, `stateLength`, `playhead` (0..1 along the waveform, left to right regardless of direction), `countdownFramesLeft`, `inputPeak`, `outputPeak`, `inputOver`, `fillProgress`, `liveChunkIndex`, `takeId`, `takeFrames`, `frozenGeneration`, `errorCode`, `sanitizedCount`, `limiterLatencyFrames`, `sampleRate`.

### 9.5 Ownership and retirement
Every `shared_ptr` the audio thread stops using is moved into the retire queue (`RetireQueue`, audio -> control SPSC, 64 slots) and destroyed by the control thread (`Engine::service()`). The audio thread never calls `delete`. Clips visible to both sides are also held by the control-side asset store until retired. Rapid replacement (A15) is covered by a threaded stress test.

### 9.6 Queues
`SpscQueue<T, N>`: bounded, wait-free, power-of-two capacity, move-only elements. Control-thread producers share a mutex; the audio thread never takes it.

## 10. Output stage and input stage (`OutputStage.h`, `InputStage.h`)

- `InputStage`: channel mapping per C8, `GainSmoother`, sanitize, peak/overload measurement.
- `OutputStage`: engine output `*` smoothed volume, plus smoothed direct monitor of the mapped input (muted while Record mode is Waiting/Playing), then optional `OutputLimiter`. In plugin formats volume still applies; monitor and limiter are compiled out of the signal path (C9).
- Export never passes through `OutputStage` [V1 §5.5, A20].

## 11. File decoding and disk cache (`Source/IO`)

Load pipeline on a worker thread, cancellable, progress 0..1, typed errors: `NotFound, Unreadable, Unsupported, TooManyChannels, TooLong (>30 min), RateTooHigh (>192 kHz), TooLarge (>3 GiB decoded), DiskFull, Cancelled, Corrupt`. Only WAV, AIFF and FLAC readers are registered. A failed or cancelled load never replaces the current file. Decoded float size `<= 256 MiB` -> RAM `AudioClip`. Larger -> planar float32 cache file in `<tmp>/ReverseBack-cache-<pid>-<token>/`, checked against free disk space; `DiskClipSource` keeps up to 16 blocks of 65,536 frames in RAM, a prefetch thread loads blocks ahead in the playback direction, `read` returns `false` for a non-resident block (underrun). Startup removes directories with our prefix whose owning process is gone; closing removes ours. A waveform overview (8,192 min/max buckets) is built during decode.

Export (`ExportService`): two passes of the same `ClipPlayer` render (section 5), so playback and export share one mapping. Pass 1 measures peak and frame count; PCM export that would exceed full scale is refused unless Peak Normalize or float is chosen. Output frame count is `M = round(L / r)` with `r = (srcRate / outRate) * speed` (C7); at source-rate export this is `round(L / speed)` [V1 §5.2]. Export keeps the clip's channel count (mono stays mono). Writes `.<name>.tmp` in the destination folder then renames; existing destinations require UI confirmation; failures leave the destination untouched and keep the in-memory asset.

## 12. Presets and Surprise (`Presets.h`)

Built-ins [V1 §3]: Say Something (Record 5 s / 2 s / 1x), Tiny Syllables (Live 0.25 s / 0.25 s), Backwards Conversation (Live 1 s / 0.5 s), Long Phrase (Record 10 s / 2 s). A preset sets mode, capture, wait, live chunk, live delay, speed, loop pattern, direction, repeat; it never touches gain, devices, monitor or recording state. Surprise: speed from {0.5, 0.75, 1, 1.5, 2}, direction from {Forward, Backward}, loop pattern from {Once, Loop, PingPong}; only in Record/File when stopped; takes an injectable RNG for tests.

## 13. Persistence (`SettingsStore`)

JSON, `{"version": 1, ...}` at the platform user-config directory (`~/.config/ReverseBack/settings.json` on Linux). Atomic save (temp + rename). Unknown versions or corrupt files fall back to defaults and keep the corrupt file as `.bad`. Stores last folder, device state (standalone), UI preferences (shortcuts enabled, hold key, reduced motion), user presets and the last parameter values; never audio, never monitor-on.

## 14. Test map

| Acceptance | Automated test |
| --- | --- |
| A01, A11, A12 | `Tests/Core/ClipTests.cpp`, `ResamplerTests.cpp` |
| A02, A03, A06, A07, A08, A18 | `Tests/Core/RecordTests.cpp` |
| A04, A05, A06, A09 | `Tests/Core/LiveTests.cpp` |
| A15 | `Tests/Core/StressTests.cpp` (allocation hook, threaded retire) + TSan/ASan runs |
| A19 | `Tests/Core/SignalToolsTests.cpp` |
| A10, A14, A20 | `Tests/Integration/IoTests.cpp` |
| A13, A16, A17 | `Tests/Integration/ProcessorTests.cpp`, `GuiTests.cpp` |
| P01-P10 | `Tests/Integration/PluginTests.cpp` + pluginval (see `PLUGIN_FORMATS.md`) |
| A05 (60 minutes) | simulated clock run in `LiveTests.cpp` (no real-time wait) |
