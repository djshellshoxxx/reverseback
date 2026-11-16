#include "Presets.h"

namespace rb
{
const std::array<BuiltinPreset, 4>& builtinPresets()
{
    static const std::array<BuiltinPreset, 4> presets = { {
        { "Say Something",
          { Mode::Record, 5.0, 2.0, 0.5, 2.0, 1.0, LoopPattern::Once, Direction::Backward, false } },
        { "Tiny Syllables",
          { Mode::Live, 5.0, 2.0, 0.25, 0.25, 1.0, LoopPattern::Once, Direction::Backward, false } },
        { "Backwards Conversation",
          { Mode::Live, 5.0, 2.0, 1.0, 0.5, 1.0, LoopPattern::Once, Direction::Backward, false } },
        { "Long Phrase",
          { Mode::Record, 10.0, 2.0, 0.5, 2.0, 1.0, LoopPattern::Once, Direction::Backward, false } },
    } };
    return presets;
}

SurpriseResult makeSurprise (SimpleRng& rng)
{
    static constexpr double speeds[] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
    SurpriseResult r;
    r.speed = speeds[rng.below (5)];
    r.direction = rng.below (2) == 0 ? Direction::Backward : Direction::Forward;
    switch (rng.below (3))
    {
        case 0: r.loop = LoopPattern::Once; break;
        case 1: r.loop = LoopPattern::Loop; break;
        default: r.loop = LoopPattern::PingPong; break;
    }
    return r;
}
}  // namespace rb
