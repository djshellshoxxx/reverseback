#include "Sheets.h"

namespace rb::ui
{
// =============================================================================== SheetHost
SheetHost::SheetHost() : closeButton_ ("Close sheet")
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    closeButton_.setStyle (ActionButton::Style::Icon);
    closeButton_.setIcon (Icon::Close);
    closeButton_.setTooltip ("Close (Esc)");
    closeButton_.onClick = [this] { close(); };
    addChildComponent (closeButton_);
    setVisible (false);
}

void SheetHost::show (std::unique_ptr<Sheet> sheet)
{
    close();
    sheet_ = std::move (sheet);
    sheet_->requestClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SheetHost> (this)] { if (safe != nullptr) safe->close(); }); };
    addAndMakeVisible (*sheet_);
    closeButton_.setVisible (true);
    closeButton_.toFront (false);
    setVisible (true);
    toFront (true);
    resized();
    grabKeyboardFocus();
    repaint();
}

void SheetHost::close()
{
    if (sheet_ == nullptr)
        return;
    removeChildComponent (sheet_.get());
    sheet_.reset();
    closeButton_.setVisible (false);
    setVisible (false);
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

juce::Rectangle<int> SheetHost::cardBounds() const
{
    if (sheet_ == nullptr)
        return {};
    const auto size = sheet_->preferredSize();
    const int w = std::min (size.x, getWidth() - 32), h = std::min (size.y, getHeight() - 32);
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

void SheetHost::resized()
{
    if (sheet_ == nullptr)
        return;
    const auto card = cardBounds();
    closeButton_.setBounds (card.getRight() - 52, card.getY() + 12, 40, 40);
    sheet_->setBounds (card.withTrimmedTop (64).reduced (20, 0).withTrimmedBottom (16));
}

void SheetHost::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.58f));
    if (sheet_ == nullptr)
        return;
    const auto card = cardBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (card.translated (0.0f, 6.0f).expanded (2.0f), metric::cardRadius + 2.0f);
    g.setColour (col::bg1);
    g.fillRoundedRectangle (card, metric::cardRadius);
    g.setColour (col::line);
    g.drawRoundedRectangle (card.reduced (0.5f), metric::cardRadius, 1.0f);
    g.setColour (col::text);
    g.setFont (fonts::semibold (22.0f));
    g.drawText (sheet_->sheetTitle(), card.withTrimmedLeft (24.0f).withHeight (64.0f), juce::Justification::centredLeft, false);
}

void SheetHost::mouseDown (const juce::MouseEvent& e)
{
    if (sheet_ != nullptr && sheet_->dismissOnScrim() && ! cardBounds().contains (e.getPosition()))
        close();
}

bool SheetHost::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && sheet_ != nullptr && sheet_->dismissOnScrim())
    {
        close();
        return true;
    }
    return sheet_ != nullptr;   // a sheet is modal: swallow everything else
}

// =============================================================================== SettingsSheet
namespace
{
juce::Label& styleLabel (juce::Label& l, const juce::String& text, float size = 15.0f, juce::Colour c = col::text2)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (fonts::regular (size));
    l.setColour (juce::Label::textColourId, c);
    l.setMinimumHorizontalScale (1.0f);
    l.setJustificationType (juce::Justification::topLeft);
    return l;
}
}  // namespace

