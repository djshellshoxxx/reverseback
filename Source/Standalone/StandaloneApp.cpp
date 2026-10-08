// Custom standalone application: one resizable window hosting the shared editor, our own device
// manager and processor player (PLUGIN_FORMATS.md section 8).
#include "ReverseBackProcessor.h"

#include <iostream>

using namespace rb;

namespace
{
juce::String usage()
{
    return "ReverseBack " RB_VERSION_STRING "\n"
           "Usage: ReverseBack [--help] [--version] [--selftest]\n"
           "  --selftest   run an offline engine check (no audio device needed) and exit 0 on success\n";
}

// Offline end-to-end check: a 1 s capture + 0.5 s wait must play back the exact reversed take.
bool runSelfTest (juce::String& report)
{
    const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("rb-selftest", ".json");
    {
        ReverseBackProcessor p (tmp);
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        auto setP = [&] (const char* id, float plain)
        {
            auto* prm = p.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };
        setP (ids::capture, 1.0f);
        setP (ids::wait, 0.5f);
        setP (ids::exact, 1.0f);
        setP (ids::outVol, 0.0f);
        setP (ids::edgeFade, 0.0f);

        const std::size_t latency = 48;   // the standalone output limiter's look-ahead at 48 kHz
        const juce::int64 total = 48000 * 3;
        std::vector<float> input (static_cast<std::size_t> (total)), output (static_cast<std::size_t> (total));
        for (juce::int64 i = 0; i < total; ++i)
            input[static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (std::sin (0.01 * static_cast<double> (i)));

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (juce::int64 pos = 0; pos < total; pos += 512)
        {
            if (pos == 0)
            {
                p.actionStart();
            }
            const int n = static_cast<int> (std::min<juce::int64> (512, total - pos));
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, input[static_cast<std::size_t> (pos + i)]);
                buf.setSample (1, i, input[static_cast<std::size_t> (pos + i)]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
                output[static_cast<std::size_t> (pos + i)] = buf.getSample (0, i);
            p.serviceNow();
        }
        p.releaseResources();

        double worst = 0.0;
        for (juce::int64 n = 0; n < 48000; ++n)
        {
            const double expected = static_cast<double> (input[static_cast<std::size_t> (48000 - 1 - n)]);
            const double got = static_cast<double> (output[static_cast<std::size_t> (72000 + n) + latency]);
            worst = std::max (worst, std::abs (expected - got));
        }
        report = "record -> wait -> reversed playback: max sample error " + juce::String (worst, 9);
        tmp.deleteFile();
        return worst < 1.0e-6;
    }
}

class ReverseBackApplication final : public juce::JUCEApplication, private juce::ChangeListener, private HostServices
{
public:
    const juce::String getApplicationName() override { return "ReverseBack"; }
    const juce::String getApplicationVersion() override { return RB_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--version"))
        {
            std::cout << "ReverseBack " << RB_VERSION_STRING << std::endl;
            setApplicationReturnValue (0);
            quit();
            return;
        }
        if (commandLine.contains ("--help") || commandLine.contains ("-h"))
        {
            std::cout << usage() << std::endl;
            setApplicationReturnValue (0);
            quit();
            return;
        }
        if (commandLine.contains ("--selftest"))
        {
            juce::String report;
            const bool ok = runSelfTest (report);
            std::cout << "ReverseBack " << RB_VERSION_STRING << " selftest: " << (ok ? "PASS" : "FAIL") << " - " << report << std::endl;
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        cleanupAbandonedCaches (juce::File::getSpecialLocation (juce::File::tempDirectory));
        processor = std::make_unique<ReverseBackProcessor>();
        processor->setHostServices (this);

        // Restore the last parameter values (never audio, never the monitor, never an auto-loaded file).
        if (processor->storedSettings().lastState.isNotEmpty())
            if (auto xml = juce::parseXML (processor->storedSettings().lastState))
                processor->setStateFromXml (*xml, false);

        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [] (bool) {});
        openDevices();
        deviceManager.addChangeListener (this);
        player.setProcessor (processor.get());
        deviceManager.addAudioCallback (&player);

        window = std::make_unique<MainWindow> (*this, *processor);
    }

    void shutdown() override
    {
        if (processor != nullptr)
        {
            auto& st = processor->storedSettings();
            if (auto xml = deviceManager.createStateXml())
                st.deviceXml = xml->toString();
            if (auto state = processor->getStateAsXml())
            {
                state->removeChildElement (state->getChildByName ("File"), true);   // no file history without consent
                st.lastState = state->toString();
            }
            processor->saveStoredSettings();
        }
        window.reset();
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        deviceManager.closeAudioDevice();
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (ReverseBackApplication& a, ReverseBackProcessor& p)
            : juce::DocumentWindow ("ReverseBack", juce::Colour (0xff0e131c), juce::DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
            auto* editor = p.createEditor();
            setContentOwned (editor, true);
            setResizable (true, false);
            setResizeLimits (820, 620, 4096, 4096);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        ReverseBackApplication& app;
    };

    // ---- HostServices
    juce::AudioDeviceManager* getDeviceManager() override { return &deviceManager; }

    juce::String getDeviceSummary() override
    {
        if (auto* d = deviceManager.getCurrentAudioDevice())
            return deviceManager.getCurrentAudioDeviceType() + " - " + d->getName() + " - "
                   + juce::String (juce::roundToInt (d->getCurrentSampleRate())) + " Hz - " + juce::String (d->getCurrentBufferSizeSamples()) + " frames";
        return "No audio device";
    }

    juce::String getDeviceError() override
    {
        if (deviceManager.getCurrentAudioDevice() == nullptr)
            return lastDeviceError.isNotEmpty() ? lastDeviceError : "No audio device is open. Choose one in Settings.";
        return {};
    }

    bool hasInputDevice() override
    {
        auto* d = deviceManager.getCurrentAudioDevice();
        return d != nullptr && ! d->getActiveInputChannels().isZero();
    }

    void retryDevices() override
    {
        deviceManager.closeAudioDevice();
        openDevices();
        if (processor != nullptr)
            processor->sendChangeMessage();
    }

    void openDevices()
    {
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;
        preferred.bufferSize = 256;
        std::unique_ptr<juce::XmlElement> saved;
        if (processor != nullptr && processor->storedSettings().deviceXml.isNotEmpty())
            saved = juce::parseXML (processor->storedSettings().deviceXml);
        // Prefer the OS default devices; fall back to whatever works.
        lastDeviceError = deviceManager.initialise (2, 2, saved.get(), true, {}, &preferred);
        if (lastDeviceError.isEmpty() && deviceManager.getCurrentAudioDevice() == nullptr)
            lastDeviceError = "No audio device could be opened.";
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (processor != nullptr)
            processor->sendChangeMessage();
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<ReverseBackProcessor> processor;
    std::unique_ptr<MainWindow> window;
    juce::String lastDeviceError;
};
}  // namespace

START_JUCE_APPLICATION (ReverseBackApplication)
