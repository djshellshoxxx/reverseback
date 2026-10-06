#include "ReverseBuffer.h"
#include "FrameScheduler.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using reverseback::AudioBuffer;
using reverseback::FrameScheduler;

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

int main()
{
    testExactMonoReverse();
    testStereoChannelsKeepIdentity();
    testRecordFiveWaitTwoAt48k();
    testSchedulerBoundariesDoNotDependOnCallbackBlockSize();
    std::cout << "ReverseBack core tests passed\n";
    return 0;
}
