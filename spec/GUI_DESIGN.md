# ReverseBack GUI design specification

Companion to `REVERSEBACK_V1.md` §4 (structure and control contract) and `docs/GUI_MOCKUP.svg` (Record mode). This document adds the visual system, exact layout, component states, text and behaviour needed to build a modern, fully functional interface in JUCE for the standalone and plugin windows. Every control listed here must be wired to the engine; no mockup-only control counts as implemented.

## 1. Principles

1. **One obvious next action.** The large primary button is always the thing to press; while busy it becomes an unmistakable red **Stop**.
2. **State in words and colour.** Colour never carries meaning alone; every state has a text label.
3. **Cheerful and restrained.** Dark theme, one accent colour per mode, rounded cards, soft depth, modest motion.
4. **Never lie about timing.** Labels use "first sound after", never a fixed per-sample delay.
5. **Disabled controls explain themselves** through tooltip and, when focused, the hint line.
6. **No decorative work on the audio thread.** The GUI only reads snapshots on a timer and posts commands.

## 2. Design tokens

### Colour (sRGB hex)
| Token | Value | Use |
| --- | --- | --- |
| `bg0` | `#0E131C` | window |
| `bg1` | `#151C29` | cards, drawer |
| `bg2` | `#1E283A` | controls |
| `bg3` | `#2A364C` | hover |
| `line` | `#2F3B52` | 1 px borders, dividers |
| `text` | `#ECF2FA` | primary text (contrast on bg1 >= 13:1) |
| `text2` | `#A8B7CD` | secondary text (>= 7:1 on bg1) |
| `text3` | `#7F8EA6` | placeholders, disabled labels (decorative only) |
| `ok` | `#48D5AF` | Ready, healthy meter |
| `warn` | `#FFC857` | Waiting, near-clip |
| `rec` | `#FF5C6C` | Recording, Stop, over, errors |
| Record accent | fill `#6454DC`, wave `#A699FF` | Record mode |
| Live accent | fill `#0E7490`, wave `#4CD3F5` | Live mode |
| File accent | fill `#A04A07`, wave `#FBBF24` | File mode |

White-ish `text` on every accent fill has contrast >= 4.5:1. Focus ring: 2 px `text` outside the control with 2 px gap, visible on all backgrounds.

### Type (Inter; embedded, OFL 1.1)
| Style | Size / weight | Use |
| --- | --- | --- |
| Display | 28 / Bold | wordmark |
| Title | 21 / SemiBold | primary button |
| Body | 16 / Regular | labels, values |
| Control | 18 / Medium | buttons, tabs |
| Caption | 14 / Regular | secondary text, meters, ruler (never below 13) |
| Mono numerals | tabular figures on | times and values |

### Shape, space, motion
- 8 px grid; margin 32 px; gutter 16 px; card radius 16; control radius 10; primary radius 14; tab container radius 14.
- Minimum touch target 40 x 40 logical px; primary 62 px high.
- Elevation: cards have a 1 px `line` border and a soft 12 px shadow at 35 % black.
- Motion: hover/press colour transitions 120 ms; drawer slide 160 ms; recording dot pulse 1 Hz; waveform glow static. **Reduced motion** (OS preference or settings override): all transitions instant, no pulse.

## 3. Window and layout

Minimum 820 x 620, default 960 x 700, maximum 1920 x 1280 (plugins) / unlimited (standalone). Layout is rows stacked top to bottom; only the waveform card flexes vertically, and all rows flex horizontally. Heights below are logical px.

| Row | Height | Content |
| --- | --- | --- |
| Header | 64 | wordmark + small "Circuit Drift Labs" caption; right: **Preset** dropdown (160 x 40), **Menu** button (40 x 40), **Settings** gear (40 x 40) |
| Mode tabs | 56 | three equal segments with icon + label: Record & Reverse, Live Reverse, Reverse File; selected segment filled with the mode accent |
| Source strip | 40 | left: source selector (see 4); right: input level meter (width 266, caption "Input") with `OVER` badge |
| Banner (conditional) | 44 | inline error with **Retry** and **Settings** |
| Waveform card | flex (min 150) | see 5 |
| Parameter row | 56 | mode specific (see 6) |
| Transport row | 62 | primary + secondary buttons |
| Options row | 40 | Direction, Loop, Volume, Advanced toggle |
| Status bar | 36 | state dot + state text + hint; right: device summary |

