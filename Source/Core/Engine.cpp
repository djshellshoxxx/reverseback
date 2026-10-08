#include "Engine.h"

#include "Resampler.h"

namespace rb
{
void Engine::prepare (double sampleRate, std::size_t maxBlock)
{
    rate_ = sampleRate;
    maxBlock_ = std::max<std::size_t> (1, maxBlock);
    SincTable::instance();

    rec_.prepare (rate_, maxBlock_, &sink_);
    live_.prepare (rate_, maxBlock_, &sink_);
    file_.configure (maxBlock_, 16.0);
    file_.setOutputRate (rate_);
    file_.setSource (fileSource_.get(), fileSel_);
    fileUnderrun_ = false;

    settingsValid_ = false;
    lastError_ = ErrorCode::None;
    inPeak_ = outPeak_ = 0.0f;
    publish (0);
}

void Engine::configurePlayers() noexcept
{
    const Frame fade = s_.exactSamples ? 0 : framesFor (s_.edgeFadeMs * 0.001, rate_);
    const Frame stopFade = framesFor (s_.stopFadeMs * 0.001, rate_, 1);
    const Frame declick = framesFor (0.002, rate_, 1);

    for (ClipPlayer* p : { &rec_.player(), &file_ })
    {
        p->setOutputRate (rate_);
        p->setFadeFrames (fade);
        p->setStopFadeFrames (stopFade);
        p->setDeclickFrames (declick);
        p->setLoop (s_.loop);
        p->setSpeed (s_.speed);
        p->setDirection (s_.direction);
    }
    live_.setFadeFrames (s_.exactSamples ? 0 : framesFor (s_.liveFadeMs * 0.001, rate_));
    live_.setStopFadeFrames (stopFade);
}

void Engine::applySettings (const Settings& s) noexcept
{
    if (! settingsValid_)
    {
        s_ = s;
        mode_ = s.mode;
        settingsValid_ = true;
        configurePlayers();
        return;
    }

    if (s.mode != mode_)
    {
        stopAll();
        mode_ = s.mode;
    }

    const bool clipChanged = s.speed != s_.speed || s.direction != s_.direction || s.loop != s_.loop
                             || s.edgeFadeMs != s_.edgeFadeMs || s.exactSamples != s_.exactSamples
                             || s.stopFadeMs != s_.stopFadeMs || s.liveFadeMs != s_.liveFadeMs;
    const Settings prev = s_;
    s_ = s;
    if (! clipChanged)
        return;

    // Only push values that changed so a ping-pong player's current direction is not overwritten.
    const Frame fade = s.exactSamples ? 0 : framesFor (s.edgeFadeMs * 0.001, rate_);
    const Frame stopFade = framesFor (s.stopFadeMs * 0.001, rate_, 1);
    for (ClipPlayer* p : { &rec_.player(), &file_ })
    {
        p->setFadeFrames (fade);
        p->setStopFadeFrames (stopFade);
        p->setLoop (s.loop);
        if (s.speed != prev.speed)
            p->setSpeed (s.speed);
        if (s.direction != prev.direction)
            p->setDirection (s.direction);
    }
    live_.setFadeFrames (s.exactSamples ? 0 : framesFor (s.liveFadeMs * 0.001, rate_));
    live_.setStopFadeFrames (stopFade);
}

void Engine::stopAll() noexcept
{
    rec_.stop();
    live_.stop();
    file_.stop();
}

void Engine::stopImmediate() noexcept
{
    rec_.stopImmediate();
    live_.stopImmediate();
    file_.stopImmediate();
}

void Engine::handle (Command&& c) noexcept
{
    auto wrongMode = [this]
    {
        Event e;
        e.type = EventType::Error;
        e.code = ErrorCode::WrongMode;
        sink_.post (std::move (e));
    };
    auto busy = [this]
    {
        Event e;
        e.type = EventType::Error;
        e.code = ErrorCode::Busy;
        sink_.post (std::move (e));
    };

    switch (c.type)
    {
        case CommandType::StartRecord:
        case CommandType::StartHold:
        {
            if (mode_ != Mode::Record)
            {
                wrongMode();
                break;
            }
            RecordSettings rs;
            rs.captureFrames = framesFor (s_.captureSeconds, rate_, 1);
            rs.waitFrames = framesFor (s_.waitSeconds, rate_);
            rs.countdownFrames = framesFor (s_.countdownSeconds, rate_);
            rs.tailGapFrames = framesFor (s_.tailGapSeconds, rate_);
            rs.autoStart = s_.autoStart;
            rs.repeat = s_.repeatSession && s_.loop == LoopPattern::Once;   // C12
            rs.thresholdDb = s_.thresholdDb;
            if (! rec_.start (rs, std::move (c.take), c.type == CommandType::StartHold))
                busy();
            break;
        }
        case CommandType::ReleaseHold: rec_.releaseHold(); break;
        case CommandType::FinishEarly: rec_.finishEarly(); break;
        case CommandType::Replay:
            if (mode_ == Mode::Record)
                rec_.replay();
            else if (mode_ == Mode::File)
                file_.play (true);
            break;
        case CommandType::StartLive:
            if (mode_ != Mode::Live)
                wrongMode();
            else if (! live_.start (std::move (c.live), s_.exactSamples ? 0 : framesFor (s_.liveFadeMs * 0.001, rate_)))
                busy();
            break;
        case CommandType::Freeze: live_.freeze(); break;
        case CommandType::Resume: live_.resume(); break;
        case CommandType::Stop: stopAll(); break;
        case CommandType::SetFile:
        {
            file_.stopImmediate();
            if (fileSource_)
                sink_.retire (std::move (fileSource_));
            fileSource_ = std::move (c.source);
            fileSel_ = { c.a, c.b };
            file_.setSource (fileSource_.get(), fileSel_);
            fileUnderrun_ = false;
            break;
        }
        case CommandType::SetSelection:
            if (fileSource_)
            {
                fileSel_ = { c.a, std::min<Frame> (c.b, fileSource_->frameCount()) };
                file_.setSource (fileSource_.get(), fileSel_);
                fileUnderrun_ = false;
            }
            break;
        case CommandType::PlayFile:
            if (mode_ != Mode::File)
                wrongMode();
            else
            {
                fileUnderrun_ = false;
                file_.play (c.flag);
            }
            break;
        case CommandType::ProvideSpareTake: rec_.provideSpare (std::move (c.take)); break;
        case CommandType::SetTakeSelection: rec_.setTakeSelection ({ c.a, c.b }); break;
    }
}

void Engine::process (const float* const* in, std::size_t inCh, float* const* out, std::size_t frames) noexcept
{
    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t n = std::min (frames - done, maxBlock_);
        const float* inPtr[kMaxChannels] = { nullptr, nullptr };
        for (std::size_t c = 0; c < std::min (inCh, kMaxChannels); ++c)
            inPtr[c] = in != nullptr && in[c] != nullptr ? in[c] + done : nullptr;
        const bool haveIn = inCh > 0 && inPtr[0] != nullptr;
        float* o[2] = { out[0] + done, out[1] + done };

        for (std::size_t i = 0; i < n; ++i)
        {
            o[0][i] = 0.0f;
            o[1][i] = 0.0f;
        }

        float pk = 0.0f;
        if (haveIn)
            for (std::size_t c = 0; c < std::min (inCh, kMaxChannels); ++c)
                for (std::size_t i = 0; i < n; ++i)
                    pk = std::max (pk, std::abs (inPtr[c][i]));
        // Peak with ~150 ms decay so a 30 Hz UI poll cannot miss short peaks between audio blocks.
        inPeak_ = std::max (pk, inPeak_ * static_cast<float> (std::exp (-static_cast<double> (n) / (rate_ * 0.15))));
        if (pk > 1.0f)
            ++overload_;

        const std::size_t effCh = haveIn ? std::min (inCh, kMaxChannels) : 0;
        rec_.process (haveIn ? inPtr : nullptr, effCh, o, n);
        live_.process (haveIn ? inPtr : nullptr, effCh, o, n);

        if (file_.isActive())
        {
            const bool playingBefore = file_.isPlaying();
            file_.process (o, 2, n);
            if (file_.underrun())
            {
                if (! fileUnderrun_)
                {
                    fileUnderrun_ = true;
                    Event e;
                    e.type = EventType::Underrun;
                    sink_.post (std::move (e));
                }
            }
            else if (playingBefore && ! file_.isActive())
            {
                Event e;
                e.type = EventType::PlaybackFinished;
                sink_.post (std::move (e));
            }
        }

        float op = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            op = std::max ({ op, std::abs (o[0][i]), std::abs (o[1][i]) });
        outPeak_ = op;

        done += n;
        ++blocks_;
        publish (n);
    }
}