SettingsSheet::SettingsSheet (ReverseBackProcessor& p, std::function<void()> onPrefsChanged, int initialTab)
    : proc_ (p), host_ (p.hostServices()), onChanged_ (std::move (onPrefsChanged))
{
    tabs_.setItems ({ { "Audio", {}, true, Icon::Speaker }, { "Interface", {}, true, Icon::Gear }, { "About", {}, true, Icon::Info } });
    tabs_.onChange = [this] (int i) { showTab (i); };
    addAndMakeVisible (tabs_);

    // Audio
    if (host_ != nullptr && host_->getDeviceManager() != nullptr)
    {
        selector_ = std::make_unique<juce::AudioDeviceSelectorComponent> (*host_->getDeviceManager(), 0, 2, 0, 2, false, false, true, false);
        addChildComponent (*selector_);
    }
    addChildComponent (audioInfo_);

    // Interface
    auto& ui = proc_.storedSettings().ui;
    shortcuts_.setButtonText ("Keyboard shortcuts (Space, R, F, Esc, H, Ctrl+O, Ctrl+S)");
    shortcuts_.setToggleState (ui.shortcutsEnabled, juce::dontSendNotification);
    shortcuts_.onClick = [this] { proc_.storedSettings().ui.shortcutsEnabled = shortcuts_.getToggleState(); proc_.saveStoredSettings(); onChanged_(); };
    tooltips_.setButtonText ("Show tooltips");
    tooltips_.setToggleState (ui.tooltips, juce::dontSendNotification);
    tooltips_.onClick = [this] { proc_.storedSettings().ui.tooltips = tooltips_.getToggleState(); proc_.saveStoredSettings(); onChanged_(); };
    motion_.setItems ({ { "Follow system" }, { "Reduced motion" }, { "Full motion" } });
    motion_.setSelected (ui.reducedMotion, false);
    motion_.onChange = [this] (int i) { proc_.storedSettings().ui.reducedMotion = i; proc_.saveStoredSettings(); onChanged_(); };
    holdKey_.setInputRestrictions (1, "abcdefghijklmnopqrstuvwxyz0123456789");
    holdKey_.setText (ui.holdKey, false);
    holdKey_.setFont (fonts::medium (18.0f));
    holdKey_.setJustification (juce::Justification::centred);
    holdKey_.onTextChange = [this]
    {
        const auto t = holdKey_.getText().toLowerCase();
        if (t.isNotEmpty())
        {
            proc_.storedSettings().ui.holdKey = t;
            proc_.saveStoredSettings();
            onChanged_();
        }
    };
    styleLabel (motionLabel_, "Animation");
    styleLabel (holdLabel_, "Hold to Record key");
    for (juce::Component* c : std::initializer_list<juce::Component*> { &shortcuts_, &tooltips_, &motion_, &holdKey_, &motionLabel_, &holdLabel_ })
        addChildComponent (*c);

    // About
    styleLabel (aboutText_,
                "ReverseBack " RB_VERSION_STRING "\nA playful local audio toy for reversing your voice, live audio and files.\n\n"
                "Copyright (c) 2026 Sheldon Davidson. Circuit Drift Labs and ReverseBack are unregistered trademarks.\n\n"
                "Source code: MIT licence. These binaries include JUCE 8 (AGPLv3), clap-juce-extensions (MIT) and the Inter font (SIL OFL 1.1), "
                "so the combined binaries are distributed under AGPL-3.0-or-later terms with the corresponding source.\n\n"
                "Everything runs locally. No audio is uploaded, no account is needed and nothing is recorded until you press a record button.",
                15.0f);
    addChildComponent (aboutText_);

    for (auto* t : std::initializer_list<juce::ToggleButton*> { &shortcuts_, &tooltips_ })
        t->setColour (juce::Slider::trackColourId, accentFor (p.mode()).fill);
    motion_.setAccent (accentFor (p.mode()).fill);
    tabs_.setAccent (accentFor (p.mode()).fill);
    tabs_.setSelected (initialTab, false);
    showTab (initialTab);
}

void SettingsSheet::showTab (int i)
{
    tab_ = i;
    const bool audio = i == 0, iface = i == 1, about = i == 2;
    if (selector_)
        selector_->setVisible (audio);
    audioInfo_.setVisible (audio);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &shortcuts_, &tooltips_, &motion_, &holdKey_, &motionLabel_, &holdLabel_ })
        c->setVisible (iface);
    aboutText_.setVisible (about);
    if (audio)
    {
        if (host_ != nullptr)
            styleLabel (audioInfo_, "Active: " + host_->getDeviceSummary() + (proc_.getLatencySamples() > 0 ? "   |   Output limiter adds "
                                      + juce::String (proc_.getLatencySamples() * 1000.0 / std::max (1.0, proc_.currentSampleRate()), 1) + " ms" : juce::String()), 14.0f);
        else
            styleLabel (audioInfo_, "Audio devices, sample rate and buffer size are managed by your host application.\n\n"
                                    "ReverseBack reports zero latency; the Wait and Live delay controls are creative delays and are not compensated by the host.",
                        15.0f);
    }
    resized();
    repaint();
}