The **Advanced drawer** is a right-hand panel (width 320) over the waveform card and parameter row, sliding in over 160 ms; the waveform card narrows instead of being covered. Drawer tabs (segmented): **Recording**, **Playback**, **Audio**, **Take**. Which tabs exist depends on mode (see 8). Overlay **sheets** (Settings, Export, About, confirmations) are centred 560 px cards over a 55 % black scrim inside the editor, so plugin windows never open extra OS windows.

### Record mode wireframe (960 x 700)
```
 ReverseBack                                  [Preset v] [=] [gear]
 [ Record & Reverse ][ Live Reverse ][ Reverse File ]
 Microphone: System default v                    Input [=====     ]
 +----------------------------------------------------------------+
 | LATEST TAKE - 5.00 seconds                  < Playing backwards |
 |  ~~~ waveform, playhead travels right to left ~~~              |
 | 0:00                                                     0:05  |
 +----------------------------------------------------------------+
 Record for [ 5.00 s ]  Wait before playback [ 2.00 s ]  Repeat session (o)
 [ Record & Reverse ] [ Hold to Record ] [ Replay ] [ Save WAV ]
 Direction [<Backward|Forward>]  Loop [Once|Loop|Ping-pong]  Volume [-12 dB]  Advanced >
 * Ready   Speak for 5 seconds. Hear it backwards after a 2-second wait.
```

### Live mode wireframe
```
 ReverseBack                                  [Preset v] [=] [gear]
 [ Record & Reverse ][ LIVE REVERSE ][ Reverse File ]
 Microphone: System default v   (headphones) Headphones recommended   Input [=====   ]
 +----------------------------------------------------------------+
 | NOW PLAYING - chunk 12 (500 ms)   age of audio rises D + 2u     |
 |  ~~~ currently playing reversed chunk waveform ~~~             |
 | capture [#####-----]  buffer [##########]  filling 1.8 s       |
 +----------------------------------------------------------------+
 Reverse chunk [ 500 ms ]  Extra delay [ 2.00 s ]  First sound after 2.50 s + device buffering
 [ Start Live ] [ Freeze ] [ Save Frozen WAV ]
 Direction (disabled: Live always reverses)  Volume [-12 dB]  Advanced >
 * Filling buffer - first sound in 1.8 s
```
Live copy must say "Startup latency is chunk + delay; each sound's delay varies inside a chunk" in the help tooltip of the computed line; the line itself never states a fixed total delay.

### File mode wireframe
```
 [ Record & Reverse ][ Live Reverse ][ REVERSE FILE ]
 File: interview.wav  48 kHz - stereo - 2:31.4          [Open...]
 +----------------------------------------------------------------+
 | drop a WAV, AIFF or FLAC file here (when empty)                 |
 |  ~~~ waveform with selection handles [|======selection======|] ~~|
 | 0:00                      0:31.2  -  1:12.9                2:31 |
 +----------------------------------------------------------------+
 Start [ 0:31.200 ]  End [ 1:12.900 ]  Length 41.7 s  [Select all]
 [ Play Selection ] [ Play from Start ] [ Save WAV ]
 Direction [<Backward|Forward>]  Loop [Once|Loop|Ping-pong]  Volume [-12 dB]  Advanced >
 * Ready
```

## 4. Source strip
- **Record/Live, standalone:** "Microphone: <actual device name>" opens a menu of input devices (same list as Audio Settings) plus "Audio settings..."; selecting a device reopens the device manager.
- **Record/Live, plugin:** "Input: Host track" caption plus the **Input Channels** menu (`inChan`).
- **File:** file name (middle-ellipsised), "48 kHz - stereo - 2:31.4", **Open...** (Ctrl+O) button, and the Loading progress bar with **Cancel** while loading.
- Input meter: 266 x 14, rounded; -60..0 dBFS, gradient `ok` -> `warn` (>= -12) -> `rec` (>= -1); 2 px peak-hold tick (1.2 s hold); **OVER** badge lights (text + `rec`) for 1.5 s when a sample exceeds 0 dBFS. Accessible summary string updates at most once per second.
- Live: a persistent compact chip "Headphones recommended for Live Reverse" with a headphone glyph.

