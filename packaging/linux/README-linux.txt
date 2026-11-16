ReverseBack 0.1.0-beta.1 (Linux x86_64)
=======================================

What is in this folder
  ReverseBack         standalone application
  ReverseBack.vst3/   VST3 plugin
  ReverseBack.clap    CLAP plugin
  install.sh          installs all three (per user by default)
  uninstall.sh        removes them again

Quick start
  1. ./install.sh                (or: sudo ./install.sh --system)
  2. Launch "ReverseBack" from your application menu, or run: reverseback
  3. Pick "Record & Reverse", press the big button, and say something.
     You will hear it backwards after the wait time.
  4. In a DAW: rescan plugins, then insert "ReverseBack" on a track.

Needs these system libraries (present on most desktop Linux installs):
  ALSA, X11, Xext, Xrandr, Xinerama, Xcursor, FreeType, Fontconfig, OpenGL (libGL).
  Debian/Ubuntu: sudo apt install libasound2t64 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1 libgl1
  The standalone uses ALSA (PipeWire/PulseAudio via their ALSA layers) and JACK when installed.

Check the install without audio hardware:  ReverseBack --selftest   (prints PASS)

Beta notes
  * Verified in the build environment: engine tests, plugin behaviour tests, GUI control tests, VST3/CLAP validation.
  * NOT verified: real microphone/speaker latency, long hardware sessions, specific DAWs. Please report problems.
  * Live Reverse can feed back into a microphone. Use headphones.
  * Plugin timing counts processed audio frames; keep the host's audio running while recording.

Licence
  Source code is MIT. These compiled binaries include JUCE 8 (AGPLv3), clap-juce-extensions (MIT)
  and the Inter font (SIL OFL 1.1), so the binaries are distributed under AGPL-3.0-or-later terms.
  Complete corresponding source: see SOURCE.txt. Third-party notices: THIRD_PARTY_LICENSES.md.