void SettingsSheet::resized()
{
    auto r = getLocalBounds();
    tabs_.setBounds (r.removeFromTop (44));
    r.removeFromTop (14);
    if (tab_ == 0)
    {
        if (selector_)
        {
            audioInfo_.setBounds (r.removeFromTop (24));
            r.removeFromTop (8);
            selector_->setBounds (r);
        }
        else
        {
            audioInfo_.setBounds (r);
        }
    }
    else if (tab_ == 1)
    {
        shortcuts_.setBounds (r.removeFromTop (36));
        r.removeFromTop (4);
        tooltips_.setBounds (r.removeFromTop (36));
        r.removeFromTop (14);
        motionLabel_.setBounds (r.removeFromTop (22));
        motion_.setBounds (r.removeFromTop (44));
        r.removeFromTop (18);
        holdLabel_.setBounds (r.removeFromTop (22).removeFromLeft (200));
        holdKey_.setBounds (r.removeFromTop (44).removeFromLeft (64));
    }
    else
    {
        aboutText_.setBounds (r);
    }
}

void SettingsSheet::paint (juce::Graphics&) {}

// =============================================================================== ExportSheet
ExportSheet::ExportSheet (ReverseBackProcessor& p, ExportSource src) : proc_ (p), src_ (std::move (src)), cancel_ ("Cancel"), save_ ("Save")
{
    const auto accent = accentFor (p.mode());
    format_.setItems ({ { "32-bit float" }, { "24-bit PCM" } });
    format_.setAccent (accent.fill);
    format_.onChange = [this] (int) { refresh(); };
    rate_.setItems ({ { "Source rate" }, { "44.1 kHz" }, { "48 kHz" } });
    rate_.setAccent (accent.fill);
    rate_.onChange = [this] (int) { refresh(); };
    dither_.setButtonText ("Dither (24-bit)");
    normalise_.setButtonText ("Peak normalise to -1 dBFS");
    exact_.setButtonText ("Exact Samples (no edge fades)");
    for (auto* t : std::initializer_list<juce::ToggleButton*> { &dither_, &normalise_, &exact_ })
    {
        t->setColour (juce::Slider::trackColourId, accent.fill);
        t->onClick = [this] { refresh(); };
    }
    exactAttach_ = std::make_unique<juce::ButtonParameterAttachment> (*proc_.apvts.getParameter (ids::exact), exact_);
    exact_.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ExportSheet> (this)] { if (safe != nullptr) safe->refresh(); }); };

    summary_.setFont (fonts::regular (15.0f));
    summary_.setColour (juce::Label::textColourId, col::text);
    summary_.setJustificationType (juce::Justification::topLeft);
    status_.setFont (fonts::regular (15.0f));
    status_.setJustificationType (juce::Justification::topLeft);
    status_.setColour (juce::Label::textColourId, col::text2);

    cancel_.setStyle (ActionButton::Style::Secondary);
    cancel_.setLabel ("Cancel");
    save_.setStyle (ActionButton::Style::Primary);
    save_.setAccent (accent);
    save_.setIcon (Icon::Save);
    save_.setLabel ("Save...");
    cancel_.onClick = [this]
    {
        if (running_)
            proc_.exporter().cancel(), running_ = false, bar_.setVisible (false), status_.setText ("Export cancelled.", juce::dontSendNotification), resized();
        else if (requestClose)
            requestClose();
    };
    save_.onClick = [this] { chooseFile(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &format_, &rate_, &dither_, &normalise_, &exact_, &summary_, &status_, &cancel_, &save_, &bar_ })
        addAndMakeVisible (*c);
    bar_.setVisible (false);
    refresh();
}

ExportSheet::~ExportSheet()
{
    proc_.exporter().cancel();
}

ExportSettings ExportSheet::settings() const
{
    ExportSettings s;
    s.format = format_.getSelected() == 0 ? ExportSettings::Format::Float32 : ExportSettings::Format::Pcm24;
    s.sampleRate = rate_.getSelected() == 1 ? 44100 : (rate_.getSelected() == 2 ? 48000 : 0);
    s.dither = dither_.getToggleState() && s.format == ExportSettings::Format::Pcm24;
    s.normalize = normalise_.getToggleState();
    return s;
}