## 5. Waveform card
- Header caption (uppercase, Caption style, `text2`): Record "LATEST TAKE - 5.00 seconds" or "NO TAKE YET"; Live "NOW PLAYING - chunk n"; File "SELECTION - 41.7 s of 2:31.4". Right side: direction chip with arrow and word ("< Playing backwards", "Playing forwards", "Backward - ready").
- Drawing: min/max envelope mirrored about the centre line, filled with a vertical gradient from the wave colour (alpha 0.9) to 0.35 alpha at the extremes, plus a 1 px brighter outline; a faint centre line; grid lines at nice time ticks; ruler labels (m:ss or s). 4 px horizontal padding. Amplitude scale fits the take (never auto-normalises audio, display only; a fixed +/-1 full scale view with peaks clipped visually at the card edge, and over-unity peaks drawn in `rec`).
- Playhead: 2 px line in `text` with a 10 px triangle on top, following the snapshot `playhead`. Record/File: during Backward playback it travels right to left.
- Record progress overlay: while Recording, the waveform draws live from the take as it fills (snapshot-driven preview from the capture buffer is **not** read from the audio thread; the card shows a progress bar and elapsed/remaining text plus the live input level instead). While Waiting, a thin countdown bar. While Countdown, a large centred numeral (`3`, `2`, `1`) in Display style, 72 px.
- File selection: shaded outside area at 55 % black; two 12 px wide handles with grips, focusable; dragging uses source frames; arrow keys move the focused handle 10 ms (Shift: 100 ms, Alt: 1 ms); the numeric inputs below edit the same values; invalid (start >= end or < 50 ms) is rejected with a hint. Double-click selects all.
- Live card: shows the reversed chunk currently playing (the engine publishes a decimated 256-point envelope per chunk), a capture progress bar for the chunk being recorded, a fill progress bar during Filling, and "Frozen" badge with the frozen chunk waveform when frozen.
- Drag and drop: dropping a file anywhere on the editor in File mode (or switching to File mode on drop) loads it; non-audio drops show a toast "Unsupported file".
- The waveform is never the only way to select an interval (numeric inputs and Select all exist).

## 6. Parameter rows
Fields use the **number field** component: a pill-shaped slider (`LinearBar`) showing value + unit, drag to change, click to type, mouse wheel and Up/Down arrow keys step, double-click resets to default, Enter commits, Esc cancels. Steps and ranges follow V1 §4.2.
- Record: **Record for** (0.25-60 s, 0.05), **Wait before playback** (0-30 s, 0.05), **Repeat session** switch with caption "Off". Repeat Session is disabled with the tooltip "Turn off Loop to repeat sessions" while Loop/Ping-pong is on (C12).
- Live: **Reverse chunk** (0.1-5 s, 0.01), **Extra delay** (0-30 s, 0.05), computed label "First sound after X.XX s + device buffering". Disabled while Live runs: tooltip "Stop Live to change this".
- File: **Start** and **End** (m:ss.mmm text fields with validation), **Length** read-out, **Select all**.
- Record fields are disabled while recording ("Applies to the next recording").

## 7. Transport and options
| Mode | Primary | Secondary |
| --- | --- | --- |
| Record | **Record & Reverse** (accent) -> **Stop** (`rec`) while busy; label changes to "Listening..." while Armed | **Hold to Record** (press and hold; text "Release to finish"), **Replay**, **Save WAV** |
| Live | **Start Live** -> **Stop** | **Freeze** / **Resume** (toggle), **Save Frozen WAV** |
| File | **Play Selection** / **Reverse** wording follows Direction: "Play Selection" (Forward) or "Play Backwards" (Backward) -> **Stop** | **Play from Start**, **Save WAV** |

Disabled rules: Replay/Save disabled until a complete asset exists (tooltip "Record something first" / "Open a file first"); Hold disabled when not Ready; Freeze only while Live is Filling/Running/Frozen.
Options row: **Direction** segmented (Backward / Forward; Live disabled with tooltip "Live always plays backwards"), **Loop** segmented (Once / Loop / Ping-pong; Live disabled), **Volume** number field (-60..0 dB, 0.1 dB) with a small speaker glyph, **Advanced** toggle (chevron rotates).

## 8. Advanced drawer contents
| Tab | Controls | Modes |
| --- | --- | --- |
| Recording | Countdown segmented (Off, 1 s, 3 s, 5 s); **Auto start on voice** switch + threshold number field (-65..-15 dBFS) + caption "Background noise and speech cannot always be told apart"; Repeat gap (0-2 s) | Record |
| Playback | Speed buttons 0.5x, 0.75x, 1x, 1.5x, 2x and a 0.5-2.0 number field; Edge fade (0-10 ms) / Live edge fade; **Exact Samples (no fades)** switch with caption "Exact sample order, may click"; Speed disabled in Live ("Live is fixed at 1x") | all |
| Audio | Input gain (-24..+24 dB), Input channels menu, Input monitor (0-100 %, standalone only; disabled in plugins with "Use your host's monitoring"), caption "Monitoring can cause feedback; use headphones" | Record, Live |
| Take | **Trim silence** and **Undo trim** buttons (selection-only), **Surprise settings** button (stopped Record/File only), take info (frames, rate, channels) | Record, File |

