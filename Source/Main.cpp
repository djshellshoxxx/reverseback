#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace reverseback
{
class ReverseBackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ReverseBack"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff101621),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(820, 620, 1800, 1200);
            setContentOwned(new MainComponent(), true);
            centreWithSize(960, 700);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow_;
};
}

START_JUCE_APPLICATION(reverseback::ReverseBackApplication)
