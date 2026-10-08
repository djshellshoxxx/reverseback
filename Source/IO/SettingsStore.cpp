#include "SettingsStore.h"

#include "SafeParse.h"

#include <cmath>

namespace rb
{
namespace
{
juce::var presetToVar (const PresetValues& v)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("mode", static_cast<int> (v.mode));
    o->setProperty ("capture", v.captureSeconds);
    o->setProperty ("wait", v.waitSeconds);
    o->setProperty ("chunk", v.liveChunkSeconds);
    o->setProperty ("delay", v.liveDelaySeconds);
    o->setProperty ("speed", v.speed);
    o->setProperty ("loop", static_cast<int> (v.loop));
    o->setProperty ("direction", static_cast<int> (v.direction));
    o->setProperty ("repeat", v.repeatSession);
    return juce::var (o);
}

// juce::jlimit passes NaN straight through, and a NaN setting later becomes a garbage frame count.
double finiteOr (const juce::var& v, double fallback)
{
    const double d = static_cast<double> (v);
    return std::isfinite (d) ? d : fallback;
}

PresetValues presetFromVar (const juce::var& v)
{
    PresetValues p;
    p.mode = static_cast<Mode> (juce::jlimit (0, 2, static_cast<int> (v.getProperty ("mode", 0))));
    p.captureSeconds = juce::jlimit (0.25, kMaxCaptureSeconds, finiteOr (v.getProperty ("capture", 5.0), 5.0));
    p.waitSeconds = juce::jlimit (0.0, kMaxWaitSeconds, finiteOr (v.getProperty ("wait", 2.0), 2.0));
    p.liveChunkSeconds = juce::jlimit (kMinChunkSeconds, kMaxChunkSeconds, finiteOr (v.getProperty ("chunk", 0.5), 0.5));
    p.liveDelaySeconds = juce::jlimit (0.0, kMaxLiveDelaySeconds, finiteOr (v.getProperty ("delay", 2.0), 2.0));
    p.speed = juce::jlimit (kMinSpeed, kMaxSpeed, finiteOr (v.getProperty ("speed", 1.0), 1.0));
    p.loop = static_cast<LoopPattern> (juce::jlimit (0, 2, static_cast<int> (v.getProperty ("loop", 0))));
    p.direction = static_cast<int> (v.getProperty ("direction", 1)) == 0 ? Direction::Forward : Direction::Backward;
    p.repeatSession = static_cast<bool> (v.getProperty ("repeat", false));
    return p;
}
}  // namespace

SettingsStore::SettingsStore (juce::File file) : file_ (std::move (file)) {}

juce::File SettingsStore::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ReverseBack").getChildFile ("settings.json");
}

juce::String SettingsStore::toJson (const StoredSettings& s)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", s.version);
    root->setProperty ("lastFolder", s.lastFolder);
    root->setProperty ("deviceXml", s.deviceXml);
    root->setProperty ("lastState", s.lastState);

    auto* ui = new juce::DynamicObject();
    ui->setProperty ("shortcutsEnabled", s.ui.shortcutsEnabled);
    ui->setProperty ("reducedMotion", s.ui.reducedMotion);
    ui->setProperty ("holdKey", s.ui.holdKey);
    ui->setProperty ("tooltips", s.ui.tooltips);
    ui->setProperty ("advancedOpen", s.ui.advancedOpen);
    ui->setProperty ("advancedTab", s.ui.advancedTab);
    root->setProperty ("ui", juce::var (ui));

    juce::Array<juce::var> presets;
    for (const auto& p : s.userPresets)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", p.name);
        o->setProperty ("values", presetToVar (p.values));
        presets.add (juce::var (o));
    }
    root->setProperty ("userPresets", presets);
    return juce::JSON::toString (juce::var (root), false);
}

bool SettingsStore::fromJson (const juce::String& text, StoredSettings& out)
{
    juce::var root;
    if (! jsonStructureOk (text) || ! juce::JSON::parse (text, root).wasOk() || ! root.isObject())
        return false;
    const int version = root.getProperty ("version", 0);
    if (version < 1)
        return false;

    StoredSettings s;
    s.version = StoredSettings::kVersion;
    s.loadedVersion = version;   // newer files are read leniently (unknown fields ignored); load() keeps a backup
    s.lastFolder = root.getProperty ("lastFolder", "").toString();
    s.deviceXml = root.getProperty ("deviceXml", "").toString();
    s.lastState = root.getProperty ("lastState", "").toString();
    const auto ui = root.getProperty ("ui", juce::var());
    if (ui.isObject())
    {
        s.ui.shortcutsEnabled = ui.getProperty ("shortcutsEnabled", true);
        s.ui.reducedMotion = juce::jlimit (0, 2, static_cast<int> (ui.getProperty ("reducedMotion", 0)));
        s.ui.holdKey = ui.getProperty ("holdKey", "h").toString().substring (0, 1).toLowerCase();
        if (s.ui.holdKey.isEmpty())
            s.ui.holdKey = "h";
        s.ui.tooltips = ui.getProperty ("tooltips", true);
        s.ui.advancedOpen = ui.getProperty ("advancedOpen", false);
        s.ui.advancedTab = juce::jlimit (0, 3, static_cast<int> (ui.getProperty ("advancedTab", 0)));
    }
    if (const auto* arr = root.getProperty ("userPresets", juce::var()).getArray())
        for (const auto& item : *arr)
        {
            const juce::String name = item.getProperty ("name", "").toString().trim();
            if (name.isNotEmpty() && s.userPresets.size() < 64)
                s.userPresets.push_back ({ name.substring (0, 40), presetFromVar (item.getProperty ("values", juce::var())) });
        }
    out = std::move (s);
    return true;
}

bool SettingsStore::tryLoad (StoredSettings& out) const
{
    if (! file_.existsAsFile())
        return false;
    return fromJson (file_.loadFileAsString(), out);
}

StoredSettings SettingsStore::load() const
{
    StoredSettings s;
    if (! file_.existsAsFile())
        return s;
    const juce::String text = file_.loadFileAsString();
    if (fromJson (text, s))
    {
        if (s.loadedVersion > StoredSettings::kVersion)
        {
            // Written by a newer ReverseBack: we keep what we understand, but our next save drops what we do
            // not, so keep the original once.
            const juce::File backup = file_.withFileExtension ("v" + juce::String (s.loadedVersion) + ".bak");
            if (! backup.existsAsFile())
                file_.copyFileTo (backup);
        }
        return s;
    }
    // Keep the unreadable file for the user instead of overwriting it silently.
    file_.copyFileTo (file_.withFileExtension (".bad"));
    return StoredSettings {};
}

bool SettingsStore::save (const StoredSettings& settings) const
{
    if (! file_.getParentDirectory().createDirectory().wasOk())
        return false;
    return file_.replaceWithText (toJson (settings));   // JUCE writes a temporary file and swaps it in
}
}  // namespace rb
