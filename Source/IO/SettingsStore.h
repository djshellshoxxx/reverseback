#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace reverseback
{
struct AppSettings
{
    static constexpr int currentVersion = 1;

    int version{currentVersion};
    double captureSeconds{5.0};
    double waitSeconds{2.0};
    double liveChunkSeconds{0.5};
    double liveDelaySeconds{2.0};
    double inputGainDb{0.0};
    double outputVolumeDb{-12.0};
    double speed{1.0};
    double edgeFadeMs{3.0};
    int mode{0};
    int direction{1};
    int loopPattern{0};
    double countdownSeconds{0.0};
    bool autoStart{false};
    double triggerThresholdDb{-45.0};
    bool repeatSession{false};
    double repeatGapSeconds{0.5};
    bool exactSamples{false};
    int exportBitDepth{32};
    bool normalizeExport{false};
    bool inputMonitor{false};
    bool shortcutsEnabled{true};
    std::string lastFolder;
    std::string inputDevice;
    std::string outputDevice;
};

class SettingsStore
{
public:
    explicit SettingsStore(std::filesystem::path path);

    [[nodiscard]] AppSettings load() const;
    void save(const AppSettings& settings) const;

    [[nodiscard]] std::map<std::string, AppSettings> loadPresets() const;
    void savePreset(const std::string& name, const AppSettings& settings) const;
    void removePreset(const std::string& name) const;

private:
    [[nodiscard]] std::filesystem::path presetsPath() const;
    static AppSettings sanitize(AppSettings settings);
    static std::string encodeSettings(const AppSettings& settings, const std::string& prefix);
    static void applyValue(AppSettings& settings, const std::string& key, const std::string& value);
    static void atomicWrite(const std::filesystem::path& path, const std::string& content);

    std::filesystem::path path_;
};
}
