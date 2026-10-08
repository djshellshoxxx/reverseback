#include "LiveTransport.h"

namespace rb
{
std::shared_ptr<LiveStorage> LiveStorage::create (double rate, int channels, Frame chunkFrames, Frame delayFrames, Frame forceSlots)
{
    if (chunkFrames == 0 || channels < 1 || channels > static_cast<int> (kMaxChannels))
        return nullptr;
    auto s = std::make_shared<LiveStorage>();
    s->rate = rate;
    s->channels = channels;
    s->W = chunkFrames;
    s->D = delayFrames;
    s->slots = forceSlots != 0 ? forceSlots : (delayFrames + chunkFrames - 1) / chunkFrames + 4;
    const std::size_t bytes = static_cast<std::size_t> (s->slots) * static_cast<std::size_t> (channels)
                              * static_cast<std::size_t> (chunkFrames) * sizeof (float);
    if (bytes > 512u * 1024u * 1024u)
        return nullptr;
    s->data.assign (bytes / sizeof (float), 0.0f);
    s->tags.assign (static_cast<std::size_t> (s->slots), -1);
    s->env.assign (static_cast<std::size_t> (s->slots) * kEnvPoints * 2, 0.0f);
    return s;
}

void LiveTransport::prepare (double rate, std::size_t, EventSink* sink)
{
    rate_ = rate;
    sink_ = sink;
    state_ = State::Ready;
    storage_.reset();
    tail_ = {};
    stopFade_ = framesFor (0.01, rate, 1);
}

void LiveTransport::invalidateTags() noexcept
{
    if (storage_)
        std::fill (storage_->tags.begin(), storage_->tags.end(), std::int64_t { -1 });
}

bool LiveTransport::start (std::shared_ptr<LiveStorage> storage, Frame fadeFrames) noexcept
{
    if (state_ != State::Ready || ! storage || storage->W == 0)
        return false;

    if (storage_ && ! (tail_.active && tail_.storage == storage_))
        sink_->retire (std::move (storage_));
    storage_ = std::move (storage);
    W_ = storage_->W;
    D_ = storage_->D;
    S_ = storage_->slots;
    ch_ = storage_->channels;
    fade_ = fadeFrames;
    base_ = 0;
    t_ = 0;
    playingChunk_ = -1;
    captureDone_ = false;
    invalidateTags();
    state_ = State::Filling;
    return true;
}

float LiveTransport::fillProgress() const noexcept
{
    if (state_ == State::Filling)
        return static_cast<float> (static_cast<double> (t_) / static_cast<double> (W_ + D_));
    return state_ == State::Ready ? 0.0f : 1.0f;
}

float LiveTransport::chunkGain (Frame u) const noexcept
{
    const Frame fe = std::min<Frame> (fade_, W_ / 2);
    if (fe == 0)
        return 1.0f;
    float g = 1.0f;
    if (u < fe)
        g *= fadeShape (u, fe);
    const Frame rem = W_ - u;   // >= 1
    if (rem <= fe)
        g *= fadeShape (rem - 1, fe);
    return g;
}

void LiveTransport::fault (ErrorCode code) noexcept
{
    stopImmediate();
    Event e;
    e.type = EventType::Error;
    e.code = code;
    sink_->post (std::move (e));
}

void LiveTransport::beginTail (bool loop, Frame slot, Frame u, Frame maxFrames) noexcept
{
    if (! storage_ || maxFrames == 0)
        return;
    if (tail_.active && tail_.storage && tail_.storage != storage_)
        sink_->retire (std::move (tail_.storage));
    tail_.active = true;
    tail_.storage = storage_;
    tail_.slot = slot;
    tail_.u = u;
    tail_.loop = loop;
    tail_.total = tail_.remaining = std::min (stopFade_, maxFrames);
}

void LiveTransport::stop() noexcept
{
    switch (state_)
    {
        case State::Ready:
            return;
        case State::Running:
        case State::Freezing:
            if (outputActive() && playingChunk_ >= 0 && ! (state_ == State::Freezing && captureDone_ && t_ >= adopt_))
            {
                const Frame u = (t_ - D_) % W_;
                beginTail (false, slotOf (static_cast<std::int64_t> ((t_ - D_) / W_) - 1), u, W_ - u);
            }
            break;
        case State::Frozen:
            beginTail (true, frozenSlot_, (t_ - adopt_) % W_, stopFade_);
            break;
        case State::Filling:
            break;
    }
    state_ = State::Ready;
    captureDone_ = false;
    playingChunk_ = -1;
}

void LiveTransport::stopImmediate() noexcept
{
    stop();
    tail_.active = false;
    if (tail_.storage && tail_.storage != storage_)
        sink_->retire (std::move (tail_.storage));
    tail_.storage.reset();
}

void LiveTransport::freeze() noexcept
{
    if (state_ != State::Filling && state_ != State::Running)
        return;
    captureDone_ = false;
    tc_ = (t_ / W_ + 1) * W_;
    state_ = State::Freezing;
}

void LiveTransport::resume() noexcept
{
    if (state_ != State::Frozen && state_ != State::Freezing)
        return;
    Frame readSlot = base_;
    if (state_ == State::Frozen)
    {
        readSlot = frozenSlot_;
        beginTail (true, frozenSlot_, (t_ - adopt_) % W_, stopFade_);
    }
    else if (outputActive())
    {
        const Frame u = (t_ - D_) % W_;
        readSlot = slotOf (static_cast<std::int64_t> ((t_ - D_) / W_) - 1);
        beginTail (false, readSlot, u, W_ - u);
    }
    // The new run starts in the slot after the one a fading tail may still be reading.
    base_ = (readSlot + 1) % S_;
    t_ = 0;
    playingChunk_ = -1;
    captureDone_ = false;
    invalidateTags();
    state_ = State::Filling;
}

void LiveTransport::captureInto (const float* const* in, std::size_t inCh, std::size_t pos, std::size_t m) noexcept
{
    const std::int64_t chunk = static_cast<std::int64_t> (t_ / W_);
    const Frame o = t_ % W_;
    const Frame slot = slotOf (chunk);
    if (o == 0)
    {
        storage_->tags[static_cast<std::size_t> (slot)] = chunk;
        std::fill_n (storage_->env.begin() + static_cast<std::ptrdiff_t> (slot * LiveStorage::kEnvPoints * 2), LiveStorage::kEnvPoints * 2, 0.0f);
    }
    float* envp = storage_->env.data() + static_cast<std::size_t> (slot) * LiveStorage::kEnvPoints * 2;

    for (int c = 0; c < ch_; ++c)
    {
        float* dst = storage_->plane (slot, c) + o;
        if (in != nullptr && inCh > 0)
            std::memcpy (dst, in[std::min (static_cast<std::size_t> (c), inCh - 1)] + pos, m * sizeof (float));
        else
            std::fill (dst, dst + m, 0.0f);
    }

    const float* d0 = storage_->plane (slot, 0) + o;
    const float* d1 = ch_ > 1 ? storage_->plane (slot, 1) + o : nullptr;
    for (std::size_t i = 0; i < m; ++i)
    {
        const float v = d1 != nullptr ? 0.5f * (d0[i] + d1[i]) : d0[i];
        const std::size_t b = static_cast<std::size_t> (((o + i) * LiveStorage::kEnvPoints) / W_);
        envp[b * 2] = std::min (envp[b * 2], v);
        envp[b * 2 + 1] = std::max (envp[b * 2 + 1], v);
    }
}

bool LiveTransport::renderChunk (float* const* out, std::size_t pos, std::size_t m) noexcept
{
    const Frame tt = t_ - D_;
    const std::int64_t k = static_cast<std::int64_t> (tt / W_) - 1;   // chunk being played
    const Frame u0 = tt % W_;
    const Frame slot = slotOf (k);
    if (k < 0 || storage_->tags[static_cast<std::size_t> (slot)] != k)
        return false;   // schedule broke: the slot was recycled or never filled
    playingChunk_ = k;

    const float* s0 = storage_->plane (slot, 0);
    const float* s1 = ch_ > 1 ? storage_->plane (slot, 1) : s0;
    for (std::size_t i = 0; i < m; ++i)
    {
        const Frame u = u0 + i;
        const Frame idx = W_ - 1 - u;
        const float g = chunkGain (u);
        out[0][pos + i] += s0[idx] * g;
        out[1][pos + i] += s1[idx] * g;
    }
    return true;
}

void LiveTransport::renderFrozen (float* const* out, std::size_t pos, std::size_t m) noexcept
{
    const float* s0 = storage_->plane (frozenSlot_, 0);
    const float* s1 = ch_ > 1 ? storage_->plane (frozenSlot_, 1) : s0;
    Frame u = (t_ - adopt_) % W_;
    for (std::size_t i = 0; i < m; ++i)
    {
        const float g = chunkGain (u);
        const Frame idx = W_ - 1 - u;
        out[0][pos + i] += s0[idx] * g;
        out[1][pos + i] += s1[idx] * g;
        if (++u == W_)
            u = 0;
    }
}

void LiveTransport::renderTail (float* const* out, std::size_t frames) noexcept
{
    if (! tail_.active || ! tail_.storage)
        return;
    const LiveStorage& st = *tail_.storage;
    const float* s0 = st.plane (tail_.slot, 0);
    const float* s1 = st.channels > 1 ? st.plane (tail_.slot, 1) : s0;
    const Frame Wt = st.W;
    for (std::size_t i = 0; i < frames && tail_.remaining > 0; ++i)
    {
        Frame u = tail_.u;
        if (u >= Wt)
        {
            if (! tail_.loop)
            {
                tail_.remaining = 0;
                break;
            }
            u = tail_.u = 0;
        }
        const float ramp = static_cast<float> (tail_.remaining) / static_cast<float> (tail_.total);
        const Frame idx = Wt - 1 - u;
        out[0][i] += s0[idx] * ramp;
        out[1][i] += s1[idx] * ramp;
        ++tail_.u;
        --tail_.remaining;
    }
    if (tail_.remaining == 0)
    {
        tail_.active = false;
        if (tail_.storage != storage_)
            sink_->retire (std::move (tail_.storage));
        tail_.storage.reset();
    }
}

void LiveTransport::process (const float* const* in, std::size_t inCh, float* const* out, std::size_t frames) noexcept
{
    std::size_t pos = 0;
    while (pos < frames && state_ != State::Ready)
    {
        const std::size_t avail = frames - pos;

        if (state_ == State::Frozen)
        {
            std::size_t m = avail;
            const Frame toLoop = W_ - ((t_ - adopt_) % W_);
            m = static_cast<std::size_t> (std::min<Frame> (m, toLoop));
            renderFrozen (out, pos, m);
            t_ += m;
            pos += m;
            continue;
        }

        const bool capturing = ! (state_ == State::Freezing && captureDone_);
        Frame m = avail;
        if (capturing)
            m = std::min<Frame> (m, W_ - (t_ % W_));
        const Frame toOut = t_ < W_ + D_ ? W_ + D_ - t_ : W_ - ((t_ - D_) % W_);
        m = std::min (m, toOut);
        if (state_ == State::Freezing && captureDone_)
            m = std::min (m, adopt_ - t_);
        m = std::max<Frame> (m, 1);

        if (capturing)
            captureInto (in, inCh, pos, static_cast<std::size_t> (m));
        if (outputActive() && ! renderChunk (out, pos, static_cast<std::size_t> (m)))
        {
            fault (ErrorCode::FellBehind);
            break;
        }
        t_ += m;
        pos += static_cast<std::size_t> (m);

        if (state_ == State::Filling && t_ >= W_ + D_)
            state_ = State::Running;

        if (state_ == State::Freezing && ! captureDone_ && t_ == tc_)
        {
            captureDone_ = true;
            frozenSlot_ = slotOf (static_cast<std::int64_t> (t_ / W_) - 1);
            if (t_ >= W_ + D_)
                adopt_ = D_ + ((t_ - D_ + W_ - 1) / W_) * W_;   // next output-chunk boundary at or after t_c
            else
                adopt_ = t_;                                      // nothing audible yet: start the loop at once
        }
        if (state_ == State::Freezing && captureDone_ && t_ >= adopt_)
        {
            state_ = State::Frozen;
            ++frozenGen_;
            playingChunk_ = -1;
            Event e;
            e.type = EventType::FrozenReady;
            e.a = frozenGen_;
            sink_->post (std::move (e));
        }
    }
    renderTail (out, frames);
}
}  // namespace rb