void Engine::publish (std::size_t) noexcept
{
    Snapshot s;
    s.mode = static_cast<std::uint8_t> (mode_);
    s.recordState = static_cast<std::uint8_t> (rec_.state());
    s.liveState = static_cast<std::uint8_t> (live_.state());
    s.filePlaying = file_.isPlaying() ? 1 : 0;
    s.fileUnderrun = file_.underrun() ? 1 : 0;
    s.recordHeld = rec_.held() && rec_.state() == RecordTransport::State::Recording ? 1 : 0;
    s.takeId = rec_.takeId();
    s.takeFrames = rec_.takeFrames();
    s.frozenGeneration = live_.frozenGeneration();
    s.overloadBlocks = overload_;
    s.sanitizedCount = sanitized_;
    s.inputPeak = inPeak_;
    s.outputPeak = outPeak_;
    s.sampleRate = rate_;
    s.blocks = blocks_;
    s.limiterLatency = limiterLatency_;

    switch (mode_)
    {
        case Mode::Record:
            s.stateFrame = rec_.stateFrame();
            s.stateLength = rec_.stateLength();
            s.countdownLeft = rec_.countdownLeft();
            s.playhead = rec_.playhead();
            break;
        case Mode::Live:
            s.liveChunkIndex = live_.playingChunk();
            s.liveCaptureChunk = live_.captureChunk();
            s.liveSlot = live_.displaySlot();
            s.liveCaptureSlot = live_.captureSlot();
            s.stateFrame = live_.captureOffset();
            s.stateLength = live_.chunkFrames();
            s.fillProgress = live_.fillProgress();
            break;
        case Mode::File:
            s.playhead = file_.positionNorm();
            break;
    }
    snapshot_.store (s);
}
}  // namespace rb
