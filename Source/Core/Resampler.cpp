#include "Resampler.h"

namespace rb
{
namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;
        if (term < 1.0e-18 * sum)
            break;
    }
    return sum;
}
}  // namespace

SincTable::SincTable()
{
    const std::size_t n = static_cast<std::size_t> (kHalfWidth) * kPointsPerUnit + 2;
    table_.resize (n);
    const double pi = 3.14159265358979323846;
    const double norm = besselI0 (kKaiserBeta);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double x = static_cast<double> (i) / kPointsPerUnit;
        const double w = x / kHalfWidth;
        const double win = w >= 1.0 ? 0.0 : besselI0 (kKaiserBeta * std::sqrt (1.0 - w * w)) / norm;
        const double s = x < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
        table_[i] = static_cast<float> (s * win);
    }
}

const SincTable& SincTable::instance()
{
    static const SincTable table;
    return table;
}
}  // namespace rb
