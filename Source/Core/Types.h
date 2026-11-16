// ReverseBack core: shared vocabulary types. Framework-free (no JUCE).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rb
{
using Frame = std::uint64_t;

enum class Mode : std::uint8_t { Record, Live, File };
enum class Direction : std::uint8_t { Forward, Backward };
enum class LoopPattern : std::uint8_t { Once, Loop, PingPong };

enum class ErrorCode : std::uint8_t
{
    None,
    FellBehind,            // "Audio processing fell behind"
    SpareTakeUnavailable,  // Repeat Session could not get a fresh take buffer in time
    Underrun,              // disk cache block was not resident
    DspFault,              // non-finite output detected
    WrongMode,
    Busy,
    TakeTooSmall           // the recording buffer was prepared for different settings than the engine now has
};

constexpr std::size_t kMaxChannels = 2;

// Half-open range of frames inside a clip.
struct Selection
{
    Frame begin = 0;
    Frame end = 0;

    constexpr Frame length() const noexcept { return end > begin ? end - begin : 0; }
    constexpr bool empty() const noexcept { return end <= begin; }
    constexpr bool operator== (const Selection& o) const noexcept { return begin == o.begin && end == o.end; }
};

// llround(seconds * rate), never negative. Clamped to at least `minimum`.
inline Frame framesFor (double seconds, double rate, Frame minimum = 0) noexcept
{
    const double f = std::round (seconds * rate);
    const Frame v = f <= 0.0 ? Frame { 0 } : static_cast<Frame> (f);
    return std::max (v, minimum);
}

inline float dbToGain (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }

inline float gainToDb (float g) noexcept { return g > 1.0e-9f ? 20.0f * std::log10 (g) : -180.0f; }

constexpr double kMinSelectionSeconds = 0.05;   // V1 section 2.3
constexpr double kMaxCaptureSeconds = 60.0;     // V1 section 4.2
constexpr double kMaxWaitSeconds = 30.0;
constexpr double kMinChunkSeconds = 0.1;
constexpr double kMaxChunkSeconds = 5.0;
constexpr double kMaxLiveDelaySeconds = 30.0;
constexpr double kMinSpeed = 0.5;
constexpr double kMaxSpeed = 2.0;
constexpr double kPreRollSeconds = 0.2;         // V1 section 3
constexpr double kVoiceSustainSeconds = 0.05;
constexpr std::size_t kTakeBudgetBytes = 256u * 1024u * 1024u;   // V1 section 5.4
}  // namespace rb
