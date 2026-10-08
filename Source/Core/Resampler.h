// Windowed-sinc prototype shared by every sample-rate / speed conversion (ENGINE_DESIGN section 4).
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace rb
{
class SincTable
{
public:
    static constexpr int kHalfWidth = 16;        // zero crossings on each side at unity cutoff
    static constexpr int kPointsPerUnit = 2048;
    static constexpr double kKaiserBeta = 9.0;

    // Built once (thread-safe static). Call during prepare() so the first use is off the audio thread.
    static const SincTable& instance();

    // P(x) = sinc(x) * kaiser(x / kHalfWidth), x >= 0.
    float eval (double ax) const noexcept
    {
        if (ax >= static_cast<double> (kHalfWidth))
            return 0.0f;
        const double f = ax * kPointsPerUnit;
        const std::size_t i = static_cast<std::size_t> (f);
        const float fr = static_cast<float> (f - static_cast<double> (i));
        return table_[i] + (table_[i + 1] - table_[i]) * fr;
    }

private:
    SincTable();
    std::vector<float> table_;
};
}  // namespace rb