void ExportSheet::refresh()
{
    dither_.setEnabled (format_.getSelected() == 1 && ! running_);
    if (format_.getSelected() == 0)
        dither_.setToggleState (false, juce::dontSendNotification);
    const auto req = proc_.makeExportRequest (src_, settings(), juce::File(), false);
    summary_.setText (exportSummary (req, src_.sampleRate, src_.channels), juce::dontSendNotification);
    save_.setEnabled (! running_ && src_.valid());
    format_.setEnabled (! running_);
    rate_.setEnabled (! running_);
    repaint();
}

void ExportSheet::resized()
{
    auto r = getLocalBounds();
    summary_.setBounds (r.removeFromTop (92));
    r.removeFromTop (6);
    format_.setBounds (r.removeFromTop (44));
    r.removeFromTop (10);
    rate_.setBounds (r.removeFromTop (44));
    r.removeFromTop (10);
    dither_.setBounds (r.removeFromTop (34));
    normalise_.setBounds (r.removeFromTop (34));
    exact_.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (52);
    save_.setBounds (bottom.removeFromRight (160));
    bottom.removeFromRight (10);
    cancel_.setBounds (bottom.removeFromRight (120));
    auto statusArea = r;
    if (running_)
        bar_.setBounds (statusArea.removeFromBottom (22));
    int by = statusArea.getBottom() - 44;
    for (auto it = decisionButtons_.rbegin(); it != decisionButtons_.rend(); ++it)
    {
        (*it)->setBounds (statusArea.getX(), by, std::min (statusArea.getWidth(), 360), 40);
        by -= 46;
    }
    status_.setBounds (statusArea.withHeight (std::max (24, by - statusArea.getY() + 40)));
}

void ExportSheet::paint (juce::Graphics&) {}

void ExportSheet::chooseFile()
{
    if (! src_.valid() || running_)
        return;
    decisionButtons_.clear();
    const juce::String lastFolder = proc_.lastFolder();
    juce::File start = lastFolder.isNotEmpty() ? juce::File (lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser_ = std::make_unique<juce::FileChooser> ("Save WAV", start.getChildFile (src_.suggestedName + ".wav"), "*.wav");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<ExportSheet> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr)
                                   return;
                               const auto f = fc.getResult();
                               if (f == juce::File())
                                   return;
                               safe->dest_ = f.hasFileExtension ("wav") ? f : f.withFileExtension ("wav");
                               safe->begin (false);
                           });
}

void ExportSheet::decision (const juce::String& message, std::vector<std::pair<juce::String, std::function<void()>>> options)
{
    status_.setText (message, juce::dontSendNotification);
    status_.setColour (juce::Label::textColourId, col::warn);
    decisionButtons_.clear();
    for (auto& [label, fn] : options)
    {
        auto b = std::make_unique<ActionButton> (label);
        b->setStyle (ActionButton::Style::Secondary);
        b->setLabel (label);
        b->onClick = [safe = juce::Component::SafePointer<ExportSheet> (this), f = fn]
        {
            if (safe == nullptr)
                return;
            safe->decisionButtons_.clear();
            safe->status_.setText ({}, juce::dontSendNotification);
            f();
            safe->resized();
        };
        addAndMakeVisible (*b);
        decisionButtons_.push_back (std::move (b));
    }
    resized();
}

void ExportSheet::begin (bool overwrite)
{
    running_ = true;
    progress_ = 0.0;
    bar_.setVisible (true);
    status_.setText ("Saving " + dest_.getFileName() + "...", juce::dontSendNotification);
    status_.setColour (juce::Label::textColourId, col::text2);
    cancel_.setLabel ("Cancel export");
    refresh();
    resized();
    auto req = proc_.makeExportRequest (src_, settings(), dest_, overwrite);
    proc_.exporter().run (std::move (req),
                          [safe = juce::Component::SafePointer<ExportSheet> (this)] (float p)
                          {
                              if (safe != nullptr)
                                  safe->progress_ = static_cast<double> (p);
                          },
                          [safe = juce::Component::SafePointer<ExportSheet> (this)] (ExportOutcome o)
                          {
                              if (safe != nullptr)
                                  safe->finished (std::move (o));
                          });
}

