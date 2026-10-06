#include "RecordTransport.h"

#include <cassert>
#include <vector>

using namespace reverseback;

static AudioBuffer mono(std::initializer_list<float> v)
{
    return AudioBuffer{std::vector<float>(v)};
}

static void countdownThenRecords()
{
    RecordTransport t;
    RecordTransport::RecordSettings s;
    s.sampleRate = 1000.0;
    s.channels = 1;
    s.captureSeconds = 0.25;
    s.waitSeconds = 0.0;
    s.countdownSeconds = 0.1;
    t.prepare(s);
    t.startPrepared();

    assert(t.state() == RecordTransport::State::Countdown);
    (void)t.processBlock(AudioBuffer{std::vector<float>(100, 8.0f)}, 100);
    assert(t.state() == RecordTransport::State::Recording);
    assert(!t.hasRetainedTake());
}

static void holdShortTapCancelsAndPreservesTake()
{
    RecordTransport t;
    t.start(1, 3, 0);
    (void)t.processBlock(mono({1,2,3}), 3);
    (void)t.processBlock(mono({0,0,0}), 3);
    assert(t.hasRetainedTake());

    RecordTransport::RecordSettings s;
    s.sampleRate = 1000.0;
    s.channels = 1;
    s.captureSeconds = 1.0;
    s.waitSeconds = 0.0;
    t.prepare(s);
    t.startPrepared(true);
    (void)t.processBlock(AudioBuffer{std::vector<float>(40,9.0f)},40);

    assert(t.finishEarly() == RecordTransport::FinishResult::CancelledTooShort);
    assert(t.state() == RecordTransport::State::Ready);
    assert((t.retainedTake()[0] == std::vector<float>{1,2,3}));
}

static void holdReleaseAcceptsValidEarlyTake()
{
    RecordTransport t;
    RecordTransport::RecordSettings s;
    s.sampleRate = 1000.0;
    s.channels = 1;
    s.captureSeconds = 1.0;
    s.waitSeconds = 0.1;
    t.prepare(s);
    t.startPrepared(true);

    (void)t.processBlock(AudioBuffer{std::vector<float>(75,0.25f)},75);
    assert(t.finishEarly() == RecordTransport::FinishResult::Accepted);
    assert(t.retainedTake()[0].size() == 75);
    assert(t.state() == RecordTransport::State::Waiting);
}

static void voiceTriggerIncludesPreRollWithinTotalDuration()
{
    RecordTransport t;
    RecordTransport::RecordSettings s;
    s.sampleRate = 1000.0;
    s.channels = 1;
    s.captureSeconds = 0.25;
    s.waitSeconds = 0.0;
    s.autoStart = true;
    s.triggerThresholdDb = -20.0;
    s.triggerSustainSeconds = 0.05;
    s.preRollSeconds = 0.2;
    t.prepare(s);
    t.startPrepared();

    assert(t.state() == RecordTransport::State::Armed);

    (void)t.processBlock(AudioBuffer{std::vector<float>(100,0.01f)},100);
    assert(t.state() == RecordTransport::State::Armed);

    (void)t.processBlock(AudioBuffer{std::vector<float>(50,0.5f)},50);
    assert(t.state() == RecordTransport::State::Recording);
    assert(t.capturedFrames() == 150);

    (void)t.processBlock(AudioBuffer{std::vector<float>(100,0.25f)},100);
    assert(t.hasRetainedTake());
    assert(t.retainedTake()[0].size() == 250);
}

static void repeatSessionUsesReadyGapWithoutConcurrentCapture()
{
    RecordTransport t;
    RecordTransport::RecordSettings s;
    s.sampleRate = 1000.0;
    s.channels = 1;
    s.captureSeconds = 0.05;
    s.waitSeconds = 0.0;
    s.repeatSession = true;
    s.readyGapSeconds = 0.02;
    t.prepare(s);
    t.startPrepared();

    auto out = t.processBlock(AudioBuffer{std::vector<float>(120,1.0f)},120);
    (void)out;
    assert(t.state() == RecordTransport::State::Recording ||
           t.state() == RecordTransport::State::ReadyGap);
}

int main()
{
    countdownThenRecords();
    holdShortTapCancelsAndPreservesTake();
    holdReleaseAcceptsValidEarlyTake();
    voiceTriggerIncludesPreRollWithinTotalDuration();
    repeatSessionUsesReadyGapWithoutConcurrentCapture();
    return 0;
}
