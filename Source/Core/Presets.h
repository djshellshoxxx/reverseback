// Built-in presets and Surprise Settings (V1 section 3, ENGINE_DESIGN section 12).
#pragma once

#include "Types.h"

#include <array>
#include <cstdint>

namespace rb
{
struct PresetValues
{
    Mode mode = Mode::Record;
    double captureSeconds = 5.0;
    double waitSeconds = 2.0;
    double liveChunkSeconds = 0.5;
    double liveDelaySeconds = 2.0;
    double speed = 1.0;
    LoopPattern loop = LoopPattern::Once;
    Direction direction = Direction::Backward;
    bool repeatSession = false;
};

struct BuiltinPreset
{
    const char* name;
    PresetValues values;
};

const std::array<BuiltinPreset, 4>& builtinPresets();

// Tiny deterministic generator so Surprise is testable.
class SimpleRng
{
public:
    explicit SimpleRng (std::uint64_t seed = 0x9E3779B97F4A7C15ull) : s_ (seed != 0 ? seed : 1) {}
    std::uint32_t next() noexcept
    {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 7;
        s_ ^= s_ << 17;
        return static_cast<std::uint32_t> (s_ >> 16);
    }
    std::uint32_t below (std::uint32_t n) noexcept { return n == 0 ? 0 : next() % n; }

private:
    std::uint64_t s_;
};

struct SurpriseResult
{
    double speed = 1.0;
    Direction direction = Direction::Backward;
    LoopPattern loop = LoopPattern::Once;
};

// Picks sensible clip speed / direction / loop only. Never touches devices, gain, capture length or recording.
SurpriseResult makeSurprise (SimpleRng& rng);
}  // namespace rb
