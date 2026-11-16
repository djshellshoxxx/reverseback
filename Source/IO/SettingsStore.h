// Versioned, atomically written user settings (ENGINE_DESIGN section 13). Never stores audio.
#pragma once

#include "Presets.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace rb
{
struct UiPrefs
{
    bool shortcutsEnabled = true;
    int reducedMotion = 0;          // 0 follow the system, 1 on, 2 off
    juce::String holdKey = "h";
    bool tooltips = true;
    bool advancedOpen = false;
    int advancedTab = 0;
};

struct UserPreset
{
    juce::String name;
    PresetValues values;
};

struct StoredSettings
{
    static constexpr int kVersion = 1;
    int version = kVersion;
    int loadedVersion = kVersion;   // version found in the file (not written back)
    juce::String lastFolder;        // last folder used for files and exports
    juce::String deviceXml;         // standalone audio device state
    juce::String lastState;         // standalone: base64 of the last parameter state (never audio)
    UiPrefs ui;
    std::vector<UserPreset> userPresets;
};

class SettingsStore
{
public:
    explicit SettingsStore (juce::File file = defaultFile());

    static juce::File defaultFile();

    // Never throws. Corrupt, unreadable or unknown-version files fall back to defaults; a corrupt file
    // is kept as "<name>.bad" so nothing is silently destroyed.
    StoredSettings load() const;
    // Reads the file without side effects (no backup copies); false if missing or unreadable.
    bool tryLoad (StoredSettings& out) const;
    bool save (const StoredSettings& settings) const;

    const juce::File& file() const noexcept { return file_; }

    // Exposed for tests.
    static juce::String toJson (const StoredSettings& s);
    static bool fromJson (const juce::String& text, StoredSettings& out);

private:
    juce::File file_;
};
}  // namespace rb
