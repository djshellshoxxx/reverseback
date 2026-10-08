// The ReverseBack editor shared by the Standalone, VST3 and CLAP builds (GUI_DESIGN.md).
#pragma once

#include "ReverseBackProcessor.h"
#include "Sheets.h"
#include "WaveformView.h"
#include "Widgets.h"

namespace rb::ui
{
class AdvancedDrawer;

class ReverseBackEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer,
                          private juce::FocusChangeListener
{
public:
    explicit ReverseBackEditor (ReverseBackProcessor&);
    ~ReverseBackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    // ---- used by tests and the screenshot harness
    juce::Component* findControl (const juce::String& id);
    SheetHost& sheetHost() noexcept { return sheets_; }
    void refreshNow() { refresh(); }
    void openSettings (int tab = 0);
    void openExport();
    void setAdvancedOpen (bool open);
    bool advancedOpen() const noexcept { return advancedOpen_; }
    const StatusBar& statusBar() const noexcept { return status_; }

private:
    friend class AdvancedDrawer;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }
    void globalFocusChanged (juce::Component*) override;

    void refresh();
    void applyModeLayout (Mode m);
    void updateTransport (const Snapshot& s, Mode m);
    void updateWaveform (const Snapshot& s, Mode m);
    void updateStatus (const Snapshot& s, Mode m);
    void updateBanner();
    void applyPrefs();
    void updateParamCaptions (Mode m, const Snapshot& s);

    void primaryAction();
    void openFileDialog();
    void showMainMenu();
    void showPresetMenu();
    void showDeviceMenu();
    void toggleDirection();
    void releaseHold();
    void layoutParamRow (juce::Rectangle<int> row, Mode m);
    void layoutTransport (juce::Rectangle<int> row, Mode m);
    void layoutOptions (juce::Rectangle<int> row, Mode m);
    double secondsPerFrame() const;

    ReverseBackProcessor& proc_;
    RbLookAndFeel laf_;
    juce::TooltipWindow tooltip_ { this, 700 };
    juce::Image logo_;

    // header / tabs / source strip
    ActionButton presetButton_, menuButton_, settingsButton_;
    SegmentedControl modeTabs_;
    std::unique_ptr<SegmentedAttachment> modeAttach_;
    ActionButton sourceButton_, openButton_;
    juce::ComboBox inputChannels_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> inputChannelsAttach_;
    juce::Label inputLabel_;
    LevelMeter meter_;
    BannerBar banner_;
    std::uint32_t shownBannerId_ = ~0u;

    // waveform + parameter row
    WaveformView wave_;
    juce::Label recLabel_, waitLabel_, chunkLabel_, delayLabel_, startLabel_, endLabel_, lengthLabel_, computedLabel_;
    NumberField capture_, wait_, chunk_, delay_, selStart_, selEnd_;
    juce::ToggleButton repeat_;
    ActionButton selectAll_;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttach_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttach_;

    // transport + options
    ActionButton primary_, hold_, replay_, save_, extra_;
    juce::Label dirLabel_, loopLabel_, volumeLabel_;
    SegmentedControl direction_, loop_;
    std::unique_ptr<SegmentedAttachment> dirAttach_, loopAttach_;
    NumberField volume_;
    ActionButton advancedToggle_;
    StatusBar status_;

    std::unique_ptr<AdvancedDrawer> drawer_;
    SheetHost sheets_;
    std::unique_ptr<juce::FileChooser> chooser_;

    // state
    Mode lastMode_ = Mode::Record;
    bool modeLaidOut_ = false;
    bool advancedOpen_ = false;
    bool holdKeyDown_ = false, holdMouseDown_ = false, holdIsFinishEarly_ = false;
    int holdKeyCode_ = 'h';
    bool shortcutsEnabled_ = true;
    std::uint64_t waveVersion_ = 1;
    std::uint32_t lastTakeVersion_ = ~0u, lastFileVersion_ = ~0u, lastFrozenVersion_ = ~0u;
    int lastSlot_ = -2;
    std::int64_t lastChunk_ = -2;
    Selection preTrimSelection_ {};
    bool dropHover_ = false;
    juce::Rectangle<int> contentArea_;
    juce::String lastStatus_;
};

}  // namespace rb::ui
