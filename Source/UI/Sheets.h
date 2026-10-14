// Modal overlay sheets drawn inside the editor (so plugin windows never open extra OS windows):
// Settings, Export, text prompt and confirmation. GUI_DESIGN.md section 9.
#pragma once

#include "ReverseBackProcessor.h"
#include "Widgets.h"

#include <memory>

namespace rb::ui
{
class Sheet : public juce::Component
{
public:
    ~Sheet() override = default;
    virtual juce::String sheetTitle() const = 0;
    virtual juce::Point<int> preferredSize() const = 0;
    virtual bool dismissOnScrim() const { return true; }
    std::function<void()> requestClose;   // set by the host
};

class SheetHost : public juce::Component
{
public:
    SheetHost();
    void show (std::unique_ptr<Sheet> sheet);
    void close();
    bool isOpen() const noexcept { return sheet_ != nullptr; }
    Sheet* current() const noexcept { return sheet_.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    std::unique_ptr<Sheet> sheet_;
    ActionButton closeButton_;
};

// -------------------------------------------------------------------------------------------------
class SettingsSheet : public Sheet
{
public:
    explicit SettingsSheet (ReverseBackProcessor& p, std::function<void()> onPrefsChanged, int initialTab = 0);
    juce::String sheetTitle() const override { return "Settings"; }
    juce::Point<int> preferredSize() const override { return { 600, host_ != nullptr ? 560 : 430 }; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showTab (int i);
    ReverseBackProcessor& proc_;
    HostServices* host_;
    std::function<void()> onChanged_;
    SegmentedControl tabs_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
    juce::ToggleButton shortcuts_, tooltips_;
    SegmentedControl motion_;
    juce::TextEditor holdKey_;
    juce::Label audioInfo_, motionLabel_, holdLabel_, aboutText_;
    int tab_ = 0;
};

// -------------------------------------------------------------------------------------------------
class ExportSheet : public Sheet
{
public:
    ExportSheet (ReverseBackProcessor& p, ExportSource src);
    ~ExportSheet() override;
    juce::String sheetTitle() const override { return "Save WAV"; }
    juce::Point<int> preferredSize() const override { return { 600, 520 }; }
    bool dismissOnScrim() const override { return ! running_; }
    void resized() override;
    void paint (juce::Graphics&) override;

    // Lets tests drive the decision flow without a native file chooser.
    void simulateOutcomeForTest (ExportOutcome o) { finished (std::move (o)); }

private:
    ExportSettings settings() const;
    void refresh();
    void chooseFile();
    void begin (bool overwrite);
    void finished (ExportOutcome outcome);
    void decision (const juce::String& message, std::vector<std::pair<juce::String, std::function<void()>>> options);

    ReverseBackProcessor& proc_;
    ExportSource src_;
    juce::File dest_;
    SegmentedControl format_, rate_;
    juce::ToggleButton dither_, normalise_, exact_;
    std::unique_ptr<juce::ButtonParameterAttachment> exactAttach_;
    juce::Label summary_, status_;
    ActionButton cancel_, save_;
    std::vector<std::unique_ptr<ActionButton>> decisionButtons_;
    std::unique_ptr<juce::FileChooser> chooser_;
    double progress_ = 0.0;
    juce::ProgressBar bar_ { progress_ };
    bool running_ = false;
};

// -------------------------------------------------------------------------------------------------
class PromptSheet : public Sheet
{
public:
    PromptSheet (juce::String title, juce::String label, juce::String initial, juce::String confirmText,
                 std::function<void (const juce::String&)> onResult);
    juce::String sheetTitle() const override { return title_; }
    juce::Point<int> preferredSize() const override { return { 480, 220 }; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title_, label_;
    juce::TextEditor editor_;
    ActionButton cancel_, ok_;
    std::function<void (const juce::String&)> onResult_;
};

class ConfirmSheet : public Sheet
{
public:
    ConfirmSheet (juce::String title, juce::String message, juce::String confirmText, std::function<void()> onConfirm);
    juce::String sheetTitle() const override { return title_; }
    juce::Point<int> preferredSize() const override { return { 480, 220 }; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title_, message_;
    ActionButton cancel_, ok_;
    std::function<void()> onConfirm_;
};
}  // namespace rb::ui
