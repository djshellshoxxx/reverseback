# Third-party notices

ReverseBack's own source code is © 2026 Sheldon Davidson and released under the MIT licence (`LICENSE`).
The compiled beta binaries combine that code with the components below. Because JUCE is used under its
**AGPLv3** option, **the compiled binaries are distributed under AGPL-3.0-or-later** (`LICENSES/AGPL-3.0.txt`);
the complete corresponding source is the repository at the release tag (see `SOURCE.txt` in each package).
Using a different licence for binaries requires a commercial JUCE licence.

| Component | Version | Licence | Use |
| --- | --- | --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.15 | AGPLv3 (dual-licensed with the commercial JUCE licence) | GUI, audio devices, file formats, plugin wrappers. JUCE bundles further components under their own licences (FreeType, libpng, zlib, FLAC, OggVorbis, jpeglib and others) listed in JUCE's `LICENSE.md`. |
| VST3 SDK | 3.8 (bundled with JUCE) | MIT, © Steinberg Media Technologies GmbH | VST3 plugin interface |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | commit `7adee3a` | MIT | CLAP wrapper |
| [CLAP](https://github.com/free-audio/clap) and clap-helpers | submodules of the above | MIT | CLAP headers and helpers |
| [Inter](https://rsms.me/inter/) | 4.0 | SIL Open Font Licence 1.1 (`Assets/Inter-LICENSE.txt`) | User-interface typeface (embedded) |

VST is a trademark of Steinberg Media Technologies GmbH. CLAP is a trademark of its respective owners.
"ReverseBack" and "Circuit Drift Labs" are unregistered trademarks of Sheldon Davidson (`COPYRIGHT-TRADEMARK.md`).
