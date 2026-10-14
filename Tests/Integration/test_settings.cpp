#include "SettingsStore.h"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace reverseback;

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "reverseback-settings-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "settings.cfg";

    SettingsStore store(path);
    auto defaults = store.load();
    assert(defaults.captureSeconds == 5.0);
    assert(!defaults.inputMonitor);

    AppSettings changed;
    changed.captureSeconds = 12.5;
    changed.outputVolumeDb = -6.0;
    changed.inputMonitor = true;
    changed.lastFolder = "C:\\Audio=Tests";
    store.save(changed);

    auto loaded = store.load();
    assert(loaded.captureSeconds == 12.5);
    assert(loaded.outputVolumeDb == -6.0);
    assert(!loaded.inputMonitor);
    assert(loaded.lastFolder == "C:\\Audio=Tests");

    store.savePreset("Long Phrase", changed);
    const auto presets = store.loadPresets();
    assert(presets.at("Long Phrase").captureSeconds == 12.5);
    assert(!presets.at("Long Phrase").inputMonitor);

    {
        std::ofstream bad(path, std::ios::trunc);
        bad << "version=999\ncaptureSeconds=not-a-number\n";
    }
    auto recovered = store.load();
    assert(recovered.version == AppSettings::currentVersion);
    assert(recovered.captureSeconds == 5.0);

    std::filesystem::remove_all(root);
    return 0;
}
