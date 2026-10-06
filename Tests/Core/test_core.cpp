#include "ReverseBuffer.h"
#include "FrameScheduler.h"
#include "RecordTransport.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using reverseback::AudioBuffer;
using reverseback::FrameScheduler;
using reverseback::RecordTransport;

static void testExactMonoReverse()
{
    AudioBuffer input{{1.0f, 2.0f, 3.0f, 4.0f}};
    const auto output = reverseback::reverseFrames(input);
    assert((output[0] == std::vector<float>{4.0f, 3.0f, 2.0f, 1.0f}));
}

static void testStereoChannelsKeepIdentity()
{
    AudioBuffer input{
        {1.0f, 2.0f, 3.0f},
        {10.0f, 20.0f, 30.0f}
    };
    const auto output = reverseback::reverseFrames(input);
    assert((output[0] == std::vector<float>{3.0f, 2.0f, 1.0f}));
    assert((output[1] == std::vector<float>{30.0f, 20.0f, 10.0f}));
}

static void testRecordFiveWaitTwoAt48k()
{
    FrameScheduler scheduler{48000};
    scheduler.configureRecord(5.0, 2.0);

    assert(scheduler.captureFrames() == 240000);
    assert(scheduler.waitFrames() == 96000);
    assert(scheduler.playbackStartFrame() == 336000);
    assert(scheduler.playbackFrames() == 240000);
}

static void testSchedulerBoundariesDoNotDependOnCallbackBlockSize()
{
    constexpr std::uint64_t expectedPlaybackStart = 336000;
    for (const std::uint32_t blockSize : {1u, 64u, 127u, 256u, 512u, 1024u})
    {
        FrameScheduler scheduler{48000};
        scheduler.configureRecord(5.0, 2.0);

        std::uint64_t cursor = 0;
        bool sawPlaybackBoundary = false;
        while (cursor < expectedPlaybackStart + blockSize)
        {
            const auto events = scheduler.eventsInBlock(cursor, blockSize);
            for (const auto& event : events)
            {
                if (event.type == FrameScheduler::EventType::PlaybackStart)
                {
                    assert(event.absoluteFrame == expectedPlaybackStart);
                    sawPlaybackBoundary = true;
                }
            }
            cursor += blockSize;
        }
        assert(sawPlaybackBoundary);
    }
}

static void testRecordTransportCapturesWaitsAndPlaysBackwards()
{
    RecordTransport transport;
    transport.start(1, 4, 2);

    const auto output = transport.processBlock(AudioBuffer{{1, 2, 3, 4, 9, 9, 9, 9}}, 8);

    assert(transport.state() == RecordTransport::State::Playing);
    assert((output[0] == std::vector<float>{0, 0, 0, 0, 0, 0, 4, 3}));
    assert(transport.hasRetainedTake());
    assert((transport.retainedTake()[0] == std::vector<float>{1, 2, 3, 4}));

    const auto tail = transport.processBlock(AudioBuffer{{0, 0}}, 2);
    assert((tail[0] == std::vector<float>{2, 1}));
    assert(transport.state() == RecordTransport::State::Ready);
}

static void testStopDuringRecordingPreservesPreviousCompleteTake()
{
    RecordTransport transport;

    transport.start(1, 3, 0);
    (void) transport.processBlock(AudioBuffer{{1, 2, 3}}, 3);
    (void) transport.processBlock(AudioBuffer{{0, 0, 0}}, 3);
    assert(transport.state() == RecordTransport::State::Ready);
    assert((transport.retainedTake()[0] == std::vector<float>{1, 2, 3}));

    transport.start(1, 4, 0);
    (void) transport.processBlock(AudioBuffer{{8, 9}}, 2);
    transport.stop();

    assert(transport.state() == RecordTransport::State::Ready);
    assert((transport.retainedTake()[0] == std::vector<float>{1, 2, 3}));
}

static void testInputIsIgnoredDuringWaitAndPlayback()
{
    RecordTransport transport;
    transport.start(1, 2, 2);

    const auto first = transport.processBlock(AudioBuffer{{1, 2, 99, 98, 97, 96}}, 6);
    assert((first[0] == std::vector<float>{0, 0, 0, 0, 2, 1}));
    assert((transport.retainedTake()[0] == std::vector<float>{1, 2}));
    assert(transport.state() == RecordTransport::State::Ready);
}

static void testReplayUsesRetainedTakeWithoutRecording()
{
    RecordTransport transport;
    transport.start(1, 3, 0);
    (void) transport.processBlock(AudioBuffer{{3, 4, 5}}, 3);
    (void) transport.processBlock(AudioBuffer{{0, 0, 0}}, 3);

    assert(transport.replay());
    assert(transport.state() == RecordTransport::State::Playing);

    const auto replay = transport.processBlock(AudioBuffer{{99, 99, 99}}, 3);
    assert((replay[0] == std::vector<float>{5, 4, 3}));
    assert(transport.state() == RecordTransport::State::Ready);
}

int main()
{
    testExactMonoReverse();
    testStereoChannelsKeepIdentity();
    testRecordFiveWaitTwoAt48k();
    testSchedulerBoundariesDoNotDependOnCallbackBlockSize();
    testRecordTransportCapturesWaitsAndPlaysBackwards();
    testStopDuringRecordingPreservesPreviousCompleteTake();
    testInputIsIgnoredDuringWaitAndPlayback();
    testReplayUsesRetainedTakeWithoutRecording();
    std::cout << "ReverseBack core tests passed\n";
    return 0;
}
