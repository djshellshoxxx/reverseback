// Custom widgets: buttons, segmented control, number field, level meter, banner, status bar.
#pragma once

#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace rb::ui
{
class ActionButton : public juce::Button, private juce::Timer
{
public:
    enum class Style { Primary, Secondary, Danger, Ghost, Tab, Icon };

    explicit ActionButton (const juce::String& name = {});
    void setStyle (Style s) { style_ = s; repaint(); }
    void setIcon (Icon i, bool trailing = false) { icon_ = i; iconTrailing_ = trailing; repaint(); }
    void setLabel (const juce::String& s);
    void setAccent (Accent a) { accent_ = a; repaint(); }
    void setSelected (bool s) { if (selected_ != s) { selected_ = s; repaint(); } }
    bool isSelected() const noexcept { return selected_; }

    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool keyPressed (const juce::KeyPress&) override;
    void buttonStateChanged() override;

    // Called with true on pointer-down and false on release/cancel (used by Hold to Record).
    std::function<void (bool)> onPressedChanged;

private:
    void timerCallback() override;
    Style style_ = Style::Secondary;
    Icon icon_ = Icon::None;
    Accent accent_ = accentFor (Mode::Record);
    bool selected_ = false, wasDown_ = false, iconTrailing_ = false;
    float hover_ = 0.0f;
};

class SegmentedControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Item
    {
        juce::String label;
        juce::String tip;
        bool enabled = true;
        Icon icon = Icon::None;
    };

    void setItems (std::vector<Item> items);
    void setItemEnabled (int index, bool enabled, const juce::String& tip = {});
    void setSelected (int index, bool notify = false);
    int getSelected() const noexcept { return selected_; }
    void setAccent (juce::Colour c) { accent_ = c; repaint(); }
    void setFontSize (float s) { fontSize_ = s; repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int indexAt (juce::Point<int>) const;
    juce::Rectangle<float> segment (int i) const;
    std::vector<Item> items_;
    int selected_ = 0;
    juce::Colour accent_ = accentFor (Mode::Record).fill;
    float fontSize_ = 16.0f;
};

class NumberField : public juce::Slider
{
public:
    enum class Kind { Seconds, Millis, Ms, Db, Dbfs, Speed, Percent, Plain };

    explicit NumberField (const juce::String& name);
    // Installs the display/parse functions (call after any parameter attachment, which overrides them).
    void configure (Kind kind, int decimals = 2);
    void setAccent (juce::Colour c) { setColour (juce::Slider::trackColourId, c); repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override { if (getParentComponent() != nullptr && ! lafRefreshed_) { lafRefreshed_ = true; sendLookAndFeelChange(); } }
    // True while the user is typing into the value box.
    bool isEditingText() const
    {
        for (auto* c : getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (c))
                return l->isBeingEdited();
        return false;
    }

private:
    Kind kind_ = Kind::Plain;
    int decimals_ = 2;
    bool lafRefreshed_ = false;
};

class LevelMeter : public juce::Component
{
public:
    // peak: linear 0..>1 as published by the engine; overloadCount: increments when a sample exceeded full scale.
    void update (float peak, std::uint32_t overloadCount);
    void paint (juce::Graphics&) override;
    bool overActive() const noexcept { return overUntil_ > juce::Time::getMillisecondCounterHiRes(); }
    juce::String summary() const;

private:
    float levelDb_ = -90.0f, holdDb_ = -90.0f;
    double holdUntil_ = 0.0, overUntil_ = 0.0;
    std::uint32_t lastOverload_ = 0;
    bool init_ = false;
};

class BannerBar : public juce::Component
{
public:
    BannerBar();
    void setContent (const juce::String& text, bool error, bool retry, bool settings, bool reveal);
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onRetry, onSettings, onDismiss, onReveal;

private:
    juce::String text_;
    bool error_ = true;
    ActionButton retry_, settings_, reveal_, close_;
};

class StatusBar : public juce::Component
{
public:
    void set (const juce::String& state, juce::Colour dot, const juce::String& hint, const juce::String& right);
    void paint (juce::Graphics&) override;
    const juce::String& stateText() const noexcept { return state_; }
    const juce::String& hintText() const noexcept { return hint_; }

private:
    juce::String state_, hint_, right_;
    juce::Colour dot_ = col::ok;
};

// Keeps a SegmentedControl in step with a choice parameter.
class SegmentedAttachment
{
public:
    SegmentedAttachment (juce::RangedAudioParameter& p, SegmentedControl& c);

private:
    SegmentedControl& control_;
    juce::ParameterAttachment attachment_;
    bool updating_ = false;
};
}  // namespace rb::ui
