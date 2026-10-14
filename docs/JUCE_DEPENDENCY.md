# JUCE dependency decision

ReverseBack pins JUCE **9.0.3** for the Windows standalone application.

- Upstream: juce-framework/JUCE
- Tag: 9.0.3
- Release date: 2026-09-28
- Core DSP/tests remain framework-free and do not require JUCE.
- The standalone target obtains JUCE with CMake FetchContent only when `REVERSEBACK_BUILD_APP=ON`.

## Licensing

The ReverseBack repository's own source license does not replace JUCE's licence terms. Anyone distributing a JUCE-linked ReverseBack binary must comply with the JUCE 9 EULA under a qualifying JUCE plan or use an applicable open-source licensing route. This repository does not vendor JUCE source code.

The build deliberately pins an exact release instead of following a moving branch so source, CI and released binaries can be reproduced.
