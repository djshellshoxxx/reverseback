# ReverseBack plugin and format specification

Fulfils the requirement in `REVERSEBACK_V1.md` §8 that a plugin wrapper "explicitly specify host routing, transport behavior, state persistence and latency policy before implementation". Formats delivered by the beta: **Standalone**, **VST3**, **CLAP**, all on Linux x86_64, with Windows built by CI from the same sources. All formats share one `ReverseBackProcessor` and one editor.

## 1. Identity

| Item | Value |
| --- | --- |
| Product / company | ReverseBack / Circuit Drift Labs |
| Plugin type | Audio effect (not an instrument); no MIDI |
| VST3 category | `Fx|Tools` |
| CLAP id | `labs.circuitdrift.reverseback`; features `audio-effect`, `utility`, `stereo` |
| Manufacturer / plugin code | `CdLb` / `RvBk` |
| Bundle names | `ReverseBack.vst3`, `ReverseBack.clap`, executable `ReverseBack` |
| Version | semantic version from CMake (`0.1.0-beta.1` for the first beta) |

## 2. Host routing

- **Buses:** one main input bus and one main output bus. Supported layouts: in {mono, stereo} x out {mono, stereo}. A disabled input bus is also accepted (File mode works; Record/Live show "No input routed"). Hosts that offer only stereo-in/stereo-out always work.
- **Signal:** output is the wet signal only; there is no dry mix [V1 §5.5]. When idle the plugin outputs silence. To hear the dry signal, use the host's parallel routing. A mono output receives the mean of the engine's L and R.
- **Input channel choice** (`Input 1 / Input 2 / Mix 1+2 / Stereo 1+2`) behaves as in the standalone; with a mono input only `Input 1` is meaningful and the control explains why.
- **No sidechain, no auxiliary buses.**

## 3. Transport and timing policy

- The engine counts **processed audio frames**. It does not follow host play/stop, tempo, loop or position. Starting a recording starts the engine clock, whatever the host transport does.
- If the host stops calling `processBlock` (many hosts do this while stopped), the engine is paused with it; timing resumes when processing resumes. The GUI cannot detect this, so the Record help text says "Audio must be running in your host for recording and playback to progress".
- **Offline render / bounce:** `isNonRealtime` is honoured; processing is deterministic and identical to real time because it is purely frame-counted.
- **Sample-rate or block-size change (`prepareToPlay`):** all transports stop, Live buffers are rebuilt, retained takes survive and are resampled on playback (C7). Never resumes recording automatically.
- **Bypass:** when the host bypasses the plugin, the engine receives `stopImmediate()` once, then the host-provided pass-through behaviour applies. Un-bypassing returns to Ready.
- **Block sizes:** 1 to 8192 frames, variable per call; larger calls are split internally.

## 4. Latency policy

- Reported plugin latency is **0 samples** in plugin formats. The Wait/Live delay is a creative delay and is deliberately not compensated by the host (compensation is excluded [V1 §8]).
- The output limiter is standalone only, so it adds no latency to plugins. In the standalone its 1 ms look-ahead is shown in the device status.
- Live Reverse: first sound at `W + D` after Start; Record: playback at `T + D` [V1 §2]. Host and device buffering are additional.

## 5. Parameters (host-visible, automatable)

Stable string ids; the id never changes once released. Units are shown in host displays. "Persist" = saved in plugin state.

| Id | Name | Type / range | Default | Persist |
| --- | --- | --- | --- | --- |
| `mode` | Mode | Record, Live, File | Record | yes |
| `capture` | Record Length | 0.25-60 s | 5 | yes |
| `wait` | Wait Before Playback | 0-30 s | 2 | yes |
| `chunk` | Live Chunk | 0.1-5 s | 0.5 | yes |
| `delay` | Live Extra Delay | 0-30 s | 2 | yes |
| `inGain` | Input Gain | -24..+24 dB | 0 | yes |
| `outVol` | Output Volume | -60..0 dB | 0 (plugin), -12 (standalone) | yes |
| `edgeFade` | Edge Fade | 0-10 ms | 3 | yes |
| `liveFade` | Live Edge Fade | 0-10 ms | 2 | yes |
| `exact` | Exact Samples | off/on | off | yes |
| `speed` | Speed | 0.5-2x | 1 | yes |
| `direction` | Direction | Forward, Backward | Backward | yes |
| `loop` | Loop | Once, Loop, Ping-pong | Once | yes |
| `repeat` | Repeat Session | off/on | off | yes |
| `tailGap` | Repeat Gap | 0-2 s | 0.5 | yes |
| `countdown` | Countdown | Off, 1, 3, 5 s | Off | yes |
| `autoStart` | Auto Start on Voice | off/on | off | yes |
| `threshold` | Voice Threshold | -65..-15 dBFS | -45 | yes |
| `inChan` | Input Channels | Input 1, Input 2, Mix 1+2, Stereo 1+2 | Input 1 | yes |
| `monitor` | Input Monitor | 0-100 % | 0 | **no** (standalone only; always starts at 0) |
| `trgStart` | Start / Stop (trigger) | momentary | off | no |
| `trgHold` | Hold to Record (trigger) | held | off | no |
| `trgReplay` | Replay (trigger) | momentary | off | no |
| `trgFreeze` | Freeze / Resume (trigger) | momentary | off | no |

