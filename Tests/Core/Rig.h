// Test rig: drives an Engine like the JUCE processor does and plays the "UI" role (supplies take
// buffers, answers spare-buffer requests, drains events and retired references).
#pragma once

#include "Engine.h"
#include "TestUtil.h"

#include <functional>

namespace rbt
{
inline float rampInput (Frame i) { return static_cast<float> (static_cast<double> (i + 1) * 1.0e-6); }

struct Rig
{
    explicit Rig (double sampleRate = 48000.0, std::size_t maxBlock = 1024) : rate (sampleRate), maxBlockFrames (maxBlock)
    {
        eng.prepare (rate, maxBlock);
        s.exactSamples = true;   // exact sample order unless a test asks for fades
        eng.applySettings (s);
    }

    void apply (const Settings& next)
    {
        s = next;
        eng.applySettings (s);
    }

    // Audio-thread entry points are wrapped in the allocation guard (A15): any heap use here fails the test.
    void send (Command&& c)
    {
#ifndef RB_NO_ALLOC_HOOK
        const long before = allocViolations.load();
        {
            NoAlloc guard;
            eng.handle (std::move (c));
        }
        if (allocViolations.load() != before)
            fail (__FILE__, __LINE__, "allocation inside Engine::handle");
#else
        eng.handle (std::move (c));
#endif
    }

    void startRecord (bool hold = false, int channels = 1)
    {
        const Frame cap = hold ? framesFor (kMaxCaptureSeconds, rate, 1) : framesFor (s.captureSeconds, rate, 1);
        Command c;
        c.type = hold ? CommandType::StartHold : CommandType::StartRecord;
        c.take = AudioClip::create (rate, channels, cap);
        send (std::move (c));
    }

    void startLive (Frame forceSlots = 0, int channels = 1)
    {
        Command c;
        c.type = CommandType::StartLive;
        c.live = LiveStorage::create (rate, channels, framesFor (s.liveChunkSeconds, rate, 1), framesFor (s.liveDelaySeconds, rate), forceSlots);
        send (std::move (c));
    }

    void cmd (CommandType t, bool flag = false, Frame a = 0, Frame b = 0)
    {
        Command c;
        c.type = t;
        c.flag = flag;
        c.a = a;
        c.b = b;
        send (std::move (c));
    }

    void service()
    {
        Event e;
        while (eng.events().poll (e))
        {
            if (e.type == EventType::NeedSpareTake && autoSpare)
            {
                Command c;
                c.type = CommandType::ProvideSpareTake;
                c.take = AudioClip::create (e.rate, e.channels, e.a);
                send (std::move (c));
            }
            events.push_back (std::move (e));
            e = Event {};
        }
        eng.events().drainRetired();
    }

    // Runs `frames` frames in blocks of `block`, servicing events between blocks.
    void run (Frame frames, std::size_t block = 256)
    {
        std::vector<float> a (block), b (block), oL (block), oR (block);
        Frame left = frames;
        while (left > 0)
        {
            const std::size_t m = static_cast<std::size_t> (std::min<Frame> (block, left));
            for (std::size_t i = 0; i < m; ++i)
            {
                a[i] = input (frame + i);
                b[i] = -a[i];
            }
            const float* in[2] = { a.data(), b.data() };
            float* o[2] = { oL.data(), oR.data() };
#ifndef RB_NO_ALLOC_HOOK
            const long allocBefore = allocViolations.load();
            {
                NoAlloc guard;
                eng.process (in, static_cast<std::size_t> (inChannels), o, m);
            }
            if (allocViolations.load() != allocBefore)
                fail (__FILE__, __LINE__, "allocation inside Engine::process");
#else
            eng.process (in, static_cast<std::size_t> (inChannels), o, m);
#endif
            out[0].insert (out[0].end(), oL.begin(), oL.begin() + static_cast<long> (m));
            out[1].insert (out[1].end(), oR.begin(), oR.begin() + static_cast<long> (m));
            frame += m;
            left -= m;
            service();
        }
    }

    int count (EventType t) const
    {
        int n = 0;
        for (const auto& e : events)
            n += e.type == t ? 1 : 0;
        return n;
    }

    std::vector<std::shared_ptr<const AudioClip>> takes() const
    {
        std::vector<std::shared_ptr<const AudioClip>> v;
        for (const auto& e : events)
            if (e.type == EventType::TakeCompleted)
                v.push_back (e.clip);
        return v;
    }

    bool hasError (ErrorCode code) const
    {
        for (const auto& e : events)
            if (e.type == EventType::Error && e.code == code)
                return true;
        return false;
    }

    double rate;
    std::size_t maxBlockFrames;
    Engine eng;
    Settings s;
    Frame frame = 0;
    int inChannels = 1;
    bool autoSpare = true;
    std::function<float (Frame)> input = rampInput;
    Planar out;
    std::vector<Event> events;
};

inline bool allZero (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < std::min(to, v.size()); ++i)
        if (v[i] != 0.0f)
            return false;
    return true;
}
}  // namespace rbt
