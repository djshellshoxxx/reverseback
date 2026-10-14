// Helpers shared by the JUCE integration tests: fixtures, temp folders and message-loop pumping.
#pragma once

#include "TestHarness.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace rbt
{
struct TempDir
{
    TempDir() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("rb-test", "", false))
    {
        dir.createDirectory();
    }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File file (const juce::String& name) const { return dir.getChildFile (name); }
    juce::File dir;
};

// Runs the JUCE message loop until `done()` or the timeout expires.
inline bool pumpUntil (const std::function<bool()>& done, int timeoutMs = 10000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (done())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    return done();
}

inline void pump (int ms = 50) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// Writes a deterministic test signal: channel c, frame i -> a smooth function in [-0.8, 0.8].
inline float testSample (int channel, juce::int64 i)
{
    return 0.8f * static_cast<float> (std::sin (0.001 * static_cast<double> (i) * (channel + 1) + 0.3 * channel));
}

inline bool writeFixture (const juce::File& file, juce::AudioFormat& format, double rate, int channels, int bits, juce::int64 frames,
                          bool floatingPoint = false)
{
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (channels).withBitsPerSample (bits);
    if (floatingPoint)
        options = options.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    constexpr int block = 4096;
    std::vector<float> planes[2];
    for (juce::int64 start = 0; start < frames; start += block)
    {
        const int n = static_cast<int> (std::min<juce::int64> (block, frames - start));
        const float* ptrs[2] = { nullptr, nullptr };
        for (int c = 0; c < channels; ++c)
        {
            planes[c].resize (static_cast<std::size_t> (n));
            for (int i = 0; i < n; ++i)
                planes[c][static_cast<std::size_t> (i)] = testSample (c, start + i);
            ptrs[c] = planes[c].data();
        }
        if (! writer->writeFromFloatArrays (ptrs, channels, n))
            return false;
    }
    writer.reset();
    return true;
}

// Reads a whole audio file back as planar floats.
struct ReadBack
{
    bool ok = false;
    double rate = 0.0;
    int channels = 0;
    bool floatingPoint = false;
    unsigned bits = 0;
    std::vector<std::vector<float>> data;
};

inline ReadBack readFile (const juce::File& f)
{
    ReadBack r;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr)
        return r;
    r.rate = reader->sampleRate;
    r.channels = static_cast<int> (reader->numChannels);
    r.floatingPoint = reader->usesFloatingPointData;
    r.bits = reader->bitsPerSample;
    r.data.assign (static_cast<std::size_t> (r.channels), std::vector<float> (static_cast<std::size_t> (reader->lengthInSamples)));
    std::vector<float*> ptrs;
    for (auto& ch : r.data)
        ptrs.push_back (ch.data());
    r.ok = reader->read (ptrs.data(), r.channels, 0, static_cast<int> (reader->lengthInSamples));
    return r;
}
}  // namespace rbt
