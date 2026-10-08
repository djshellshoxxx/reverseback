# ReverseBack build, packaging and release specification

Settles the open items from `REVERSEBACK_V1.md` §5.1 ("pin an exact compatible release ... review its current license before distributing") and the plan's dependency task.

## 1. Dependencies and licensing

| Dependency | Pin | Role | License used |
| --- | --- | --- | --- |
| JUCE | tag `8.0.15` (git) | device I/O, GUI, file formats, plugin wrappers | **AGPLv3** (JUCE is dual-licensed AGPLv3 / commercial; JUCE 9.x has the same structure) |
| clap-juce-extensions | commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` (0.26.0 + 117 commits; the 0.26.0 tag predates JUCE 8.0.11 and does not compile against JUCE 8.0.15) with its pinned `clap` and `clap-helpers` submodules | CLAP wrapper | MIT |
| VST3 SDK | bundled inside JUCE 8.0.15 | VST3 wrapper | per JUCE (GPLv3 path) |
| Inter font | 4.0 (embedded TTF/OTF subset) | UI typography | SIL OFL 1.1 |

**Licensing decision.** The ReverseBack source authored by Circuit Drift Labs remains MIT (`LICENSE`). Because the binaries link JUCE under its AGPLv3 option, **the compiled beta binaries are distributed under AGPL-3.0-or-later terms** and must be accompanied by the complete corresponding source (the repository at the release tag) and the third-party notices (`THIRD_PARTY_LICENSES.md`). Shipping closed-source binaries, or binaries under other terms, requires a commercial JUCE licence; changing that is the owner's decision and does not affect the engine, which has no JUCE dependency. The "VST" name is a trademark of Steinberg Media Technologies GmbH; the beta does not use the VST logo.

Dependencies are fetched by CMake `FetchContent` at the pinned tags (`-DFETCHCONTENT_SOURCE_DIR_JUCE=...` and `..._CLAP_JUCE_EXTENSIONS=...` allow offline builds from local checkouts). No other network access happens at build time.

## 2. Repository layout (additions to V1 §9)
```
CMakeLists.txt            options, dependency fetch, targets
cmake/                    helper modules
Source/Core/              framework-free engine (static library rb_core)
Source/Audio/             ReverseBackProcessor, parameters, state
Source/IO/                FileService, DiskClipSource, ExportService, SettingsStore
Source/UI/                editor, look-and-feel, components, sheets
Source/Standalone/        custom standalone application
Assets/                   Inter font files, icon
Tests/Core/               engine tests (no JUCE)
Tests/Integration/        IO, processor, plugin and GUI tests (JUCE)
packaging/linux/          desktop entry, icon, install script, deb control, AppStream
scripts/                  build, test, package, validate helpers
.github/workflows/        CI and release
docs/                     status, screenshots, plans
```

## 3. CMake interface

| Option | Default | Meaning |
| --- | --- | --- |
| `RB_BUILD_PLUGIN` | ON | JUCE targets (Standalone, VST3, CLAP) |
| `RB_BUILD_TESTS` | ON | core tests always; integration tests when the plugin is built |
| `RB_SANITIZE` | none | `address;undefined` or `thread` for core tests |
| `RB_CORE_ONLY` | OFF | build only `rb_core` and its tests (no JUCE, no network) |
| `CMAKE_BUILD_TYPE` | Release | Release uses `-O2`, LTO off for reproducibility |

Targets: `rb_core` (static lib), `rb_core_tests` (CTest), `ReverseBack` (JUCE plugin with `Standalone VST3 CLAP`), `rb_integration_tests`, `package-linux` (custom target producing the archives). Versions come from `project(ReverseBack VERSION x.y.z)` plus `RB_VERSION_SUFFIX` (`beta.1`) and are baked into the binaries and the About sheet.

Linux build dependencies (Ubuntu 24.04 names): `build-essential cmake ninja-build pkg-config libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev libjack-jackd2-dev`. Run-time (packages): `libasound2t64 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1 libgl1`; JACK is used when present.

Compile flags: `-Wall -Wextra -Wpedantic -Wconversion` for `rb_core` (warnings are errors in CI), C++20, `-fvisibility=hidden` for plugin binaries. Plugin format factories are the only exported symbols (checked by `scripts/validate-linux.sh`).

## 4. Deliverables of the Linux beta (`0.1.0-beta.1`)

| Artifact | Contents / install location |
| --- | --- |
| `ReverseBack-0.1.0-beta.1-linux-x86_64.tar.gz` | `ReverseBack` (standalone), `ReverseBack.vst3/`, `ReverseBack.clap`, `install.sh`, `uninstall.sh`, README, licences, source notice |
| `reverseback_0.1.0~beta.1_amd64.deb` | `/usr/bin/reverseback`, `/usr/lib/vst3/ReverseBack.vst3`, `/usr/lib/clap/ReverseBack.clap`, desktop entry, icon, licences |
| `ReverseBack-0.1.0-beta.1-vst3-linux-x86_64.zip`, `-clap-...zip`, `-standalone-...tar.gz` | single-format archives |
| `SHA256SUMS` | checksums of all of the above |

`install.sh` installs per user by default (`~/.vst3`, `~/.clap`, `~/.local/bin`, `~/.local/share/applications`, icon) or system-wide with `--system`; it never needs network access and is idempotent. Windows `.zip` artifacts (standalone `.exe`, VST3, CLAP) are produced by CI from the same sources; they are **not** built or tested in the Linux release session and are labelled untested until a Windows run is recorded in `docs/IMPLEMENTATION_STATUS.md`.

## 5. Quality gates for a release

1. `rb_core_tests` and `rb_integration_tests` pass in Release; `rb_core_tests` also pass under ASan+UBSan and TSan.
2. No compiler warnings in `rb_core`.
3. `ldd` on all binaries shows only the run-time packages in section 3; plugin binaries export only format entry points.
4. VST3: `pluginval --strictness-level 5` passes (if validator unavailable, the gap is recorded, not hidden). CLAP: descriptor/entry inspected and instantiated through `clap-validator` or the in-repo `scripts/clap_probe` smoke program.
5. Standalone launches under Xvfb, shows each mode, survives resize, and `--selftest` exits 0.
6. Screenshots of every mode reviewed.
7. `docs/IMPLEMENTATION_STATUS.md` lists every A01-A20 / P01-P12 item with evidence or an explicit "not verified" note (for example real microphone / audio hardware, Windows).

## 6. CI (`.github/workflows/ci.yml`)

- `core` (ubuntu-24.04): `RB_CORE_ONLY` build, CTest, ASan/UBSan job, TSan job.
- `linux` (ubuntu-24.04): full build, integration tests under `xvfb-run`, package, upload artifacts after tests pass.
- `windows` (windows-latest, MSVC 2022): full build, tests, zip standalone/VST3/CLAP, upload artifacts after tests pass.
- `release` (on tag `v*`, needs `linux` and `windows`): creates a GitHub release marked prerelease for `-beta` tags and attaches artifacts and `SHA256SUMS`.
Third-party actions are pinned by version. CI is configuration only until it has run; the status document says which jobs have actually run.

## 7. Versioning and compatibility

Semantic versions; pre-releases `-beta.N`. Plugin ids and parameter ids are frozen from the first beta (`PLUGIN_FORMATS.md` §5). Settings and state carry a `version` integer; readers accept older versions and ignore newer unknown fields.

## 8. Beta scope and honest limitations

In: all three modes, all V1 "fun" features, standalone + VST3 + CLAP on Linux, full GUI, export, presets, settings, disk-backed large-file cache, Windows CI definition.
Not verified in the beta session: real microphone/speaker latency and glitch behaviour on physical hardware (the environment has no audio device), Windows/macOS execution, loading in a commercial DAW. macOS is out of scope for the beta. LV2 and MP3/AAC are future work.
