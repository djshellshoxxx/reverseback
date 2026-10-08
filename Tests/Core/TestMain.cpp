#include "TestHarness.h"

#include <chrono>
#include <cstdlib>
#include <new>

#ifdef RB_JUCE_TESTS
 #include <juce_gui_basics/juce_gui_basics.h>
#endif

namespace rbt
{
std::atomic<long> allocViolations { 0 };
thread_local int noAllocDepth = 0;

int runAll (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    for (const auto& c : registry())
    {
        if (filter != nullptr && std::strstr (c.name, filter) == nullptr)
            continue;
        const int before = failureCount();
        const auto t0 = std::chrono::steady_clock::now();
        try
        {
            c.fn();
        }
        catch (const TestAbort&)
        {
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        const bool ok = failureCount() == before;
        std::printf ("[%s] %-48s %8.1f ms\n", ok ? " ok " : "FAIL", c.name, ms);
        ++ran;
        if (! ok)
            ++failedTests;
    }
    std::printf ("\n%d tests, %d failed, %d checks\n", ran, failedTests, checkCount());
    return failedTests == 0 && ran > 0 ? 0 : 1;
}
}  // namespace rbt

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // a crash must not hide which test was running
#ifdef RB_JUCE_TESTS
    juce::ScopedJuceInitialiser_GUI juceInit;   // created inside main so JUCE's own statics exist first
#endif
    return rbt::runAll (argc, argv);
}

#ifndef RB_NO_ALLOC_HOOK
// Count heap allocations made while a NoAlloc guard is active on the calling thread.
void* operator new (std::size_t n)
{
    if (rbt::noAllocDepth > 0)
        rbt::allocViolations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t n)
{
    if (rbt::noAllocDepth > 0)
        rbt::allocViolations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif
