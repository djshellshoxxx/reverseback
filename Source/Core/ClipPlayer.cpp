#include "ClipPlayer.h"

#include "Resampler.h"

namespace rb
{
namespace
{
constexpr float kPiF = 3.14159265358979323846f;

// Raised-cosine fade shape for frame index idx (0-based) of a fade F frames long. Never exactly 0 or 1.
inline float fadeShape (Frame idx, Frame F) noexcept
{
    return 0.5f * (1.0f - std::cos (kPiF * static_cast<float> (idx + 1) / static_cast<float> (F + 1)));
}
}  // namespace

void ClipPlayer::configure (std::size_t maxBlock, double maxRatio)
{
    maxBlock_ = std::max<std::size_t> (1, maxBlock);
    maxRatio_ = std::max (1.0, maxRatio);
    SincTable::instance();

    tmpStore_.assign (kMaxChannels * maxBlock_, 0.0f);
    winCapacity_ = static_cast<std::size_t> (std::ceil (static_cast<double> (maxBlock_) * maxRatio_))
                   + static_cast<std::size_t> (std::ceil (2.0 * SincTable::kHalfWidth * maxRatio_)) + 16;
    winStore_.assign (kMaxChannels * winCapacity_, 0.0f);
    for (std::size_t c = 0; c < kMaxChannels; ++c)
    {
        tmp_[c] = tmpStore_.data() + c * maxBlock_;
        win_[c] = winStore_.data() + c * winCapacity_;
    }
    updateRatio();
}

void ClipPlayer::updateRatio() noexcept
{
    const double srcRate = src_ != nullptr ? srcRate_ : outRate_;
    const double r = srcRate / outRate_ * speed_;
    ratio_ = std::clamp (r, 1.0 / 64.0, maxRatio_);
}

void ClipPlayer::beginSegment (double u0) noexcept
{
    u0_ = std::clamp (u0, 0.0, static_cast<double> (len_));
    n_ = 0;
    const double remaining = static_cast<double> (len_) - u0_;
    M_ = remaining > 0.0 ? std::max<Frame> (1, static_cast<Frame> (std::llround (remaining / ratio_))) : 0;
    fadeEff_ = std::min<Frame> (fade_, M_ / 2);
}

void ClipPlayer::setOutputRate (double rate) noexcept
{
    if (rate <= 0.0 || rate == outRate_)
        return;
    const double uc = curU();
    outRate_ = rate;
    updateRatio();
    if (! finished_ && len_ > 0)
        beginSegment (uc);
}

void ClipPlayer::setSource (const ClipSource* src, Selection sel) noexcept
{
    state_ = State::Idle;
    src_ = src;
    sel_ = sel;
    len_ = src != nullptr ? sel.length() : 0;
    clipChannels_ = src != nullptr ? std::min (src->channels(), static_cast<int> (kMaxChannels)) : 1;
    srcRate_ = src != nullptr ? src->sampleRate() : outRate_;
    finished_ = true;
    underrun_ = false;
    u0_ = 0.0;
    n_ = M_ = passFrames_ = fadeEff_ = 0;
    gain_ = gainTarget_ = 1.0f;
    pendingDir_ = pendingSpeed_ = pendingRestart_ = false;
    updateRatio();
}

void ClipPlayer::setSpeed (double speed) noexcept
{
    speed = std::clamp (speed, 0.25, 4.0);
    if (state_ == State::Playing)
    {
        if (std::abs (speed - (pendingSpeed_ ? newSpeed_ : speed_)) > 1.0e-12)
        {
            pendingSpeed_ = true;
            newSpeed_ = speed;
            raisePending();
        }
        return;
    }
    if (speed == speed_)
        return;
    const double uc = curU();
    speed_ = speed;
    updateRatio();
    if (! finished_ && len_ > 0)
        beginSegment (uc);
}

void ClipPlayer::setDirection (Direction d) noexcept
{
    if (state_ == State::Playing)
    {
        if (d != (pendingDir_ ? newDir_ : dir_))
        {
            pendingDir_ = true;
            newDir_ = d;
            raisePending();
        }
        return;
    }
    if (d == dir_)
        return;
    const double uc = curU();
    dir_ = d;
    if (! finished_ && len_ > 0)
        beginSegment (std::clamp (static_cast<double> (len_) - 1.0 - uc, 0.0, static_cast<double> (len_)));
}

void ClipPlayer::raisePending() noexcept
{
    gainTarget_ = 0.0f;
    gainStep_ = 1.0f / static_cast<float> (declick_);
}

void ClipPlayer::applyPending() noexcept
{
    double uc = curU();
    if (pendingDir_ && newDir_ != dir_)
    {
        dir_ = newDir_;
        uc = std::clamp (static_cast<double> (len_) - 1.0 - uc, 0.0, static_cast<double> (len_));
    }
    if (pendingSpeed_)
    {
        speed_ = newSpeed_;
        updateRatio();
    }
    const bool restart = pendingRestart_;
    if (restart)
        uc = 0.0;
    pendingDir_ = pendingSpeed_ = pendingRestart_ = false;
    finished_ = false;
    beginSegment (uc);
    passFrames_ = restart ? 0 : fadeEff_;   // a rebase ramps up with the declick instead of a pass fade-in
    gainTarget_ = 1.0f;
    gainStep_ = 1.0f / static_cast<float> (declick_);
}

void ClipPlayer::play (bool fromStart) noexcept
{
    if (src_ == nullptr || len_ == 0)
        return;
    underrun_ = false;

    switch (state_)
    {
        case State::Playing:
            if (fromStart)
            {
                pendingRestart_ = true;
                raisePending();
            }
            return;

        case State::Stopping:
            state_ = State::Playing;
            if (fromStart)
            {
                pendingRestart_ = true;
                raisePending();
            }
            else
            {
                gainTarget_ = 1.0f;
                gainStep_ = 1.0f / static_cast<float> (declick_);
            }
            return;

        case State::Idle:
            if (fromStart || finished_ || n_ >= M_)
            {
                finished_ = false;
                beginSegment (0.0);
                passFrames_ = 0;
                gain_ = gainTarget_ = 1.0f;
            }
            else
            {
                beginSegment (curU());
                passFrames_ = fadeEff_;
                gain_ = 0.0f;
                gainTarget_ = 1.0f;
                gainStep_ = 1.0f / static_cast<float> (declick_);
            }
            state_ = State::Playing;
            return;
    }
}

void ClipPlayer::stop() noexcept
{
    if (state_ != State::Playing)
        return;
    if (pendingDir_ || pendingSpeed_ || pendingRestart_)
        applyPending();
    state_ = State::Stopping;
    gainTarget_ = 0.0f;
    gainStep_ = 1.0f / static_cast<float> (stopFade_);
}

void ClipPlayer::stopImmediate() noexcept
{
    if (pendingDir_ || pendingSpeed_ || pendingRestart_)
        applyPending();
    state_ = State::Idle;
    gain_ = gainTarget_ = 1.0f;
}

bool ClipPlayer::advancePass() noexcept
{
    switch (loop_)
    {
        case LoopPattern::Once:
            state_ = State::Idle;
            finished_ = true;
            u0_ = 0.0;
            n_ = M_ = 0;
            gain_ = gainTarget_ = 1.0f;
            return false;

        case LoopPattern::PingPong:
            dir_ = dir_ == Direction::Forward ? Direction::Backward : Direction::Forward;
            [[fallthrough]];
        case LoopPattern::Loop:
            beginSegment (0.0);
            passFrames_ = 0;
            return true;
    }
    return false;
}

float ClipPlayer::edgeGain (Frame passFrame, Frame n) const noexcept
{
    float g = 1.0f;
    if (fadeEff_ != 0)
    {
        if (passFrame < fadeEff_)
            g *= fadeShape (passFrame, fadeEff_);
        const Frame remaining = M_ - n;   // >= 1 while rendering
        if (remaining <= fadeEff_)
            g *= fadeShape (remaining - 1, fadeEff_);
    }
    return g;
}

bool ClipPlayer::renderRaw (std::size_t m) noexcept
{
    const int cc = clipChannels_;
    const std::int64_t b = static_cast<std::int64_t> (sel_.begin);
    const std::int64_t last = static_cast<std::int64_t> (sel_.end) - 1;
    const bool bwd = dir_ == Direction::Backward;

    // Exact path: unity ratio on integer positions copies samples untouched (exact reversal).
    if (ratio_ == 1.0 && u0_ == std::floor (u0_))
    {
        const std::int64_t u = static_cast<std::int64_t> (u0_) + static_cast<std::int64_t> (n_);
        if (! bwd)
            return src_->read (b + u, m, tmp_);
        const std::int64_t low = last - (u + static_cast<std::int64_t> (m) - 1);
        if (! src_->read (low, m, tmp_))
            return false;
        for (int c = 0; c < cc; ++c)
            std::reverse (tmp_[c], tmp_[c] + m);
        return true;
    }

    const double r = ratio_;
    const double fc = std::min (1.0, 1.0 / r);
    const double H = static_cast<double> (SincTable::kHalfWidth) / fc;
    auto posOf = [&] (Frame i) noexcept
    {
        const double u = u0_ + static_cast<double> (n_ + i) * r;
        return bwd ? static_cast<double> (last) - u : static_cast<double> (b) + u;
    };

    const double p0 = posOf (0), p1 = posOf (m - 1);
    const double lo = std::min (p0, p1), hi = std::max (p0, p1);
    std::int64_t wLo = std::max<std::int64_t> (b, static_cast<std::int64_t> (std::floor (lo - H)));
    std::int64_t wHi = std::min<std::int64_t> (last, static_cast<std::int64_t> (std::ceil (hi + H)));
    if (wHi < wLo)
        wLo = wHi = std::clamp (static_cast<std::int64_t> (lo), b, last);
    std::size_t count = static_cast<std::size_t> (wHi - wLo + 1);
    if (count > winCapacity_)
    {
        count = winCapacity_;
        wHi = wLo + static_cast<std::int64_t> (count) - 1;
    }
    if (! src_->read (wLo, count, win_))
        return false;

    const SincTable& table = SincTable::instance();
    for (std::size_t i = 0; i < m; ++i)
    {
        const double p = posOf (i);
        const std::int64_t k0 = static_cast<std::int64_t> (std::ceil (p - H));
        const std::int64_t k1 = static_cast<std::int64_t> (std::floor (p + H));
        double acc0 = 0.0, acc1 = 0.0, wsum = 0.0;
        for (std::int64_t k = k0; k <= k1; ++k)
        {
            const double w = static_cast<double> (table.eval (fc * std::abs (static_cast<double> (k) - p)));
            std::int64_t kk = k < b ? b : (k > last ? last : k);   // edge hold at the selection ends
            kk = kk < wLo ? wLo : (kk > wHi ? wHi : kk);
            const std::size_t j = static_cast<std::size_t> (kk - wLo);
            acc0 += w * static_cast<double> (win_[0][j]);
            if (cc > 1)
                acc1 += w * static_cast<double> (win_[1][j]);
            wsum += w;
        }
        if (wsum < 1.0e-12)
        {
            const std::int64_t kn = std::clamp (static_cast<std::int64_t> (std::llround (p)), wLo, wHi);
            acc0 = static_cast<double> (win_[0][kn - wLo]);
            acc1 = cc > 1 ? static_cast<double> (win_[1][kn - wLo]) : 0.0;
            wsum = 1.0;
        }
        tmp_[0][i] = static_cast<float> (acc0 / wsum);
        if (cc > 1)
            tmp_[1][i] = static_cast<float> (acc1 / wsum);
    }
    return true;
}

std::size_t ClipPlayer::process (float* const* out, std::size_t outChannels, std::size_t frames) noexcept
{
    std::size_t done = 0;
    const int cc = clipChannels_;

    while (done < frames && state_ != State::Idle)
    {
        if (n_ >= M_)
        {
            if (! advancePass())
                break;
            continue;
        }

        const bool pending = pendingDir_ || pendingSpeed_ || pendingRestart_;
        if (pending && gain_ <= 0.0f)
        {
            applyPending();
            continue;
        }

        std::size_t m = std::min ({ frames - done, maxBlock_, static_cast<std::size_t> (M_ - n_) });
        if (gainTarget_ < gain_)   // ramping down: end the sub-block exactly where the ramp reaches zero
        {
            const auto kz = static_cast<std::size_t> (std::ceil (gain_ / gainStep_));
            m = std::min (m, std::max<std::size_t> (1, kz));
        }

        if (! renderRaw (m))
        {
            underrun_ = true;
            state_ = State::Idle;   // pause; position is retained
            gain_ = gainTarget_ = 1.0f;
            break;
        }

        const Frame pf0 = passFrames_, n0 = n_;
        for (std::size_t i = 0; i < m; ++i)
        {
            const float g = edgeGain (pf0 + i, n0 + i) * gain_;
            if (gain_ != gainTarget_)
            {
                gain_ = gain_ < gainTarget_ ? std::min (gain_ + gainStep_, gainTarget_)
                                            : std::max (gain_ - gainStep_, gainTarget_);
            }
            for (std::size_t oc = 0; oc < outChannels; ++oc)
                out[oc][done + i] += tmp_[std::min (oc, static_cast<std::size_t> (cc - 1))][i] * g;
        }

        n_ += m;
        passFrames_ += m;
        done += m;

        if (state_ == State::Stopping && gain_ <= 0.0f)
        {
            state_ = State::Idle;
            gain_ = gainTarget_ = 1.0f;
            break;
        }
        if (n_ >= M_ && ! advancePass())
            break;
    }
    return done;
}

float ClipPlayer::positionNorm() const noexcept
{
    if (src_ == nullptr || len_ == 0)
        return 0.0f;
    const double f = std::clamp (curU() / static_cast<double> (len_), 0.0, 1.0);
    return static_cast<float> (dir_ == Direction::Forward ? f : 1.0 - f);
}
}  // namespace rb
