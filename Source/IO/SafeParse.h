// Cheap structural checks before untrusted text (state blobs, settings.json) reaches a recursive-descent
// parser: a file with 100 000 nested brackets must be refused, not crash the host with a stack overflow.
#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace rb
{
constexpr int kMaxParseDepth = 64;
constexpr int kMaxParseChars = 2 * 1024 * 1024;

inline bool jsonStructureOk (const juce::String& text)
{
    if (text.length() > kMaxParseChars)
        return false;
    int depth = 0;
    bool inString = false, escaped = false;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const juce::juce_wchar c = *p;
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > kMaxParseDepth)
                return false;
        }
        else if ((c == '}' || c == ']') && depth > 0)
            --depth;
    }
    return true;
}

inline bool xmlStructureOk (const juce::String& text)
{
    if (text.length() > kMaxParseChars)
        return false;
    int depth = 0;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        if (*p != '<')
            continue;
        auto next = p;
        ++next;
        const juce::juce_wchar n = *next;
        if (n == '/')
        {
            if (depth > 0)
                --depth;
        }
        else if (n != '!' && n != '?' && n != 0)
        {
            if (++depth > kMaxParseDepth)
                return false;
            auto q = next;   // an element that closes itself ("<a/>") does not nest
            juce::juce_wchar prev = 0;
            while (! q.isEmpty() && *q != '>')
                prev = *q++;
            if (prev == '/' && depth > 0)
                --depth;
        }
    }
    return true;
}

inline std::unique_ptr<juce::XmlElement> parseXmlSafely (const juce::String& text)
{
    if (! xmlStructureOk (text))
        return nullptr;
    return juce::parseXML (text);
}

// Same container format as AudioProcessor::copyXmlToBinary, with the structural check in front of the parser.
inline std::unique_ptr<juce::XmlElement> xmlFromStateBlob (const void* data, int sizeInBytes)
{
    constexpr juce::uint32 magic = 0x21324356;
    if (data != nullptr && sizeInBytes > 8 && juce::ByteOrder::littleEndianInt (data) == magic)
    {
        const int len = static_cast<int> (juce::ByteOrder::littleEndianInt (static_cast<const char*> (data) + 4));
        if (len > 0)
            return parseXmlSafely (juce::String::fromUTF8 (static_cast<const char*> (data) + 8, juce::jmin (sizeInBytes - 8, len)));
    }
    return nullptr;
}
}  // namespace rb