void ExportSheet::finished (ExportOutcome o)
{
    running_ = false;
    bar_.setVisible (false);
    cancel_.setLabel ("Close");
    refresh();
    switch (o.error)
    {
        case ExportError::None:
            proc_.showSavedBanner (o.written);
            if (requestClose)
                requestClose();
            return;
        case ExportError::Cancelled:
            status_.setText ("Export cancelled.", juce::dontSendNotification);
            break;
        case ExportError::WouldClip:
            decision (o.message, { { "Export as 32-bit float", [this] { format_.setSelected (0, false); refresh(); begin (true); } },
                                   { "Normalise to -1 dBFS", [this] { normalise_.setToggleState (true, juce::dontSendNotification); refresh(); begin (true); } } });
            return;
        case ExportError::DestinationExists:
            decision (o.message + " Replace it?", { { "Replace", [this] { begin (true); } } });
            return;
        case ExportError::Invalid:
        case ExportError::ReadFailed:
        case ExportError::WriteFailed:
            status_.setText (o.message, juce::dontSendNotification);
            status_.setColour (juce::Label::textColourId, col::rec);
            break;
    }
    resized();
}

// =============================================================================== PromptSheet / ConfirmSheet
PromptSheet::PromptSheet (juce::String title, juce::String label, juce::String initial, juce::String confirmText,
                          std::function<void (const juce::String&)> onResult)
    : title_ (std::move (title)), label_ (std::move (label)), cancel_ ("Cancel"), ok_ (confirmText), onResult_ (std::move (onResult))
{
    editor_.setText (initial, false);
    editor_.setFont (fonts::regular (18.0f));
    editor_.setSelectAllWhenFocused (true);
    editor_.setInputRestrictions (40);
    addAndMakeVisible (editor_);
    cancel_.setStyle (ActionButton::Style::Secondary);
    cancel_.setLabel ("Cancel");
    ok_.setStyle (ActionButton::Style::Primary);
    ok_.setLabel (confirmText);
    cancel_.onClick = [this] { if (requestClose) requestClose(); };
    auto accept = [this]
    {
        const auto t = editor_.getText().trim();
        if (t.isEmpty())
            return;
        auto cb = onResult_;
        if (requestClose)
            requestClose();
        if (cb)
            cb (t);
    };
    ok_.onClick = accept;
    editor_.onReturnKey = accept;
    editor_.onEscapeKey = [this] { if (requestClose) requestClose(); };
    addAndMakeVisible (cancel_);
    addAndMakeVisible (ok_);
}

void PromptSheet::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (28);
    editor_.setBounds (r.removeFromTop (44));
    auto b = r.removeFromBottom (48);
    ok_.setBounds (b.removeFromRight (150));
    b.removeFromRight (10);
    cancel_.setBounds (b.removeFromRight (110));
}

void PromptSheet::paint (juce::Graphics& g)
{
    g.setColour (col::text2);
    g.setFont (fonts::regular (15.0f));
    g.drawText (label_, 0, 0, getWidth(), 24, juce::Justification::centredLeft, false);
}

ConfirmSheet::ConfirmSheet (juce::String title, juce::String message, juce::String confirmText, std::function<void()> onConfirm)
    : title_ (std::move (title)), message_ (std::move (message)), cancel_ ("Cancel"), ok_ (confirmText), onConfirm_ (std::move (onConfirm))
{
    cancel_.setStyle (ActionButton::Style::Secondary);
    cancel_.setLabel ("Cancel");
    ok_.setStyle (ActionButton::Style::Danger);
    ok_.setLabel (confirmText);
    cancel_.onClick = [this] { if (requestClose) requestClose(); };
    ok_.onClick = [this]
    {
        auto cb = onConfirm_;
        if (requestClose)
            requestClose();
        if (cb)
            cb();
    };
    addAndMakeVisible (cancel_);
    addAndMakeVisible (ok_);
}

void ConfirmSheet::resized()
{
    auto b = getLocalBounds().removeFromBottom (48);
    ok_.setBounds (b.removeFromRight (150));
    b.removeFromRight (10);
    cancel_.setBounds (b.removeFromRight (110));
}

void ConfirmSheet::paint (juce::Graphics& g)
{
    g.setColour (col::text);
    g.setFont (fonts::regular (16.0f));
    g.drawFittedText (message_, getLocalBounds().withTrimmedBottom (60), juce::Justification::topLeft, 4);
}
}  // namespace rb::ui
