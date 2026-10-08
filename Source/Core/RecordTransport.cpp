#include "RecordTransport.h"

namespace rb
{
void RecordTransport::prepare (double rate, std::size_t maxBlock, EventSink* sink)
{
    rate_ = rate;
    sink_ = sink;
    minFrames_ = framesFor (kMinSelectionSeconds, rate, 1);
    maxHoldFrames_ = framesFor (kMaxCaptureSeconds, rate, 1);
    ring_.configure (rate, static_cast<int> (kMaxChannels), kPreRollSeconds);
    trigger_.prepare (rate);
    playingTake_ = nullptr;
    player_.configure (maxBlock, 16.0);
    player_.setOutputRate (rate);
    player_.setSource (nullptr, {});
    state_ = State::Ready;
    current_.reset();
    spare_.reset();
}

void RecordTransport::discardCurrent() noexcept
{
    if (current_)
        sink_->retire (std::move (current_));
    current_.reset();
}

void RecordTransport::requestSpare() noexcept
{
    Event e;
    e.type = EventType::NeedSpareTake;
    e.a = spareCapacity_;
    e.channels = spareChannels_;
    e.rate = spareRate_;
    sink_->post (std::move (e));
}

bool RecordTransport::start (const RecordSettings& s, std::shared_ptr<AudioClip> take, bool held) noexcept
{
    if (state_ != State::Ready || ! take || take->sealed())
        return false;
    if (player_.isActive())
        player_.stopImmediate();

    cfg_ = s;
    current_ = std::move (take);
    takeCh_ = current_->channels();
    spareCapacity_ = current_->capacity();
    spareChannels_ = takeCh_;
    spareRate_ = current_->sampleRate();
    held_ = held;
    repeat_ = s.repeat && ! held;
    captured_ = 0;
    ring_.setChannels (takeCh_);
    trigger_.arm (s.thresholdDb);

    if (held)
    {
        target_ = std::min (maxHoldFrames_, current_->capacity());
        state_ = State::Recording;
    }
    else if (s.countdownFrames > 0)
    {
        cdLeft_ = s.countdownFrames;
        state_ = State::Countdown;
    }
    else
    {
        beginArmedOrRecording();
    }

    if (repeat_)
        requestSpare();
    return true;
}

void RecordTransport::provideSpare (std::shared_ptr<AudioClip> take) noexcept
{
    if (! take || take->sealed())
        return;
    if (spare_)
        sink_->retire (std::move (spare_));
    spare_ = std::move (take);
}

void RecordTransport::beginArmedOrRecording() noexcept
{
    if (cfg_.autoStart && ! held_)
    {
        ring_.clear();
        trigger_.reset();
        state_ = State::Armed;
    }
    else
    {
        beginRecording();
    }
}

void RecordTransport::beginRecording() noexcept
{
    captured_ = 0;
    target_ = std::min (cfg_.captureFrames, current_->capacity());
    if (target_ == 0)
    {
        cancelCapture (true);
        return;
    }
    state_ = State::Recording;
}

void RecordTransport::onTrigger() noexcept
{
    const Frame target = std::min (cfg_.captureFrames, current_->capacity());
    const Frame n = std::min (ring_.available(), target);
    float* dst[kMaxChannels] = { current_->channel (0), takeCh_ > 1 ? current_->channel (1) : nullptr };
    ring_.copyOut (dst, n);
    captured_ = n;
    target_ = target;
    state_ = State::Recording;
    if (captured_ >= target_)
        closeCapture();
}

void RecordTransport::cancelCapture (bool tooShort) noexcept
{
    discardCurrent();
    state_ = State::Ready;
    Event e;
    e.type = tooShort ? EventType::TooShort : EventType::CaptureCancelled;
    sink_->post (std::move (e));
}

void RecordTransport::closeCapture() noexcept
{
    if (captured_ < minFrames_)
    {
        cancelCapture (true);
        return;
    }

    current_->seal (captured_);
    std::shared_ptr<const AudioClip> clip = current_;
    current_.reset();

    Event e;
    e.type = EventType::TakeCompleted;
    e.clip = clip;
    e.a = clip->frameCount();
    sink_->post (std::move (e));

    if (retained_)
    {
        // The player must not keep a pointer to a take that the control thread is about to free.
        player_.setSource (nullptr, {});
        playingTake_ = nullptr;
        sink_->retire (std::move (retained_));
    }
    retained_ = std::move (clip);
    takeSel_ = {};
    ++takeId_;

    waitLeft_ = cfg_.waitFrames;
    state_ = State::Waiting;
}

void RecordTransport::finishEarly() noexcept
{
    if (state_ == State::Recording)
        closeCapture();
}

void RecordTransport::releaseHold() noexcept
{
    if (state_ == State::Recording && held_)
        closeCapture();
}

void RecordTransport::setTakeSelection (Selection sel) noexcept
{
    if (! retained_)
        return;
    if (sel.empty() || sel.end > retained_->frameCount() || sel.length() < minFrames_)
        takeSel_ = {};
    else
        takeSel_ = sel;
}

void RecordTransport::beginPlaying() noexcept
{
    const Frame total = retained_->frameCount();
    Selection sel = takeSel_.empty() ? Selection { 0, total } : takeSel_;
    if (player_.isActive() && playingTake_ == retained_.get())
    {
        player_.play (true);   // restart with a declick instead of cutting the audio
    }
    else
    {
        player_.setOutputRate (rate_);
        player_.setSource (retained_.get(), sel);
        player_.play (true);
        playingTake_ = retained_.get();
    }
    state_ = State::Playing;
}

bool RecordTransport::replay() noexcept
{
    if (! retained_ || (state_ != State::Ready && state_ != State::Playing))
        return false;
    beginPlaying();
    return true;
}

void RecordTransport::onPlayFinished() noexcept
{
    Event e;
    e.type = EventType::PlaybackFinished;
    sink_->post (std::move (e));
    if (repeat_)
    {
        gapLeft_ = cfg_.tailGapFrames;
        gapExtra_ = 0;
        state_ = State::ReadyGap;
    }
    else
    {
        state_ = State::Ready;
    }
}

void RecordTransport::stop() noexcept
{
    switch (state_)
    {
        case State::Ready:
            if (player_.isActive())
                player_.stop();
            return;
        case State::Countdown:
        case State::Armed:
        case State::Recording:
            discardCurrent();
            {
                Event e;
                e.type = EventType::CaptureCancelled;
                sink_->post (std::move (e));
            }
            break;
        case State::Playing:
            player_.stop();
            break;
        case State::Waiting:
        case State::ReadyGap:
            break;
    }
    if (spare_)
        sink_->retire (std::move (spare_));
    spare_.reset();
    repeat_ = false;
    state_ = State::Ready;
}

void RecordTransport::stopImmediate() noexcept
{
    const bool wasPlaying = player_.isActive();
    stop();
    if (wasPlaying)
        player_.stopImmediate();
}

Frame RecordTransport::stateFrame() const noexcept
{
    switch (state_)
    {
        case State::Countdown: return cfg_.countdownFrames - cdLeft_;
        case State::Recording: return captured_;
        case State::Waiting: return cfg_.waitFrames - waitLeft_;
        case State::ReadyGap: return cfg_.tailGapFrames - std::min (cfg_.tailGapFrames, gapLeft_);
        default: return 0;
    }
}

Frame RecordTransport::stateLength() const noexcept
{
    switch (state_)
    {
        case State::Countdown: return cfg_.countdownFrames;
        case State::Recording: return target_;
        case State::Waiting: return cfg_.waitFrames;
        case State::ReadyGap: return cfg_.tailGapFrames;
        case State::Playing: return player_.segmentFrames();
        default: return 0;
    }
}

void RecordTransport::copyInput (const float* const* in, std::size_t inCh, std::size_t pos, std::size_t m) noexcept
{
    for (int c = 0; c < takeCh_; ++c)
    {
        float* dst = current_->channel (c) + captured_;
        if (in != nullptr && inCh > 0)
        {
            const float* src = in[std::min (static_cast<std::size_t> (c), inCh - 1)] + pos;
            std::memcpy (dst, src, m * sizeof (float));
        }
        else
        {
            std::fill (dst, dst + m, 0.0f);
        }
    }
}

void RecordTransport::process (const float* const* in, std::size_t inCh, float* const* out, std::size_t frames) noexcept
{
    std::size_t pos = 0;
    while (pos < frames)
    {
        const std::size_t avail = frames - pos;
        switch (state_)
        {
            case State::Ready:
                if (player_.isActive())
                {
                    float* o[2] = { out[0] + pos, out[1] + pos };
                    player_.process (o, 2, avail);
                }
                pos = frames;
                break;

            case State::Countdown:
            {
                const std::size_t m = static_cast<std::size_t> (std::min<Frame> (avail, cdLeft_));
                cdLeft_ -= m;
                pos += m;
                if (cdLeft_ == 0)
                    beginArmedOrRecording();
                break;
            }

            case State::Armed:
            {
                bool fired = false;
                while (pos < frames && ! fired)
                {
                    float f[kMaxChannels] = { 0.0f, 0.0f };
                    float msq = 0.0f;
                    for (int c = 0; c < takeCh_; ++c)
                    {
                        f[c] = (in != nullptr && inCh > 0) ? in[std::min (static_cast<std::size_t> (c), inCh - 1)][pos] : 0.0f;
                        msq += f[c] * f[c];
                    }
                    ring_.push (f);
                    ++pos;
                    fired = trigger_.processSquare (msq / static_cast<float> (takeCh_));
                }
                if (fired)
                    onTrigger();
                break;
            }

            case State::Recording:
            {
                const std::size_t m = static_cast<std::size_t> (std::min<Frame> (avail, target_ - captured_));
                copyInput (in, inCh, pos, m);
                captured_ += m;
                pos += m;
                if (captured_ >= target_)
                    closeCapture();
                break;
            }

            case State::Waiting:
            {
                if (waitLeft_ == 0)
                {
                    beginPlaying();
                    break;
                }
                const std::size_t m = static_cast<std::size_t> (std::min<Frame> (avail, waitLeft_));
                waitLeft_ -= m;
                pos += m;
                if (waitLeft_ == 0)
                    beginPlaying();
                break;
            }

            case State::Playing:
            {
                float* o[2] = { out[0] + pos, out[1] + pos };
                const std::size_t got = player_.process (o, 2, avail);
                pos += got;
                if (player_.underrun())
                {
                    Event e;
                    e.type = EventType::Underrun;
                    sink_->post (std::move (e));
                    stop();
                    break;
                }
                if (! player_.isActive())
                    onPlayFinished();
                else if (got == 0)
                    pos = frames;   // defensive: never spin
                break;
            }

            case State::ReadyGap:
            {
                if (gapLeft_ > 0)
                {
                    const std::size_t m = static_cast<std::size_t> (std::min<Frame> (avail, gapLeft_));
                    gapLeft_ -= m;
                    pos += m;
                    break;
                }
                if (spare_)
                {
                    current_ = std::move (spare_);
                    spare_.reset();
                    takeCh_ = current_->channels();
                    ring_.setChannels (takeCh_);
                    beginRecording();
                    if (state_ == State::Recording)
                        requestSpare();   // buffer for the cycle after this one
                    break;
                }
                // No fresh buffer yet: extend the gap (up to 2 s) rather than allocate on the audio thread.
                const Frame limit = framesFor (2.0, rate_, 1);
                const std::size_t m = static_cast<std::size_t> (std::max<Frame> (1, std::min<Frame> (avail, limit - std::min (gapExtra_, limit))));
                gapExtra_ += m;
                pos += m;
                if (gapExtra_ >= limit)
                {
                    Event e;
                    e.type = EventType::Error;
                    e.code = ErrorCode::SpareTakeUnavailable;
                    sink_->post (std::move (e));
                    state_ = State::Ready;
                    repeat_ = false;
                }
                break;
            }
        }
    }
}
}  // namespace rb
