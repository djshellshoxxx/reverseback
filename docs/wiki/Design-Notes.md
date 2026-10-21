# Design Notes

Source: `spec/REVERSEBACK_V1.md`.

- **Exact reversal.** At 1x speed, the output is the input in reverse sample order, channel order preserved. A default 3 ms fade changes a few boundary samples to avoid clicks (section 5.2).
- **Speed changes use a real resampler.** Naive linear interpolation is ruled out as the final quality. Pitch-preserving stretch is outside V1.
- **Smooth edges, not perfect seams.** Live chunks use fades inside each chunk, with no overlap. The spec asks for the wording "Smooth edges" because perfect seams are not promised.
- **Memory and thread rules.** Defined in spec section 5.4 (audio thread constraints).
- **Engine reuse.** One engine is meant to serve the standalone app and a later VST3 effect.
