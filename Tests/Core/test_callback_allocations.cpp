#include "LiveReverseTransport.h"
#include "RecordTransport.h"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<bool> trackAllocations{false};
static std::atomic<std::size_t> allocationCount{0};

void* operator new(std::size_t size)
{
    if (trackAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace reverseback;

int main()
{
    {
        RecordTransport record;
        RecordTransport::RecordSettings settings;
        settings.sampleRate = 1000.0;
        settings.channels = 1;
        settings.captureSeconds = 0.25;
        settings.waitSeconds = 0.0;
        settings.repeatSession = true;
        settings.readyGapSeconds = 0.05;
        record.prepare(settings);
        record.startPrepared();

        AudioBuffer input(1, std::vector<float>(1000, 0.25f));
        AudioBuffer output(1, std::vector<float>(1000, 0.0f));

        allocationCount.store(0);
        trackAllocations.store(true);
        record.processBlockInto(input, 1000, output);
        trackAllocations.store(false);

        assert(allocationCount.load() == 0);
    }

    {
        LiveReverseTransport live;
        live.start(1, 100, 300);
        AudioBuffer input(1, std::vector<float>(1000, 0.25f));
        AudioBuffer output(1, std::vector<float>(1000, 0.0f));

        allocationCount.store(0);
        trackAllocations.store(true);
        live.processBlockInto(input, 450, output);
        live.requestFreeze();
        live.processBlockInto(input, 550, output);
        trackAllocations.store(false);

        assert(allocationCount.load() == 0);
    }

    return 0;
}
