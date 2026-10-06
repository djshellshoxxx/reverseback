#include "AudioClip.h"
#include "ClipPlayer.h"
#include "SignalTools.h"
#include "LiveReverseTransport.h"

#include <cassert>
#include <cmath>
#include <vector>

using namespace reverseback;

static AudioClip makeMono(std::initializer_list<float> values, double sr=1000.0)
{
    return AudioClip(sr, AudioBuffer{std::vector<float>(values)});
}

static void exactReverseAndDoubleReverse()
{
    const auto clip = makeMono({1,2,3,4});
    ClipPlayer p;
    p.prepare(clip, {0,4}, Direction::Reverse, 1.0, LoopPattern::Once, 0);
    AudioBuffer out(1, std::vector<float>(4));
    p.process(out, 4);
    assert((out[0] == std::vector<float>{4,3,2,1}));

    const AudioClip reversed(1000.0, out);
    p.prepare(reversed, {0,4}, Direction::Reverse, 1.0, LoopPattern::Once, 0);
    AudioBuffer twice(1, std::vector<float>(4));
    p.process(twice, 4);
    assert((twice[0] == std::vector<float>{1,2,3,4}));
}

static void speedControlsOutputLength()
{
    const auto clip = makeMono({0,1,2,3,4,5,6,7});
    ClipPlayer p;
    p.prepare(clip, {0,8}, Direction::Forward, 2.0, LoopPattern::Once, 0);
    AudioBuffer fast(1, std::vector<float>(8));
    const auto writtenFast = p.process(fast, 8);
    assert(writtenFast == 4);

    p.prepare(clip, {0,8}, Direction::Forward, 0.5, LoopPattern::Once, 0);
    AudioBuffer slow(1, std::vector<float>(20));
    const auto writtenSlow = p.process(slow, 20);
    assert(writtenSlow == 16);
}

static void fadesDoNotChangeDuration()
{
    const auto clip = makeMono({1,1,1,1,1,1,1,1});
    ClipPlayer p;
    p.prepare(clip, {0,8}, Direction::Forward, 1.0, LoopPattern::Once, 2);
    AudioBuffer out(1, std::vector<float>(8));
    const auto written = p.process(out, 8);
    assert(written == 8);
    assert(std::fabs(out[0][0]) < 1.0e-6f);
    assert(std::fabs(out[0][7]) < 1.0e-6f);
}

static void pingPongAlternatesDirection()
{
    const auto clip = makeMono({1,2,3});
    ClipPlayer p;
    p.prepare(clip, {0,3}, Direction::Forward, 1.0, LoopPattern::PingPong, 0);
    AudioBuffer out(1, std::vector<float>(6));
    const auto written = p.process(out, 6);
    assert(written == 6);
    assert((out[0] == std::vector<float>{1,2,3,3,2,1}));
}

static void trimSelectionFindsSignalAndPadding()
{
    AudioBuffer b(1, std::vector<float>(1000, 0.0f));
    for (std::size_t i=400;i<600;++i) b[0][i]=0.5f;
    AudioClip clip(1000.0, b);
    const auto sel = findNonSilentSelection(clip, -40.0, 0.02, 0.05);
    assert(sel.begin <= 350);
    assert(sel.end >= 650);
    assert(sel.end <= 1000);
}

static void trimSilentClipReturnsWholeSelection()
{
    AudioClip clip(1000.0, AudioBuffer{std::vector<float>(250,0.0f)});
    const auto sel = findNonSilentSelection(clip, -40.0, 0.02, 0.05);
    assert(sel.begin == 0 && sel.end == 250);
}

static void freezeAdoptsCompletedChunkAndLoops()
{
    LiveReverseTransport live;
    live.start(1,4,0);

    (void) live.processBlock(AudioBuffer{{1,2}},2);
    live.requestFreeze();
    const auto first = live.processBlock(AudioBuffer{{3,4,9,9,9,9}},6);
    assert(live.state() == LiveReverseTransport::State::Frozen);
    assert(live.hasFrozenChunk());
    assert((live.frozenChunk()[0] == std::vector<float>{1,2,3,4}));
    assert((first[0] == std::vector<float>{0,0,4,3,2,1}));

    live.resume();
    assert(live.state() == LiveReverseTransport::State::Filling);
}

static void directionAndSpeedChangesPreserveApproximateSourcePosition()
{
    const auto clip = makeMono({0,1,2,3,4,5,6,7,8,9});
    ClipPlayer p;
    p.prepare(clip, {0,10}, Direction::Forward, 1.0, LoopPattern::Once, 1);
    AudioBuffer first(1, std::vector<float>(4));
    p.process(first, 4);
    const auto before = p.currentSourceFrame();

    p.setDirectionAtCurrentPosition(Direction::Reverse);
    const auto afterDirection = p.currentSourceFrame();
    assert(afterDirection == before || afterDirection + 1 == before || before + 1 == afterDirection);

    p.setSpeedAtCurrentPosition(0.5);
    const auto afterSpeed = p.currentSourceFrame();
    assert(afterSpeed == afterDirection || afterSpeed + 1 == afterDirection || afterDirection + 1 == afterSpeed);
}

int main()
{
    exactReverseAndDoubleReverse();
    speedControlsOutputLength();
    fadesDoNotChangeDuration();
    pingPongAlternatesDirection();
    trimSelectionFindsSignalAndPadding();
    trimSilentClipReturnsWholeSelection();
    freezeAdoptsCompletedChunkAndLoops();
    directionAndSpeedChangesPreserveApproximateSourcePosition();
    return 0;
}