Trigger parameters act on a **rising edge only**; state restore never produces an edge, so loading a project, a preset or a state cannot start a recording [V1 §6]. Continuous parameters are smoothed or latched as specified in `ENGINE_DESIGN.md` §9.3. Parameters that cannot change in the current state (capture length while recording, live chunk while Live runs) are accepted from the host but take effect at the next start; the editor shows them disabled with an explanation.

## 6. State persistence

`getStateInformation` writes a versioned XML document:
```
<ReverseBackState version="1">  persistent parameters as attributes
  <File path="..." begin="..." end="..."/>        optional: path and selection only
</ReverseBackState>
```
Rules:
1. **No audio data is stored**: no takes, no frozen chunks, no file contents.
2. On restore, parameters apply, the engine is stopped and Ready, monitor is 0, trigger parameters are off. Nothing records or plays.
3. If a `File` element exists and the file is present, it is decoded asynchronously (cancellable) and the selection restored; if missing or unsupported the File slot stays empty and the status shows "File not found: <name>".
4. Unknown future versions are ignored except for parameters that exist; corrupt state falls back to defaults without throwing.
5. State is host-thread safe: it reads atomics/value tree only.

## 7. Editor in plugin windows

Same editor as the standalone. Resizable from 820 x 620 up to 1920 x 1280 with the aspect not locked; the host's scale factor is honoured. Differences in plugin formats: no Audio Settings device section (shows "Audio devices are managed by your host"), no Input Monitor, no output limiter, keyboard shortcuts only act while the editor has focus, and the menu button offers the same commands as the standalone's menu. File choosers and the export dialog use asynchronous native choosers and never block the host.

## 8. Standalone application

Custom JUCE standalone (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`): a single resizable window hosting the editor; our own `AudioDeviceManager` and `AudioProcessorPlayer`. Behaviour:
- Opens the OS default input/output at 48 kHz and 256 frames when supported, falling back to the device defaults; actual values are shown after opening [V1 §4.2].
- Audio Settings sheet embeds the JUCE device selector (input/output device, channels, sample rate, buffer size) with the **Test** button for the explicit output test sound. The chosen devices persist in `settings.json`.
- On input permission denial or no input device: Record and Live show an inline error with Retry and Settings; File mode remains usable. If no output device can be opened, the window still opens, shows the error, and Retry/Settings offer recovery.
- A device disconnect or rate change stops the engine (retained takes kept) and reopens/prepare; recording never resumes automatically.
- Command-line: `--version`, `--help`, `--selftest` (offline engine check, exit code 0/1, no audio device required).
- Linux: ALSA (and JACK when present at run time); window icon from the bundled PNG; `.desktop` entry installed by packages.

## 9. Format-specific acceptance tests

| ID | Test | Evidence |
| --- | --- | --- |
| P01 | Bus layouts mono/stereo in/out accepted; unsupported layouts rejected | `PluginTests` |
| P02 | Parameter count, ids, ranges, defaults, text round-trip match section 5 | `PluginTests` |
| P03 | State save -> restore reproduces parameters; no audio stored; monitor 0; triggers off | `PluginTests` |
| P04 | Restoring state during idle never starts capture/playback (silent output after restore) | `PluginTests` |
| P05 | Missing file path in state leaves File slot empty with message, no crash | `PluginTests` |
| P06 | Block sizes 1, 7, 64, 513, 8192 and changing block size between calls give identical timelines | `PluginTests` |
| P07 | `prepareToPlay` with a new rate stops transports, keeps the take, plays it at the new rate | `PluginTests` |
| P08 | Bypass during recording cancels capture, keeps prior take, no output afterwards | `PluginTests` |
| P09 | Reported latency 0 in plugin builds | `PluginTests` |
| P10 | pluginval strictness 5 (VST3) passes; CLAP binary exports a valid `clap_entry` and descriptor | release checklist |
| P11 | Offline (non-realtime) render of a scripted Record cycle equals the real-time-style block run | `PluginTests` |
| P12 | Editor opens, resizes between min and max, and closes repeatedly without leaks or crashes | `GuiTests` under Xvfb |

## 10. Known format limitations (beta)

- No host-transport sync, tempo sync, MIDI triggers or latency compensation (excluded by V1 §8; trigger parameters can be MIDI-mapped in hosts that support it).
- Takes are not saved with the project (by design, privacy); re-record after loading a project.
- Linux keyboard accessibility tools are limited by JUCE's platform support; every action is reachable through keyboard shortcuts and the menu.