Selecting a tab remembers the last choice per mode. Open/closed state of the drawer persists in settings.

## 9. Sheets and menus
- **Menu (=)** and keyboard equivalents: Open File (Ctrl+O), Save WAV (Ctrl+S), Start/Stop (Space), Replay (R), Toggle Direction (F), Stop/Cancel (Esc), Hold to Record (H, configurable), Settings, Keyboard shortcuts on/off, Reduced motion, About. Every shortcut has a menu entry showing its key.
- **Settings sheet:** tabs Audio (standalone: embedded JUCE device selector with input/output device, channels, rate, buffer and **Test** sound; plugin: informational), Interface (reduced motion, shortcuts, hold key, tooltips), About.
- **Export sheet:** shows the source (take/selection/frozen chunk), the **export summary** (direction, speed, selection, fades, duration, sample rate, channels), format (32-bit float default / 24-bit PCM), sample rate (Source / 44.1 / 48), Dither (24-bit only), Peak Normalize to -1 dBFS (off), Exact Samples. **Save...** opens an async native file chooser. Progress bar with **Cancel**. If PCM would clip: confirmation card with **Export as 32-bit float**, **Normalize to -1 dBFS**, **Cancel**. Existing file: **Replace** / **Cancel**.
- **Presets menu:** the four built-ins, user presets, **Save current as...**, **Delete preset...**. Selecting stops processing, applies settings, returns to Ready; never records.

## 10. Status and error text
| Situation | State text | Hint |
| --- | --- | --- |
| Record Ready | Ready | Speak for {T} seconds. Hear it backwards after a {D}-second wait. |
| Countdown | Starting in {n} | Get ready |
| Armed | Listening for your voice | Recording starts when sound is detected |
| Recording | Recording - {s} s left | Press Stop to cancel; Finish early with Space |
| Held | Recording - release to finish | |
| Waiting | Waiting - {s} s | |
| Playing | Playing backwards / forwards | |
| ReadyGap | Next take in {s} s | Repeat Session is on |
| Too short | Hold longer to record | |
| Live Filling | Filling buffer - first sound in {s} s | Headphones recommended |
| Live Running | Live - reversing {W} chunks | |
| Freezing | Freezing... | |
| Frozen | Frozen - looping last chunk | Resume to go live again |
| File Empty | Open or drop a WAV, AIFF or FLAC file | |
| File Loading | Loading... {p} % | Cancel to keep the current file |
| File Ready | Ready | |
| Error | Audio processing fell behind / Could not open the microphone: {reason} / Could not load file: {reason} | Retry / Settings |
Errors persist as an inline banner (never a recurring modal) until dismissed, retried or resolved. Success toasts (export saved) appear for 4 s at the bottom right with **Show in folder**.

## 11. Component states
All interactive components define: default, hover (`bg3`), pressed (darken 8 %), keyboard-focused (focus ring), disabled (45 % opacity, tooltip reason, not focusable by mouse), and, where relevant, selected (accent fill). Switches animate 120 ms. Buttons show an icon (vector path) before the label. Tooltips are enabled by default (700 ms delay) and use the same tokens.

## 12. Keyboard, focus, accessibility
- Tab order follows visual order: mode tabs, source selector, waveform handles, parameter fields, transport, options, drawer, status.
- Space toggles Start/Stop **only when no text input has focus**; typing in a field never records. Key repeat is ignored for Hold (H). Window focus loss releases Hold (engine `ReleaseHold`).
- Each component sets title, description and help text; meters and countdowns expose summary strings updated at most once per second (to avoid flooding screen readers); state changes are announced via the status label.
- Touch targets >= 40 px; contrast >= 4.5:1 for text; colour is always paired with text or an icon.
- Reduced motion honoured (section 2).

## 13. GUI verification
`Tests/Integration/GuiTests.cpp` creates the editor against a real `ReverseBackProcessor` and drives each control by component id, asserting the intended engine/parameter change (A16), disabled-reason tooltips, and the "typing never records" rule. A screenshot harness renders all three modes, the drawer, each sheet and the error banner to PNG for review; release screenshots live in `docs/screenshots/`.
