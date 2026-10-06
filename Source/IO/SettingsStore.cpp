#include "SettingsStore.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace reverseback
{
namespace
{
bool parseBool(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes";
}

std::string escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        if (c == '\\' || c == '\n' || c == '=')
        {
            out += '\\';
            if (c == '\n')
                out += 'n';
            else
                out += c;
        }
        else
            out += c;
    }
    return out;
}

std::string unescape(const std::string& value)
{
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' && i + 1 < value.size())
        {
            const char n = value[++i];
            out += n == 'n' ? '\n' : n;
        }
        else
            out += value[i];
    }
    return out;
}

std::pair<std::string, std::string> splitLine(const std::string& line)
{
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '=')
            return {unescape(line.substr(0, i)), unescape(line.substr(i + 1))};
    }
    return {unescape(line), {}};
}
}

SettingsStore::SettingsStore(std::filesystem::path path)
    : path_(std::move(path))
{
}

AppSettings SettingsStore::load() const
{
    AppSettings settings;
    std::ifstream in(path_);
    if (!in)
        return settings;

    try
    {
        std::string line;
        while (std::getline(in, line))
        {
            const auto [key, value] = splitLine(line);
            if (!key.empty())
                applyValue(settings, key, value);
        }
    }
    catch (...)
    {
        return AppSettings{};
    }

    if (settings.version != AppSettings::currentVersion)
        return AppSettings{};

    // Monitoring is deliberately never restored across launches.
    settings.inputMonitor = false;
    return sanitize(settings);
}

void SettingsStore::save(const AppSettings& settings) const
{
    auto safe = sanitize(settings);
    safe.version = AppSettings::currentVersion;
    atomicWrite(path_, encodeSettings(safe, {}));
}

std::map<std::string, AppSettings> SettingsStore::loadPresets() const
{
    std::map<std::string, AppSettings> presets;
    std::ifstream in(presetsPath());
    if (!in)
        return presets;

    std::string line;
    while (std::getline(in, line))
    {
        const auto [fullKey, value] = splitLine(line);
        const auto separator = fullKey.find('.');
        if (separator == std::string::npos)
            continue;

        const auto name = fullKey.substr(0, separator);
        const auto key = fullKey.substr(separator + 1);
        applyValue(presets[name], key, value);
    }

    for (auto& [name, settings] : presets)
    {
        (void) name;
        settings = sanitize(settings);
        settings.inputMonitor = false;
    }
    return presets;
}

void SettingsStore::savePreset(const std::string& name, const AppSettings& settings) const
{
    if (name.empty() || name.find('\n') != std::string::npos || name.find('=') != std::string::npos)
        throw std::invalid_argument("Preset name is invalid");

    auto presets = loadPresets();
    auto safe = sanitize(settings);
    safe.inputMonitor = false;
    presets[name] = safe;

    std::ostringstream out;
    for (const auto& [presetName, preset] : presets)
        out << encodeSettings(preset, escape(presetName) + ".");

    atomicWrite(presetsPath(), out.str());
}

void SettingsStore::removePreset(const std::string& name) const
{
    auto presets = loadPresets();
    presets.erase(name);

    std::ostringstream out;
    for (const auto& [presetName, preset] : presets)
        out << encodeSettings(preset, escape(presetName) + ".");

    atomicWrite(presetsPath(), out.str());
}

std::filesystem::path SettingsStore::presetsPath() const
{
    return path_.parent_path() / (path_.stem().string() + "-presets.cfg");
}

AppSettings SettingsStore::sanitize(AppSettings s)
{
    s.captureSeconds = std::clamp(s.captureSeconds, 0.25, 60.0);
    s.waitSeconds = std::clamp(s.waitSeconds, 0.0, 30.0);
    s.liveChunkSeconds = std::clamp(s.liveChunkSeconds, 0.1, 5.0);
    s.liveDelaySeconds = std::clamp(s.liveDelaySeconds, 0.0, 30.0);
    s.inputGainDb = std::clamp(s.inputGainDb, -24.0, 24.0);
    s.outputVolumeDb = std::clamp(s.outputVolumeDb, -60.0, 0.0);
    s.speed = std::clamp(s.speed, 0.5, 2.0);
    s.edgeFadeMs = std::clamp(s.edgeFadeMs, 0.0, 10.0);
    return s;
}

std::string SettingsStore::encodeSettings(const AppSettings& s, const std::string& prefix)
{
    std::ostringstream out;
    out << prefix << "version=" << AppSettings::currentVersion << '\n'
        << prefix << "captureSeconds=" << s.captureSeconds << '\n'
        << prefix << "waitSeconds=" << s.waitSeconds << '\n'
        << prefix << "liveChunkSeconds=" << s.liveChunkSeconds << '\n'
        << prefix << "liveDelaySeconds=" << s.liveDelaySeconds << '\n'
        << prefix << "inputGainDb=" << s.inputGainDb << '\n'
        << prefix << "outputVolumeDb=" << s.outputVolumeDb << '\n'
        << prefix << "speed=" << s.speed << '\n'
        << prefix << "edgeFadeMs=" << s.edgeFadeMs << '\n'
        << prefix << "inputMonitor=" << (s.inputMonitor ? 1 : 0) << '\n'
        << prefix << "shortcutsEnabled=" << (s.shortcutsEnabled ? 1 : 0) << '\n'
        << prefix << "lastFolder=" << escape(s.lastFolder) << '\n'
        << prefix << "inputDevice=" << escape(s.inputDevice) << '\n'
        << prefix << "outputDevice=" << escape(s.outputDevice) << '\n';
    return out.str();
}

void SettingsStore::applyValue(AppSettings& s, const std::string& key, const std::string& value)
{
    try
    {
        if (key == "version") s.version = std::stoi(value);
        else if (key == "captureSeconds") s.captureSeconds = std::stod(value);
        else if (key == "waitSeconds") s.waitSeconds = std::stod(value);
        else if (key == "liveChunkSeconds") s.liveChunkSeconds = std::stod(value);
        else if (key == "liveDelaySeconds") s.liveDelaySeconds = std::stod(value);
        else if (key == "inputGainDb") s.inputGainDb = std::stod(value);
        else if (key == "outputVolumeDb") s.outputVolumeDb = std::stod(value);
        else if (key == "speed") s.speed = std::stod(value);
        else if (key == "edgeFadeMs") s.edgeFadeMs = std::stod(value);
        else if (key == "inputMonitor") s.inputMonitor = parseBool(value);
        else if (key == "shortcutsEnabled") s.shortcutsEnabled = parseBool(value);
        else if (key == "lastFolder") s.lastFolder = value;
        else if (key == "inputDevice") s.inputDevice = value;
        else if (key == "outputDevice") s.outputDevice = value;
    }
    catch (...)
    {
        // Ignore individual corrupt entries and retain defaults/current value.
    }
}

void SettingsStore::atomicWrite(const std::filesystem::path& path, const std::string& content)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());

    const auto temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Unable to open temporary settings file");
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out)
            throw std::runtime_error("Unable to write temporary settings file");
    }

    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
        if (ec)
        {
            std::filesystem::remove(temp);
            throw std::runtime_error("Unable to atomically replace settings file");
        }
    }
}
}
