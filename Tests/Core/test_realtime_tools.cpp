#include "CommandQueue.h"
#include "RealtimeTools.h"

#include <cassert>
#include <cmath>
#include <limits>

using namespace reverseback;

int main()
{
    CommandQueue<int,4> q;
    assert(q.push(1));
    assert(q.push(2));
    assert(q.push(3));
    assert(!q.push(4));
    assert(q.pop().value() == 1);
    assert(q.pop().value() == 2);
    assert(q.pop().value() == 3);
    assert(!q.pop().has_value());

    SmoothedGain gain;
    gain.prepare(1000.0, 0.02, -6.0);
    const auto before = gain.nextGain();
    gain.setTargetDb(0.0);
    float after = before;
    for(int i=0;i<20;++i) after=gain.nextGain();
    assert(after > before);
    assert(std::fabs(after - 1.0f) < 1.0e-4f);

    VoiceTrigger trigger;
    trigger.prepare(1000.0, 1, -20.0, 0.05, 0.2);
    float quiet[1]{0.01f};
    for(int i=0;i<100;++i) assert(!trigger.processFrame(quiet));
    float loud[1]{0.5f};
    for(int i=0;i<49;++i) assert(!trigger.processFrame(loud));
    assert(trigger.processFrame(loud));
    assert(trigger.preRoll()[0].size() == 150);

    PeakProtector limiter(-1.0f);
    assert(limiter.process(2.0f) <= limiter.ceilingLinear());
    assert(limiter.process(-2.0f) >= -limiter.ceilingLinear());
    assert(limiter.process(std::numeric_limits<float>::infinity()) == 0.0f);
    return 0;
}
