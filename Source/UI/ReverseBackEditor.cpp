#include "ReverseBackEditor.h"

#include <BinaryData.h>

namespace rb
{
juce::AudioProcessorEditor* createReverseBackEditor (ReverseBackProcessor& p) { return new ui::ReverseBackEditor (p); }
}  // namespace rb

namespace rb::ui
{
namespace
{
using LabelPtr = std::unique_ptr<juce::Label>;

void styleCaption (juce::Label& l, const juce::String& text, float size = 15.0f, juce::Colour c = col::text2)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (fonts::regular (size));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setInterceptsMouseClicks (false, false);
    l.setMinimumHorizontalScale (0.8f);
    l.setBorderSize ({ 0, 0, 0, 0 });
}

int textWidth (const juce::Label& l) { return static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText()))) + 4; }

juce::String dirWord (bool backward) { return backward ? "backwards" : "forwards"; }
}  // namespace

// =================================================================================================
// Advanced drawer
// =================================================================================================
class AdvancedDrawer : public juce::Component
{
public:
    struct Row
    {
        std::unique_ptr<juce::Label> label;
        juce::Component* control = nullptr;
        int height = 40;
    };

    struct Page : public juce::Component
    {
        std::vector<Row> rows;
        void add (const juce::String& caption, juce::Component& c, int h = 40)
        {
            Row r;
            r.label = std::make_unique<juce::Label>();
            styleCaption (*r.label, caption, 14.0f);
            r.control = &c;
            r.height = h;
            addAndMakeVisible (*r.label);
            addAndMakeVisible (c);
            rows.push_back (std::move (r));
        }
        void addPlain (juce::Component& c, int h = 40)
        {
            Row r;
            r.control = &c;
            r.height = h;
            addAndMakeVisible (c);
            rows.push_back (std::move (r));
        }
        int contentHeight() const
        {
            int h = 8;
            for (const auto& r : rows)
                h += r.height + 6;
            return h;
        }
        void resized() override
        {
            int y = 4;
            for (auto& r : rows)
            {
                auto row = juce::Rectangle<int> (0, y, getWidth(), r.height);
                if (r.label != nullptr)
                {
                    r.label->setBounds (row.removeFromLeft (116));
                    r.control->setBounds (row);
                }
                else
                {
                    r.control->setBounds (row);
                }
                y += r.height + 6;
            }
        }
    };

    struct SpeedPresets : public juce::Component, public juce::SettableTooltipClient
    {
        SpeedPresets()
        {
            static constexpr double values[] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
            for (int i = 0; i < 5; ++i)
            {
                auto b = std::make_unique<ActionButton> ("speed" + juce::String (i));
                b->setComponentID ("adv.speed." + juce::String (i));
                b->setStyle (ActionButton::Style::Secondary);
                b->setLabel (juce::String (values[i], values[i] == 0.75 ? 2 : (values[i] == 1.0 || values[i] == 2.0 ? 0 : 1)) + "x");
                b->onClick = [this, v = values[i]] { if (onPick) onPick (v); };
                addAndMakeVisible (*b);
                buttons.push_back (std::move (b));
            }
        }
        void resized() override
        {
            const int w = (getWidth() - 4 * 4) / 5;
            for (std::size_t i = 0; i < buttons.size(); ++i)
                buttons[i]->setBounds (static_cast<int> (i) * (w + 4), 0, w, getHeight());
        }
        void setCurrent (double v)
        {
            static constexpr double values[] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
            for (std::size_t i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (std::abs (values[i] - v) < 0.005, juce::dontSendNotification);
        }
        std::function<void (double)> onPick;
        std::vector<std::unique_ptr<ActionButton>> buttons;
    };

    explicit AdvancedDrawer (ReverseBackEditor& e) : ed_ (e), proc_ (e.proc_)
    {
        auto& apvts = proc_.apvts;
        setComponentID ("drawer");
        tabs_.setComponentID ("drawer.tabs");
        addAndMakeVisible (tabs_);
        addAndMakeVisible (viewport_);
        viewport_.setScrollBarsShown (true, false);
        viewport_.setScrollBarThickness (8);

        // ---- Recording page
        countdown_.setItems ({ { "Off" }, { "1 s" }, { "3 s" }, { "5 s" } });
        countdown_.setComponentID ("adv.countdown");
        countdownAttach_ = std::make_unique<SegmentedAttachment> (*apvts.getParameter (ids::countdown), countdown_);
        autoStart_.setButtonText ("Auto start on voice");
        autoStart_.setComponentID ("adv.autoStart");
        autoStart_.setTooltip ("Arm the microphone and start recording when sound is detected. Background noise and speech cannot always be told apart.");
        buttonAttach_.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (ids::autoStart), autoStart_));
        threshold_.setComponentID ("adv.threshold");
        attach (threshold_, ids::threshold, NumberField::Kind::Dbfs, 1);
        repeatGap_.setComponentID ("adv.repeatGap");
        attach (repeatGap_, ids::tailGap, NumberField::Kind::Seconds, 2);
        recPage_.add ("Countdown", countdown_);
        recPage_.add ("Voice trigger", autoStart_);
        recPage_.add ("Threshold", threshold_);
        recPage_.add ("Repeat gap", repeatGap_);

        // ---- Playback page
        speedPresets_.onPick = [this] (double v)
        {
            if (auto* p = proc_.apvts.getParameter (ids::speed))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (v)));
                p->endChangeGesture();
            }
        };
        speedPresets_.setComponentID ("adv.speedPresets");
        speed_.setComponentID ("adv.speed");
        attach (speed_, ids::speed, NumberField::Kind::Speed, 2);
        edgeFade_.setComponentID ("adv.edgeFade");
        attach (edgeFade_, ids::edgeFade, NumberField::Kind::Ms, 1);
        edgeFade_.setTooltip ("Short fades at clip boundaries to avoid clicks.");
        liveFade_.setComponentID ("adv.liveFade");
        attach (liveFade_, ids::liveFade, NumberField::Kind::Ms, 1);
        liveFade_.setTooltip ("Smooth edges on every live chunk. Not perfectly seamless speech.");
        exact_.setButtonText ("Exact Samples (no fades)");
        exact_.setComponentID ("adv.exact");
        exact_.setTooltip ("Exact sample order with no edge fades. Exports are bit-exact reversals, but playback may click at the boundaries.");
        buttonAttach_.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (ids::exact), exact_));
        playPage_.add ("Speed", speedPresets_);
        playPage_.add ("Fine speed", speed_);
        playPage_.add ("Edge fade", edgeFade_);
        playPage_.add ("Live edge fade", liveFade_);
        playPage_.add ("Fades", exact_);

        // ---- Audio page
        inGain_.setComponentID ("adv.inGain");
        attach (inGain_, ids::inGain, NumberField::Kind::Db, 1);
        chan_.setComponentID ("adv.inChan");
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ids::inChan)))
            chan_.addItemList (choice->choices, 1);
        chanAttach_ = std::make_unique<juce::ComboBoxParameterAttachment> (*apvts.getParameter (ids::inChan), chan_);
        monitor_.setComponentID ("adv.monitor");
        attach (monitor_, ids::monitor, NumberField::Kind::Percent, 0);
        monitorNote_.setComponentID ("adv.monitorNote");
        styleCaption (monitorNote_, proc_.isStandalone() ? "Monitoring can cause feedback: use headphones." : "Use your host's input monitoring.", 13.0f, col::text3);
        monitorNote_.setJustificationType (juce::Justification::topLeft);
        audioPage_.add ("Input gain", inGain_);
        audioPage_.add ("Input channels", chan_);
        audioPage_.add ("Input monitor", monitor_);
        audioPage_.addPlain (monitorNote_, 36);

        // ---- Take page
        trim_.setComponentID ("adv.trim");
        trim_.setStyle (ActionButton::Style::Secondary);
        trim_.setIcon (Icon::Trim);
        trim_.setLabel ("Trim silence");
        trim_.setTooltip ("Select only the part with sound (50 ms padding). The audio itself is never changed.");
        undo_.setComponentID ("adv.undoTrim");
        undo_.setStyle (ActionButton::Style::Secondary);
        undo_.setIcon (Icon::Undo);
        undo_.setLabel ("Undo trim");
        surprise_.setComponentID ("adv.surprise");
        surprise_.setStyle (ActionButton::Style::Secondary);
        surprise_.setIcon (Icon::Shuffle);
        surprise_.setLabel ("Surprise settings");
        surprise_.setTooltip ("Pick a playful speed, direction and loop pattern. Never touches devices, gain or recording.");
        takeInfo_.setComponentID ("adv.takeInfo");
        styleCaption (takeInfo_, {}, 13.0f, col::text3);
        takeInfo_.setJustificationType (juce::Justification::topLeft);
        takePage_.addPlain (trim_);
        takePage_.addPlain (undo_);
        takePage_.addPlain (surprise_);
        takePage_.addPlain (takeInfo_, 44);

        trim_.onClick = [this] { doTrim(); };
        undo_.onClick = [this] { doUndo(); };
        surprise_.onClick = [this] { proc_.applySurprise(); };

        tabs_.onChange = [this] (int i)
        {
            showPage (i);
            auto& ui = proc_.storedSettings().ui;
            ui.advancedTab = tabPages_.empty() ? 0 : tabPages_[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (tabPages_.size()) - 1, i))];
            proc_.saveStoredSettings();
        };
    }

    void setMode (Mode m)
    {
        mode_ = m;
        const auto accent = accentFor (m);
        tabs_.setAccent (accent.fill);
        for (auto* c : std::initializer_list<juce::ToggleButton*> { &autoStart_, &exact_ })
            c->setColour (juce::Slider::trackColourId, accent.fill);
        for (auto* n : std::initializer_list<NumberField*> { &threshold_, &repeatGap_, &speed_, &edgeFade_, &liveFade_, &inGain_, &monitor_ })
            n->setAccent (accent.fill);
        countdown_.setAccent (accent.fill);

        std::vector<SegmentedControl::Item> items;
        tabPages_.clear();
        auto addTab = [&] (const char* name, int page) { items.push_back ({ name }); tabPages_.push_back (page); };
        if (m == Mode::Record) { addTab ("Recording", 0); addTab ("Playback", 1); addTab ("Audio", 2); addTab ("Take", 3); }
        else if (m == Mode::Live) { addTab ("Playback", 1); addTab ("Audio", 2); }
        else { addTab ("Playback", 1); addTab ("Take", 3); }
        tabs_.setItems (std::move (items));
        tabs_.setFontSize (14.0f);
        int want = proc_.storedSettings().ui.advancedTab;
        int idx = 0;
        for (std::size_t i = 0; i < tabPages_.size(); ++i)
            if (tabPages_[i] == want)
                idx = static_cast<int> (i);
        tabs_.setSelected (idx, false);
        showPage (idx);
    }

    void showPage (int tab)
    {
        if (tabPages_.empty())
            return;
        Page* pages[] = { &recPage_, &playPage_, &audioPage_, &takePage_ };
        Page* p = pages[tabPages_[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (tabPages_.size()) - 1, tab))]];
        viewport_.setViewedComponent (p, false);
        p->sendLookAndFeelChange();
        resized();
    }

    void refreshState (const Snapshot& s, Mode m)
    {
        const bool busy = proc_.isBusy();
        const bool live = m == Mode::Live;
        speedPresets_.setEnabled (! live);
        speed_.setEnabled (! live);
        speedPresets_.setTooltip (live ? "Live is fixed at 1x." : "Tape-style speed: pitch follows speed.");
        speed_.setTooltip (live ? "Live is fixed at 1x." : "Speed 0.5x to 2x. Pitch changes with speed.");
        edgeFade_.setEnabled (! live);
        liveFade_.setEnabled (live);
        speedPresets_.setCurrent (proc_.apvts.getRawParameterValue (ids::speed)->load());

        const bool standalone = proc_.isStandalone();
        monitor_.setEnabled (standalone);
        monitor_.setTooltip (standalone ? "Hear the microphone directly. Off by default; can cause feedback."
                                        : "Direct monitoring is only available in the standalone app. Use your host's input monitoring.");
        const bool hostMono = proc_.getTotalNumInputChannels() < 2;
        chan_.setTooltip (hostMono && ! standalone ? "A mono input only has Input 1." : "Which input channel(s) to record.");

        const auto take = proc_.currentTake();
        const auto file = proc_.fileAsset();
        bool canTrim = false, canUndo = false;
        if (m == Mode::Record)
        {
            canTrim = take && ! busy;
            canUndo = take && ! take->trim.empty() && ! busy;
            trim_.setTooltip (take ? "Select only the part with sound. The audio itself is never changed." : "Record something first.");
            styleCaption (takeInfo_, take ? juce::String (take->clip->frameCount() / take->clip->sampleRate(), 2) + " s, " + juce::String (juce::roundToInt (take->clip->sampleRate())) + " Hz, "
                                                + (take->clip->channels() == 1 ? "mono" : "stereo") + (take->trim.empty() ? "" : ", trimmed") : juce::String ("No take yet."), 13.0f, col::text3);
        }
        else if (m == Mode::File)
        {
            canTrim = file && file->ram && ! s.filePlaying;
            canUndo = ! ed_.preTrimSelection_.empty() && file && ! s.filePlaying;
            trim_.setTooltip (! file ? "Open a file first." : (! file->ram ? "Trim silence is not available for very large files." : "Select only the part with sound."));
            styleCaption (takeInfo_, file ? file->name + "\n" + fmtClock (file->durationSeconds()) + ", " + juce::String (juce::roundToInt (file->sampleRate)) + " Hz, "
                                                + (file->channels == 1 ? "mono" : "stereo") : juce::String ("No file loaded."), 13.0f, col::text3);
        }
        trim_.setEnabled (canTrim);
        undo_.setEnabled (canUndo);
        surprise_.setEnabled (! busy && m != Mode::Live);
        surprise_.setTooltip (m == Mode::Live ? "Surprise settings apply to Record and File modes." : "Pick a playful speed, direction and loop pattern.");
        repeatGap_.setEnabled (m == Mode::Record);
        const bool recording = m == Mode::Record && s.recordState != 0;
        countdown_.setEnabled (! recording);
        autoStart_.setEnabled (! recording);
        threshold_.setEnabled (! recording && autoStart_.getToggleState());
        threshold_.setTooltip (autoStart_.getToggleState() ? "Sound level that starts the recording." : "Turn on Auto start on voice first.");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        tabs_.setBounds (r.removeFromTop (36));
        r.removeFromTop (8);
        viewport_.setBounds (r);
        if (auto* p = viewport_.getViewedComponent())
            p->setSize (std::max (100, r.getWidth() - (viewport_.isVerticalScrollBarShown() ? 10 : 0)), static_cast<Page*> (p)->contentHeight());
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::bg1);
        g.fillRoundedRectangle (r, metric::cardRadius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r, metric::cardRadius, 1.0f);
    }

private:
    void attach (NumberField& f, const char* id, NumberField::Kind kind, int decimals)
    {
        sliderAttach_.push_back (std::make_unique<juce::SliderParameterAttachment> (*proc_.apvts.getParameter (id), f));
        f.configure (kind, decimals);
    }

    void doTrim()
    {
        if (mode_ == Mode::Record)
        {
            if (! proc_.trimSilence())
                proc_.showBanner (BannerKind::Info, "No speech or sound detected.", false, false, 3.0);
        }
        else if (mode_ == Mode::File)
        {
            if (auto file = proc_.fileAsset(); file && file->ram)
            {
                const Selection sel = findNonSilentSelection (*file->ram);
                if (sel.empty())
                    proc_.showBanner (BannerKind::Info, "No speech or sound detected.", false, false, 3.0);
                else
                {
                    ed_.preTrimSelection_ = proc_.fileSelection();
                    proc_.setFileSelection (sel);
                }
            }
        }
    }

    void doUndo()
    {
        if (mode_ == Mode::Record)
            proc_.undoTrim();
        else if (mode_ == Mode::File && ! ed_.preTrimSelection_.empty())
        {
            proc_.setFileSelection (ed_.preTrimSelection_);
            ed_.preTrimSelection_ = {};
        }
    }

    ReverseBackEditor& ed_;
    ReverseBackProcessor& proc_;
    Mode mode_ = Mode::Record;
    SegmentedControl tabs_;
    juce::Viewport viewport_;
    std::vector<int> tabPages_;
    Page recPage_, playPage_, audioPage_, takePage_;

    SegmentedControl countdown_;
    std::unique_ptr<SegmentedAttachment> countdownAttach_;
    juce::ToggleButton autoStart_, exact_;
    NumberField threshold_ { "Voice threshold" }, repeatGap_ { "Repeat gap" }, speed_ { "Speed" }, edgeFade_ { "Edge fade" },
        liveFade_ { "Live edge fade" }, inGain_ { "Input gain" }, monitor_ { "Input monitor" };
    SpeedPresets speedPresets_;
    juce::ComboBox chan_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> chanAttach_;
    juce::Label monitorNote_, takeInfo_;
    ActionButton trim_ { "Trim" }, undo_ { "Undo" }, surprise_ { "Surprise" };
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttach_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttach_;
};

// =================================================================================================
// Editor
// =================================================================================================
ReverseBackEditor::ReverseBackEditor (ReverseBackProcessor& p)
    : juce::AudioProcessorEditor (p), proc_ (p),
      presetButton_ ("Presets"), menuButton_ ("Menu"), settingsButton_ ("Settings"), sourceButton_ ("Source"), openButton_ ("Open"),
      capture_ ("Record for"), wait_ ("Wait before playback"), chunk_ ("Reverse chunk"), delay_ ("Extra delay"),
      selStart_ ("Selection start"), selEnd_ ("Selection end"), selectAll_ ("Select all"),
      primary_ ("Primary"), hold_ ("Hold"), replay_ ("Replay"), save_ ("Save"), extra_ ("Extra"), volume_ ("Output volume"), advancedToggle_ ("Advanced")
{
    setLookAndFeel (&laf_);
    tooltip_.setLookAndFeel (&laf_);
    logo_ = juce::ImageCache::getFromMemory (RBAssets::icon256_png, RBAssets::icon256_pngSize);
    setWantsKeyboardFocus (true);
    setTitle ("ReverseBack");
    setDescription ("Reverse your voice, live audio and files.");

    auto& apvts = p.apvts;
    auto addAll = [this] (std::initializer_list<juce::Component*> list) { for (auto* c : list) addAndMakeVisible (*c); };

    // ---- header
    presetButton_.setComponentID ("presetButton");
    presetButton_.setStyle (ActionButton::Style::Secondary);
    presetButton_.setLabel ("Presets");
    presetButton_.setIcon (Icon::Chevron, true);
    presetButton_.setTooltip ("Quick settings for voice, tiny syllables and long phrases. Presets never start recording.");
    presetButton_.onClick = [this] { showPresetMenu(); };
    menuButton_.setComponentID ("menuButton");
    menuButton_.setStyle (ActionButton::Style::Icon);
    menuButton_.setIcon (Icon::Menu);
    menuButton_.setTooltip ("Menu: every action and its keyboard shortcut");
    menuButton_.onClick = [this] { showMainMenu(); };
    settingsButton_.setComponentID ("settingsButton");
    settingsButton_.setStyle (ActionButton::Style::Icon);
    settingsButton_.setIcon (Icon::Gear);
    settingsButton_.setTooltip ("Settings");
    settingsButton_.onClick = [this] { openSettings(); };

    // ---- mode tabs
    modeTabs_.setComponentID ("modeTabs");
    modeTabs_.setItems ({ { "Record & Reverse", {}, true, Icon::Mic }, { "Live Reverse", {}, true, Icon::Headphones }, { "Reverse File", {}, true, Icon::Folder } });
    modeTabs_.setFontSize (18.0f);
    modeAttach_ = std::make_unique<SegmentedAttachment> (*apvts.getParameter (ids::mode), modeTabs_);
    modeTabs_.setTooltip ("Switching mode stops any playback");

    // ---- source strip
    sourceButton_.setComponentID ("sourceButton");
    sourceButton_.setStyle (ActionButton::Style::Secondary);
    sourceButton_.setIcon (Icon::Mic);
    sourceButton_.onClick = [this] { showDeviceMenu(); };
    openButton_.setComponentID ("openButton");
    openButton_.setStyle (ActionButton::Style::Secondary);
    openButton_.setIcon (Icon::Folder);
    openButton_.setLabel ("Open...");
    openButton_.setTooltip ("Open a WAV, AIFF or FLAC file (Ctrl+O). You can also drop a file here.");
    openButton_.onClick = [this]
    {
        if (proc_.isLoadingFile())
            proc_.cancelLoad();
        else
            openFileDialog();
    };
    inputChannels_.setComponentID ("inputChannels");
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ids::inChan)))
        inputChannels_.addItemList (choice->choices, 1);
    inputChannelsAttach_ = std::make_unique<juce::ComboBoxParameterAttachment> (*apvts.getParameter (ids::inChan), inputChannels_);
    inputChannels_.setTooltip ("Which input channel(s) to record");
    styleCaption (inputLabel_, "Input", 14.0f);
    inputLabel_.setJustificationType (juce::Justification::centredRight);
    meter_.setComponentID ("inputMeter");
    meter_.setTitle ("Input level");
    banner_.setComponentID ("banner");
    banner_.onDismiss = [this] { proc_.dismissBanner(); };
    banner_.onRetry = [this] { proc_.retryBanner(); };
    banner_.onSettings = [this] { openSettings(); };
    banner_.onReveal = [this] { proc_.banner().reveal.revealToUser(); };
    banner_.setVisible (false);

    // ---- waveform
    wave_.setComponentID ("waveform");
    wave_.onSelectionChanged = [this] (double b, double e)
    {
        if (auto f = proc_.fileAsset())
        {
            const double frames = static_cast<double> (f->frames);
            const Selection sel { static_cast<Frame> (std::llround (b * frames)), static_cast<Frame> (std::llround (e * frames)) };
            proc_.setFileSelection (sel);
        }
    };
    wave_.onSelectAll = [this]
    {
        if (auto f = proc_.fileAsset())
            proc_.setFileSelection ({ 0, f->frames });
    };

    // ---- parameter row
    capture_.setComponentID ("capture");
    wait_.setComponentID ("wait");
    chunk_.setComponentID ("chunk");
    delay_.setComponentID ("delay");
    auto attach = [this, &apvts] (NumberField& f, const char* id, NumberField::Kind k, int dec)
    {
        sliderAttach_.push_back (std::make_unique<juce::SliderParameterAttachment> (*apvts.getParameter (id), f));
        f.configure (k, dec);
    };
    attach (capture_, ids::capture, NumberField::Kind::Seconds, 2);
    attach (wait_, ids::wait, NumberField::Kind::Seconds, 2);
    attach (chunk_, ids::chunk, NumberField::Kind::Millis, 0);
    attach (delay_, ids::delay, NumberField::Kind::Seconds, 2);
    attach (volume_, ids::outVol, NumberField::Kind::Db, 1);
    volume_.setComponentID ("volume");
    dirLabel_.setComponentID ("directionLabel");
    capture_.setArrowStep (0.05);
    wait_.setArrowStep (0.05);
    delay_.setArrowStep (0.05);
    chunk_.setArrowStep (0.01);
    capture_.setTooltip ("How long to record (0.25 to 60 s). Applies to the next recording.");
    wait_.setTooltip ("Silence before playback starts (0 to 30 s).");
    chunk_.setTooltip ("Live Reverse reverses each chunk of this length (0.1 to 5 s).");
    delay_.setTooltip ("Extra delay added after each chunk completes (0 to 30 s).");
    volume_.setTooltip ("Playback volume (does not change exported files).");
    styleCaption (recLabel_, "Record for");
    styleCaption (waitLabel_, "Wait before playback");
    styleCaption (chunkLabel_, "Reverse chunk");
    styleCaption (delayLabel_, "Extra delay");
    styleCaption (startLabel_, "Start");
    styleCaption (endLabel_, "End");
    styleCaption (lengthLabel_, "Length");
    styleCaption (computedLabel_, {}, 15.0f, col::text);
    computedLabel_.setComponentID ("firstSound");
    computedLabel_.setTooltip ("Startup latency is chunk + delay, plus device buffering. Inside each chunk the delay of a sound varies: the start of a chunk plays last-captured audio first.");
    repeat_.setButtonText ("Repeat session");
    repeat_.setComponentID ("repeat");
    buttonAttach_.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (ids::repeat), repeat_));

    for (auto* f : { &selStart_, &selEnd_ })
    {
        f->setRange (0.0, 1.0, 0.001);
        f->textFromValueFunction = [] (double v) { return fmtClock (v, true); };
        f->valueFromTextFunction = [f] (const juce::String& t) { return parseSeconds (t, f->getValue()); };
        f->setNumDecimalPlacesToDisplay (3);
    }
    selStart_.setComponentID ("selStart");
    selEnd_.setComponentID ("selEnd");
    auto applyFields = [this]
    {
        if (auto f = proc_.fileAsset())
        {
            const double rate = f->sampleRate;
            const Selection sel { static_cast<Frame> (std::llround (selStart_.getValue() * rate)), static_cast<Frame> (std::llround (selEnd_.getValue() * rate)) };
            if (! proc_.setFileSelection (sel))
                proc_.showBanner (BannerKind::Info, "The selection must be at least 50 ms long and inside the file.", false, false, 3.0);
            refresh();
        }
    };
    selStart_.onValueChange = [this, applyFields] { if (selStart_.isMouseButtonDown() || selStart_.hasKeyboardFocus (true) || selStart_.isEditingText()) applyFields(); };
    selEnd_.onValueChange = [this, applyFields] { if (selEnd_.isMouseButtonDown() || selEnd_.hasKeyboardFocus (true) || selEnd_.isEditingText()) applyFields(); };
    selectAll_.setComponentID ("selectAll");
    selectAll_.setStyle (ActionButton::Style::Secondary);
    selectAll_.setLabel ("Select all");
    selectAll_.onClick = [this] { if (wave_.onSelectAll) wave_.onSelectAll(); };

    // ---- transport
    primary_.setComponentID ("primary");
    primary_.setStyle (ActionButton::Style::Primary);
    primary_.onClick = [this] { primaryAction(); };
    hold_.setComponentID ("hold");
    hold_.setStyle (ActionButton::Style::Secondary);
    hold_.onPressedChanged = [this] (bool down)
    {
        if (holdIsFinishEarly_)
            return;
        holdMouseDown_ = down;
        if (down)
            proc_.actionHoldDown();
        else
            proc_.actionHoldUp();
    };
    hold_.onClick = [this] { if (holdIsFinishEarly_) proc_.actionFinishEarly(); };
    replay_.setComponentID ("replay");
    replay_.setStyle (ActionButton::Style::Secondary);
    replay_.setIcon (Icon::Replay);
    replay_.setLabel ("Replay");
    replay_.onClick = [this] { proc_.actionReplay(); };
    save_.setComponentID ("save");
    save_.setStyle (ActionButton::Style::Secondary);
    save_.setIcon (Icon::Save);
    save_.setLabel ("Save WAV");
    save_.onClick = [this] { openExport(); };
    extra_.setComponentID ("extra");
    extra_.setStyle (ActionButton::Style::Secondary);
    extra_.onClick = [this]
    {
        if (proc_.mode() == Mode::Live)
            proc_.actionFreezeResume();
        else if (proc_.mode() == Mode::File)
            proc_.playFile (true);
    };

    // ---- options row
    styleCaption (dirLabel_, "Direction");
    styleCaption (loopLabel_, "Loop");
    styleCaption (volumeLabel_, "Volume");
    direction_.setComponentID ("direction");
    direction_.setItems ({ { "Forward", {}, true, Icon::Forward }, { "Backward", {}, true, Icon::Backward } });
    dirAttach_ = std::make_unique<SegmentedAttachment> (*apvts.getParameter (ids::direction), direction_);
    loop_.setComponentID ("loop");
    loop_.setItems ({ { "Once" }, { "Loop" }, { "Ping-pong" } });
    loopAttach_ = std::make_unique<SegmentedAttachment> (*apvts.getParameter (ids::loop), loop_);
    advancedToggle_.setComponentID ("advancedToggle");
    advancedToggle_.setStyle (ActionButton::Style::Ghost);
    advancedToggle_.setIcon (Icon::Chevron, true);
    advancedToggle_.setLabel ("Advanced");
    advancedToggle_.setTooltip ("Countdown, voice trigger, speed, fades, input and take tools");
    advancedToggle_.onClick = [this] { setAdvancedOpen (! advancedOpen_); };

    status_.setComponentID ("status");
    status_.setTitle ("Status");

    drawer_ = std::make_unique<AdvancedDrawer> (*this);

    addAll ({ &presetButton_, &menuButton_, &settingsButton_, &modeTabs_, &sourceButton_, &openButton_, &inputChannels_, &inputLabel_, &meter_, &banner_,
              &wave_, &recLabel_, &waitLabel_, &chunkLabel_, &delayLabel_, &startLabel_, &endLabel_, &lengthLabel_, &computedLabel_, &capture_, &wait_,
              &chunk_, &delay_, &selStart_, &selEnd_, &repeat_, &selectAll_, &primary_, &hold_, &replay_, &save_, &extra_, &dirLabel_, &loopLabel_, &volumeLabel_,
              &direction_, &loop_, &volume_, &advancedToggle_, &status_ });
    addChildComponent (*drawer_);
    addChildComponent (sheets_);

    setSize (metric::defaultW, metric::defaultH);
    setResizable (true, ! proc_.isStandalone());
    setResizeLimits (metric::minW, metric::minH, 1920, 1280);

    applyPrefs();
    advancedOpen_ = proc_.storedSettings().ui.advancedOpen;
    drawer_->setVisible (advancedOpen_);

    proc_.addChangeListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);
    startTimerHz (30);
    refresh();
}

ReverseBackEditor::~ReverseBackEditor()
{
    // Parameter attachments hold listeners on the widgets below: they must go first, whatever the
    // declaration order of the members is (a slider declared after the attachment vector used to be
    // destroyed before it and the attachment then touched freed memory).
    sliderAttach_.clear();
    buttonAttach_.clear();
    inputChannelsAttach_.reset();
    modeAttach_.reset();
    dirAttach_.reset();
    loopAttach_.reset();
    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    proc_.removeChangeListener (this);
    if (holdKeyDown_ || holdMouseDown_)
        proc_.actionHoldUp();   // never leave a hold stuck
    tooltip_.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Component* ReverseBackEditor::findControl (const juce::String& id)
{
    std::function<juce::Component* (juce::Component&)> search = [&] (juce::Component& c) -> juce::Component*
    {
        if (c.getComponentID() == id)
            return &c;
        for (auto* child : c.getChildren())
            if (auto* f = search (*child))
                return f;
        return nullptr;
    };
    return search (*this);
}

void ReverseBackEditor::applyPrefs()
{
    const auto& ui = proc_.storedSettings().ui;
    setReducedMotionOverride (ui.reducedMotion == 0 ? 0 : (ui.reducedMotion == 1 ? 1 : 2));
    shortcutsEnabled_ = ui.shortcutsEnabled;
    holdKeyCode_ = ui.holdKey.isNotEmpty() ? static_cast<int> (ui.holdKey[0]) : 'h';
    tooltip_.setMillisecondsBeforeTipAppears (ui.tooltips ? 700 : 3600000);
}

double ReverseBackEditor::secondsPerFrame() const { return 1.0 / std::max (1.0, proc_.currentSampleRate()); }

// ------------------------------------------------------------------------------------------ layout
void ReverseBackEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (col::bg0.brighter (0.035f), 0.0f, 0.0f, col::bg0, 0.0f, static_cast<float> (getHeight()), false));
    g.fillAll();
    if (logo_.isValid())
        g.drawImage (logo_, juce::Rectangle<float> (static_cast<float> (metric::margin), 12.0f, 40.0f, 40.0f));
    g.setColour (col::text);
    g.setFont (fonts::bold (28.0f));
    g.drawText ("ReverseBack", metric::margin + 52, 8, 240, 34, juce::Justification::centredLeft, false);
    g.setColour (col::text3);
    g.setFont (fonts::regular (13.0f));
    g.drawText ("Circuit Drift Labs  -  " RB_VERSION_STRING, metric::margin + 54, 38, 300, 18, juce::Justification::centredLeft, false);
}

void ReverseBackEditor::layoutParamRow (juce::Rectangle<int> row, Mode m)
{
    const int h = 44;
    row = row.withSizeKeepingCentre (row.getWidth(), h);
    auto place = [&row, h] (juce::Label& l, juce::Component& c, int cw)
    {
        const int lw = textWidth (l);
        l.setBounds (row.removeFromLeft (lw));
        row.removeFromLeft (10);
        c.setBounds (row.removeFromLeft (cw).withHeight (h));
        row.removeFromLeft (28);
    };
    if (m == Mode::Record)
    {
        place (recLabel_, capture_, 132);
        place (waitLabel_, wait_, 132);
        repeat_.setBounds (row.removeFromLeft (200));
    }
    else if (m == Mode::Live)
    {
        place (chunkLabel_, chunk_, 120);
        place (delayLabel_, delay_, 120);
        computedLabel_.setBounds (row);
    }
    else
    {
        place (startLabel_, selStart_, 136);
        place (endLabel_, selEnd_, 136);
        lengthLabel_.setBounds (row.removeFromLeft (150));
        selectAll_.setBounds (row.removeFromLeft (118).withHeight (h));
    }
}

void ReverseBackEditor::layoutTransport (juce::Rectangle<int> row, Mode m)
{
    const int gap = metric::gutter;
    if (m == Mode::Record)
    {
        const int w = row.getWidth() - 3 * gap;
        primary_.setBounds (row.removeFromLeft (w * 34 / 100));
        row.removeFromLeft (gap);
        hold_.setBounds (row.removeFromLeft (w * 26 / 100));
        row.removeFromLeft (gap);
        replay_.setBounds (row.removeFromLeft (w * 18 / 100));
        row.removeFromLeft (gap);
        save_.setBounds (row);
    }
    else
    {
        const int w = row.getWidth() - 2 * gap;
        primary_.setBounds (row.removeFromLeft (w * 42 / 100));
        row.removeFromLeft (gap);
        extra_.setBounds (row.removeFromLeft (w * 30 / 100));
        row.removeFromLeft (gap);
        save_.setBounds (row);
    }
}

void ReverseBackEditor::layoutOptions (juce::Rectangle<int> row, Mode)
{
    const int h = 40;
    row = row.withSizeKeepingCentre (row.getWidth(), h);
    constexpr int dirW = 206, loopW = 236;
    const int dirLabelW = textWidth (dirLabel_), loopLabelW = textWidth (loopLabel_), volLabelW = textWidth (volumeLabel_);
    // Natural layout first; when the window is narrow the Direction/Loop captions go (the controls name
    // themselves), then Advanced and the volume field shrink. At the 820 px minimum everything still fits.
    constexpr int gap = 16;
    const int natural = (dirLabelW + 8 + dirW + gap) + (loopLabelW + 8 + loopW + gap) + (volLabelW + 8 + 104) + 136;
    const bool captions = natural <= row.getWidth();
    int advW = 136, volW = 104;
    if (! captions)
    {
        const int needed = (dirW + gap) + (loopW + gap) + (volLabelW + 8 + volW) + advW;
        if (needed > row.getWidth())
        {
            advW = 112;
            volW = 92;
        }
    }
    dirLabel_.setVisible (captions);
    loopLabel_.setVisible (captions);

    advancedToggle_.setBounds (row.removeFromRight (advW));
    auto place = [&row, h] (juce::Label* l, int labelW, juce::Component& c, int cw)
    {
        if (l != nullptr)
        {
            l->setBounds (row.removeFromLeft (labelW));
            row.removeFromLeft (8);
        }
        c.setBounds (row.removeFromLeft (cw).withHeight (h));
        row.removeFromLeft (gap);
    };
    place (captions ? &dirLabel_ : nullptr, dirLabelW, direction_, dirW);
    place (captions ? &loopLabel_ : nullptr, loopLabelW, loop_, loopW);
    volumeLabel_.setBounds (row.removeFromLeft (volLabelW));
    row.removeFromLeft (8);
    volume_.setBounds (row.removeFromLeft (volW).withHeight (h));
}

void ReverseBackEditor::resized()
{
    const Mode m = proc_.mode();
    auto b = getLocalBounds();
    const int mx = metric::margin;

    auto header = b.removeFromTop (metric::headerH);
    {
        auto h = header.reduced (mx, 12);
        settingsButton_.setBounds (h.removeFromRight (40));
        h.removeFromRight (8);
        menuButton_.setBounds (h.removeFromRight (40));
        h.removeFromRight (12);
        presetButton_.setBounds (h.removeFromRight (150));
    }
    modeTabs_.setBounds (b.removeFromTop (metric::tabsH).reduced (mx, 4));
    b.removeFromTop (8);

    auto source = b.removeFromTop (metric::sourceH).reduced (mx, 0);
    meter_.setBounds (source.removeFromRight (266));
    inputLabel_.setBounds (source.removeFromRight (50));
    source.removeFromRight (12);
    if (m == Mode::File)
    {
        openButton_.setBounds (source.removeFromRight (130));
        source.removeFromRight (10);
        sourceButton_.setBounds (source);
        inputChannels_.setBounds ({});
    }
    else
    {
        const bool showChannels = true;
        if (showChannels)
        {
            inputChannels_.setBounds (source.removeFromRight (140));
            source.removeFromRight (10);
        }
        if (m == Mode::Live)
            source.removeFromRight (0);
        sourceButton_.setBounds (source.removeFromLeft (std::min (source.getWidth(), proc_.isStandalone() ? 420 : 240)));
        openButton_.setBounds ({});
    }
    b.removeFromTop (12);

    auto statusRow = b.removeFromBottom (metric::statusH);
    status_.setBounds (statusRow.reduced (mx, 0));
    b.removeFromBottom (6);
    auto optionsRow = b.removeFromBottom (metric::optionsH).reduced (mx, 0);
    b.removeFromBottom (12);
    auto transportRow = b.removeFromBottom (metric::transportH).reduced (mx, 0);
    b.removeFromBottom (12);
    auto paramRow = b.removeFromBottom (metric::paramH).reduced (mx, 0);
    b.removeFromBottom (12);

    if (banner_.isVisible())
    {
        banner_.setBounds (b.removeFromTop (metric::bannerH).reduced (mx, 0));
        b.removeFromTop (8);
    }
    contentArea_ = b.reduced (mx, 0);
    auto waveArea = contentArea_;
    if (advancedOpen_ && drawer_ != nullptr)
    {
        drawer_->setBounds (waveArea.removeFromRight (metric::drawerW));
        waveArea.removeFromRight (metric::gutter);
        drawer_->setVisible (true);
    }
    else if (drawer_ != nullptr)
    {
        drawer_->setVisible (false);
    }
    wave_.setBounds (waveArea);

    layoutParamRow (paramRow, m);
    layoutTransport (transportRow, m);
    layoutOptions (optionsRow, m);
    sheets_.setBounds (getLocalBounds());
}

void ReverseBackEditor::applyModeLayout (Mode m)
{
    const auto accent = accentFor (m);
    const bool rec = m == Mode::Record, live = m == Mode::Live, file = m == Mode::File;
    modeTabs_.setAccent (accent.fill);
    for (auto* n : { &capture_, &wait_, &chunk_, &delay_, &selStart_, &selEnd_, &volume_ })
        n->setAccent (accent.fill);
    for (auto* t : { &repeat_ })
        t->setColour (juce::Slider::trackColourId, accent.fill);
    for (auto* s : { &direction_, &loop_ })
        s->setAccent (accent.fill);
    for (auto* b : { &primary_ })
        b->setAccent (accent);
    save_.setAccent (accent);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &recLabel_, &waitLabel_, &capture_, &wait_, &repeat_, &hold_, &replay_ })
        c->setVisible (rec);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &chunkLabel_, &delayLabel_, &chunk_, &delay_, &computedLabel_ })
        c->setVisible (live);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &startLabel_, &endLabel_, &selStart_, &selEnd_, &lengthLabel_, &selectAll_, &openButton_ })
        c->setVisible (file);
    extra_.setVisible (! rec);
    inputChannels_.setVisible (! file);
    meter_.setVisible (! file);
    inputLabel_.setVisible (! file);
    modeLaidOut_ = true;
    lastMode_ = m;
    if (drawer_)
        drawer_->setMode (m);
    if (file)
        sourceButton_.setIcon (Icon::Folder);
    else
        sourceButton_.setIcon (Icon::Mic);
    repaint();
    resized();
}

void ReverseBackEditor::setAdvancedOpen (bool open)
{
    advancedOpen_ = open;
    advancedToggle_.setIcon (open ? Icon::ChevronUp : Icon::Chevron, true);
    proc_.storedSettings().ui.advancedOpen = open;
    proc_.saveStoredSettings();
    if (drawer_)
        drawer_->setVisible (open);
    resized();
    repaint();
}

// ------------------------------------------------------------------------------------------ refresh
void ReverseBackEditor::refresh()
{
    const Snapshot s = proc_.snapshot();
    const Mode m = proc_.mode();
    if (! modeLaidOut_ || m != lastMode_)
        applyModeLayout (m);

    meter_.update (s.inputPeak, s.overloadBlocks);
    meter_.setDescription (meter_.summary());
    updateBanner();
    updateTransport (s, m);
    updateWaveform (s, m);
    updateParamCaptions (m, s);
    updateStatus (s, m);
    if (drawer_ && advancedOpen_)
        drawer_->refreshState (s, m);
    dirLabel_.setEnabled (m != Mode::Live);
    direction_.setEnabled (m != Mode::Live);
    direction_.setTooltip (m == Mode::Live ? "Live always plays each chunk backwards." : "Which way the sound plays. Press F to flip.");
    loop_.setEnabled (m != Mode::Live);
    loop_.setTooltip (m == Mode::Live ? "Looping applies to Record and File playback. Use Freeze to loop a live chunk." : "Once, Loop, or Ping-pong (alternates forward and backward).");

    if (holdKeyDown_ && ! juce::KeyPress::isKeyCurrentlyDown (holdKeyCode_))
        releaseHold();   // safety net if a key-up was missed
}

void ReverseBackEditor::updateBanner()
{
    const Banner& b = proc_.banner();
    const bool show = b.kind != BannerKind::None;
    if (show != banner_.isVisible() || b.id != shownBannerId_)
    {
        shownBannerId_ = b.id;
        banner_.setContent (b.text, b.kind != BannerKind::Info, b.canRetry, b.canOpenSettings, b.reveal != juce::File());
        if (show != banner_.isVisible())
        {
            banner_.setVisible (show);
            resized();
        }
    }
}

void ReverseBackEditor::updateTransport (const Snapshot& s, Mode m)
{
    const bool busy = proc_.isBusy();
    const auto rs = static_cast<RecordTransport::State> (s.recordState);
    const auto ls = static_cast<LiveTransport::State> (s.liveState);
    const bool backward = proc_.apvts.getRawParameterValue (ids::direction)->load() >= 0.5f;
    const auto take = proc_.currentTake();
    const auto file = proc_.fileAsset();

    primary_.setStyle (busy ? ActionButton::Style::Primary : ActionButton::Style::Primary);
    auto setPrimary = [this] (const juce::String& label, Icon icon, bool stop, bool enabled, const juce::String& tip)
    {
        primary_.setStyle (stop ? ActionButton::Style::Danger : ActionButton::Style::Primary);
        primary_.setLabel (label);
        primary_.setIcon (icon);
        primary_.setEnabled (enabled);
        primary_.setTooltip (tip);
    };

    const ExportSource es = busy ? ExportSource {} : proc_.exportSource();
    if (m == Mode::Record)
    {
        const bool ready = rs == RecordTransport::State::Ready;
        setPrimary (ready ? "Record & Reverse" : "Stop", ready ? Icon::Record : Icon::Stop, ! ready, true,
                    ready ? "Record, wait, then hear it back (Space)" : "Stop and discard the unfinished recording (Esc)");
        const bool recording = rs == RecordTransport::State::Recording;
        holdIsFinishEarly_ = recording && s.recordHeld == 0;
        if (holdIsFinishEarly_)
        {
            hold_.setLabel ("Finish Early");
            hold_.setIcon (Icon::Check);
            hold_.setEnabled (true);
            hold_.setTooltip ("Keep what has been recorded so far (needs at least 50 ms)");
        }
        else if (recording)
        {
            hold_.setLabel ("Release to Finish");
            hold_.setIcon (Icon::Hold);
            hold_.setEnabled (true);
        }
        else
        {
            hold_.setLabel ("Hold to Record");
            hold_.setIcon (Icon::Hold);
            hold_.setEnabled (ready);
            hold_.setTooltip (ready ? "Press and hold to record, release to hear it (key: " + juce::String::charToString (static_cast<juce::juce_wchar> (holdKeyCode_)).toUpperCase() + ")"
                                    : "Wait for the current session to finish, or press Stop");
        }
        replay_.setEnabled (take != nullptr && ready);
        replay_.setTooltip (take ? "Play the last take again without recording (R)" : "Record something first");
        save_.setEnabled (es.valid());
        save_.setTooltip (es.valid() ? "Save the take as a WAV file (Ctrl+S)" : (take ? "Wait for playback to finish" : "Record something first"));
    }
    else if (m == Mode::Live)
    {
        const bool ready = ls == LiveTransport::State::Ready;
        setPrimary (ready ? "Start Live" : "Stop", ready ? Icon::Play : Icon::Stop, ! ready, true,
                    ready ? "Start continuously reversing chunks of the microphone (Space). Headphones recommended." : "Stop and clear the live buffers (Esc)");
        const bool frozen = ls == LiveTransport::State::Frozen || ls == LiveTransport::State::Freezing;
        extra_.setLabel (ls == LiveTransport::State::Freezing ? "Freezing..." : (frozen ? "Resume" : "Freeze"));
        extra_.setIcon (frozen ? Icon::Play : Icon::Freeze);
        extra_.setEnabled (ls != LiveTransport::State::Ready);
        extra_.setTooltip (ls == LiveTransport::State::Ready ? "Start Live first, then Freeze loops the current chunk"
                                                             : (frozen ? "Return to live input with a fresh buffer" : "Repeat the current chunk until you press Resume"));
        save_.setLabel ("Save Frozen WAV");
        save_.setEnabled (ls == LiveTransport::State::Frozen);
        save_.setTooltip (ls == LiveTransport::State::Frozen ? "Save the frozen chunk as a WAV file" : "Freeze a chunk first");
    }
    else
    {
        const bool playing = s.filePlaying != 0;
        const bool have = file != nullptr;
        setPrimary (playing ? "Stop" : (backward ? "Play Backwards" : "Play Selection"), playing ? Icon::Stop : Icon::Play, playing, have,
                    have ? (playing ? "Stop playback (Space)" : "Play the selected region from the playhead (Space)") : "Open a file first");
        extra_.setLabel ("Play from Start");
        extra_.setIcon (Icon::Replay);
        extra_.setEnabled (have);
        extra_.setTooltip (have ? "Restart at the start of the selection in the current direction" : "Open a file first");
        save_.setEnabled (es.valid());
        save_.setTooltip (es.valid() ? "Save the selection as a WAV file (Ctrl+S)" : (have ? "Stop playback first" : "Open a file first"));
    }
    if (m != Mode::Live)
        save_.setLabel ("Save WAV");
    save_.setIcon (Icon::Save);
    hold_.setVisible (m == Mode::Record);
    replay_.setVisible (m == Mode::Record);
    primary_.setTitle (primary_.getButtonText());
    openButton_.setLabel (proc_.isLoadingFile() ? "Cancel loading" : "Open...");
    repeat_.setEnabled (m == Mode::Record && s.recordState == 0
                        && juce::roundToInt (proc_.apvts.getRawParameterValue (ids::loop)->load()) == 0);
    repeat_.setTooltip (juce::roundToInt (proc_.apvts.getRawParameterValue (ids::loop)->load()) != 0 ? "Turn off Loop to repeat sessions"
                                                                                                    : "Record, play back, then record again until stopped");
    const bool recBusy = m == Mode::Record && s.recordState != 0;
    capture_.setEnabled (! recBusy);
    wait_.setEnabled (! recBusy);
    capture_.setTooltip (recBusy ? "Applies to the next recording" : "How long to record (0.25 to 60 s).");
    const bool liveBusy = m == Mode::Live && ls != LiveTransport::State::Ready;
    chunk_.setEnabled (! liveBusy);
    delay_.setEnabled (! liveBusy);
    chunk_.setTooltip (liveBusy ? "Stop Live to change this" : "Live Reverse reverses each chunk of this length (0.1 to 5 s).");
    delay_.setTooltip (liveBusy ? "Stop Live to change this" : "Extra delay added after each chunk completes (0 to 30 s).");
    selStart_.setEnabled (file != nullptr && s.filePlaying == 0);
    selEnd_.setEnabled (file != nullptr && s.filePlaying == 0);
    selectAll_.setEnabled (file != nullptr && s.filePlaying == 0);
    inputChannels_.setEnabled (! busy);
}

void ReverseBackEditor::updateParamCaptions (Mode m, const Snapshot&)
{
    if (m == Mode::Live)
    {
        const double w = static_cast<double> (proc_.apvts.getRawParameterValue (ids::chunk)->load());
        const double d = static_cast<double> (proc_.apvts.getRawParameterValue (ids::delay)->load());
        computedLabel_.setText ("First sound after " + juce::String (w + d, 2) + " s + device buffering", juce::dontSendNotification);
    }
    else if (m == Mode::File)
    {
        if (auto f = proc_.fileAsset())
        {
            const Selection sel = proc_.fileSelection();
            const double rate = f->sampleRate;
            const bool editing = selStart_.isEditingText() || selEnd_.isEditingText() || selStart_.isMouseButtonDown() || selEnd_.isMouseButtonDown();
            if (! editing)
            {
                selStart_.setRange (0.0, f->durationSeconds(), 0.001);
                selEnd_.setRange (0.0, f->durationSeconds(), 0.001);
                selStart_.setValue (static_cast<double> (sel.begin) / rate, juce::dontSendNotification);
                selEnd_.setValue (static_cast<double> (sel.end) / rate, juce::dontSendNotification);
            }
            lengthLabel_.setText ("Length " + juce::String (static_cast<double> (sel.length()) / rate, 2) + " s", juce::dontSendNotification);
        }
        else
        {
            selStart_.setValue (0.0, juce::dontSendNotification);
            selEnd_.setValue (0.0, juce::dontSendNotification);
            lengthLabel_.setText ("Length -", juce::dontSendNotification);
        }
    }
}

void ReverseBackEditor::updateWaveform (const Snapshot& s, Mode m)
{
    WaveformView::Model w;
    w.mode = m;
    w.accent = accentFor (m);
    w.version = waveVersion_;
    w.dropHighlight = dropHover_;
    const bool backward = proc_.apvts.getRawParameterValue (ids::direction)->load() >= 0.5f;
    w.chipDirection = backward ? Direction::Backward : Direction::Forward;
    const double rate = std::max (1.0, proc_.currentSampleRate());

    if (m == Mode::Record)
    {
        const auto take = proc_.currentTake();
        const auto rs = static_cast<RecordTransport::State> (s.recordState);
        if (take && take->id != lastTakeVersion_)
        {
            lastTakeVersion_ = take->id;
            ++waveVersion_;
        }
        if (! take && lastTakeVersion_ != ~0u)
        {
            lastTakeVersion_ = ~0u;
            ++waveVersion_;
        }
        const std::uint32_t trimMarker = take ? static_cast<std::uint32_t> (take->trim.begin * 31u + take->trim.end) : 0;
        if (trimMarker != lastTrimMarker_)
        {
            lastTrimMarker_ = trimMarker;
            ++waveVersion_;
        }
        w.version = waveVersion_;
        w.overview = take ? take->overview : nullptr;
        w.durationSeconds = take ? take->clip->frameCount() / take->clip->sampleRate() : 0.0;
        w.header = take ? "Latest take  -  " + juce::String (w.durationSeconds, 2) + " seconds" : "No take yet";
        w.rulerLeft = "0:00";
        w.rulerRight = take ? fmtClock (w.durationSeconds) : juce::String();
        w.emptyMessage = "Press Record & Reverse, then say something.\nYou will hear it backwards after the wait.";
        const bool playing = rs == RecordTransport::State::Playing;
        w.showPlayhead = playing;
        w.playhead = s.playhead;
        if (take && ! take->trim.empty())
        {
            const double total = static_cast<double> (take->clip->frameCount());
            w.showSelection = true;
            w.selBegin = static_cast<double> (take->trim.begin) / total;
            w.selEnd = static_cast<double> (take->trim.end) / total;
            w.selectionEditable = false;
        }
        w.chip = playing ? (backward ? "Playing backwards" : "Playing forwards") : (backward ? "Backward - ready" : "Forward - ready");
        w.dimmed = rs == RecordTransport::State::Recording || rs == RecordTransport::State::Countdown || rs == RecordTransport::State::Armed;

        const double perFrame = 1.0 / rate;
        if (rs == RecordTransport::State::Countdown)
        {
            w.overlay = WaveformView::Overlay::Countdown;
            w.countdownNumber = std::max (1, static_cast<int> (std::ceil (static_cast<double> (s.countdownLeft) * perFrame)));
        }
        else if (rs == RecordTransport::State::Armed)
        {
            w.overlay = WaveformView::Overlay::Waiting;
            w.overlayText = "Listening for your voice";
            w.overlaySub = "Recording starts when sound is detected";
        }
        else if (rs == RecordTransport::State::Recording)
        {
            w.overlay = WaveformView::Overlay::Recording;
            const double left = std::max (0.0, static_cast<double> (s.stateLength - s.stateFrame) * perFrame);
            const double done = static_cast<double> (s.stateFrame) * perFrame;
            w.overlayText = s.recordHeld ? "Recording " + juce::String (done, 1) + " s" : "Recording  -  " + juce::String (left, 1) + " s left";
            w.overlaySub = s.recordHeld ? "Release to finish" : "of " + juce::String (static_cast<double> (s.stateLength) * perFrame, 2) + " s";
            w.overlayProgress = s.stateLength > 0 ? static_cast<float> (static_cast<double> (s.stateFrame) / static_cast<double> (s.stateLength)) : 0.0f;
        }
        else if (rs == RecordTransport::State::Waiting)
        {
            w.overlay = WaveformView::Overlay::Waiting;
            w.overlayText = "Waiting  -  " + juce::String (std::max (0.0, static_cast<double> (s.stateLength - s.stateFrame) * perFrame), 1) + " s";
            w.overlaySub = "Playback starts when the wait ends";
            w.overlayProgress = s.stateLength > 0 ? static_cast<float> (static_cast<double> (s.stateFrame) / static_cast<double> (s.stateLength)) : 0.0f;
            w.dimmed = false;
        }
    }
    else if (m == Mode::File)
    {
        const auto file = proc_.fileAsset();
        if (proc_.fileVersion() != lastFileVersion_)
        {
            lastFileVersion_ = proc_.fileVersion();
            preTrimSelection_ = {};   // "Undo trim" must never restore the frame range of a previous file
            ++waveVersion_;
        }
        w.version = waveVersion_;
        w.overview = file ? file->overview : nullptr;
        w.emptyMessage = "Drop a WAV, AIFF or FLAC file here,\nor press Open... (Ctrl+O)";
        if (file)
        {
            const Selection sel = proc_.fileSelection();
            const double total = static_cast<double> (file->frames);
            w.durationSeconds = file->durationSeconds();
            w.showSelection = true;
            w.selBegin = static_cast<double> (sel.begin) / total;
            w.selEnd = static_cast<double> (sel.end) / total;
            w.selectionEditable = s.filePlaying == 0;
            w.header = "Selection  -  " + juce::String (static_cast<double> (sel.length()) / file->sampleRate, 1) + " s of " + fmtClock (file->durationSeconds())
                       + "  -  " + file->name;
            w.rulerLeft = "0:00";
            w.rulerRight = fmtClock (file->durationSeconds());
            w.showPlayhead = s.filePlaying != 0 || s.playhead > 0.0f;
            const double n = w.selBegin + static_cast<double> (s.playhead) * (w.selEnd - w.selBegin);
            w.playhead = static_cast<float> (n);
            w.chip = s.filePlaying ? (backward ? "Playing backwards" : "Playing forwards") : (backward ? "Backward - ready" : "Forward - ready");
        }
        else
        {
            w.header = "No file loaded";
        }
        if (proc_.isLoadingFile())
        {
            w.overlay = WaveformView::Overlay::Filling;
            w.overlayText = "Loading  " + juce::String (juce::roundToInt (proc_.loadProgress() * 100.0f)) + " %";
            w.overlaySub = "Cancel to keep the current file";
            w.overlayProgress = proc_.loadProgress();
        }
    }
    else   // Live
    {
        const auto ls = static_cast<LiveTransport::State> (s.liveState);
        const auto storage = proc_.liveStorage();
        const Settings cfg = proc_.paramRefs().readSettings();
        const double wSec = cfg.liveChunkSeconds;
        w.durationSeconds = wSec;
        w.rulerLeft = "0:00";
        w.rulerRight = juce::String (wSec * 1000.0, 0) + " ms";
        w.emptyMessage = "Press Start Live and talk.\nEach chunk is played back reversed.";
        w.chip = ls == LiveTransport::State::Frozen ? "Looping backwards" : "Chunks play backwards";
        w.chipDirection = Direction::Backward;
        w.frozenBadge = ls == LiveTransport::State::Frozen;

        if (storage && s.liveSlot >= 0 && (ls == LiveTransport::State::Running || ls == LiveTransport::State::Freezing || ls == LiveTransport::State::Frozen))
        {
            if (s.liveSlot != lastSlot_ || s.liveChunkIndex != lastChunk_ || proc_.frozenVersion() != lastFrozenVersion_)
            {
                lastSlot_ = s.liveSlot;
                lastChunk_ = s.liveChunkIndex;
                lastFrozenVersion_ = proc_.frozenVersion();
                ++waveVersion_;
            }
            w.liveEnv.assign (storage->envelope (static_cast<Frame> (s.liveSlot)), storage->envelope (static_cast<Frame> (s.liveSlot)) + LiveStorage::kEnvPoints * 2);
            w.header = ls == LiveTransport::State::Frozen ? "Frozen chunk" : "Now playing  -  chunk " + juce::String (s.liveChunkIndex + 1) + "  (" + fmtMillis (wSec) + ")";
            // playhead: playback offset inside the chunk, travelling right to left
            const double W = static_cast<double> (storage->W), D = static_cast<double> (storage->D);
            const double cap = static_cast<double> (s.stateFrame);   // capture offset within the chunk being recorded
            const double dm = std::fmod (D, W);
            double u = std::fmod (cap - dm + W, W);
            if (ls == LiveTransport::State::Frozen)
                u = std::fmod (cap, W);   // loop phase is only approximate while frozen
            w.showPlayhead = true;
            w.playhead = static_cast<float> (1.0 - u / W);
        }
        else
        {
            if (lastSlot_ != -2)
            {
                lastSlot_ = -2;
                ++waveVersion_;
            }
            w.header = ls == LiveTransport::State::Ready ? "Live Reverse" : "Waiting for the first chunk";
        }
        w.version = waveVersion_;
        const double perFrame = 1.0 / rate;
        if (ls == LiveTransport::State::Filling)
        {
            w.overlay = WaveformView::Overlay::Filling;
            const double total = (cfg.liveChunkSeconds + cfg.liveDelaySeconds);
            w.overlayText = "Filling buffer";
            w.overlaySub = "First sound in " + juce::String (std::max (0.0, total * (1.0 - static_cast<double> (s.fillProgress))), 1) + " s";
            w.overlayProgress = s.fillProgress;
        }
        else if (ls == LiveTransport::State::Running)
        {
            w.overlay = WaveformView::Overlay::Filling;
            w.overlayText = {};
            w.overlaySub = {};
            w.overlayProgress = s.stateLength > 0 ? static_cast<float> (static_cast<double> (s.stateFrame) / static_cast<double> (s.stateLength)) : 0.0f;
            juce::ignoreUnused (perFrame);
        }
    }
    wave_.setModel (std::move (w));
    wave_.setDescription (wave_.model().header);
}

void ReverseBackEditor::updateStatus (const Snapshot& s, Mode m)
{
    const double rate = std::max (1.0, proc_.currentSampleRate());
    const double perFrame = 1.0 / rate;
    const auto accent = accentFor (m);
    const bool backward = proc_.apvts.getRawParameterValue (ids::direction)->load() >= 0.5f;
    juce::String state, hint;
    juce::Colour dot = col::ok;
    const Settings cfg = proc_.paramRefs().readSettings();

    if (m == Mode::Record)
    {
        switch (static_cast<RecordTransport::State> (s.recordState))
        {
            case RecordTransport::State::Ready:
                state = "Ready";
                hint = "Speak for " + juce::String (cfg.captureSeconds, cfg.captureSeconds == std::floor (cfg.captureSeconds) ? 0 : 2) + " seconds. Hear it "
                       + (backward ? "backwards" : "played forwards") + " after a " + juce::String (cfg.waitSeconds, cfg.waitSeconds == std::floor (cfg.waitSeconds) ? 0 : 2) + "-second wait.";
                break;
            case RecordTransport::State::Countdown:
                state = "Starting in " + juce::String (std::max (1, static_cast<int> (std::ceil (static_cast<double> (s.countdownLeft) * perFrame))));
                hint = "Get ready";
                dot = col::warn;
                break;
            case RecordTransport::State::Armed:
                state = "Listening for your voice";
                hint = "Recording starts when sound is detected. Press Stop to cancel.";
                dot = col::warn;
                break;
            case RecordTransport::State::Recording:
                dot = col::rec;
                if (s.recordHeld)
                {
                    state = "Recording - release to finish";
                }
                else
                {
                    state = "Recording - " + juce::String (std::max (0.0, static_cast<double> (s.stateLength - s.stateFrame) * perFrame), 1) + " s left";
                    hint = "Press Stop to cancel, or Finish Early to keep what you have.";
                }
                break;
            case RecordTransport::State::Waiting:
                state = "Waiting - " + juce::String (std::max (0.0, static_cast<double> (s.stateLength - s.stateFrame) * perFrame), 1) + " s";
                dot = col::warn;
                break;
            case RecordTransport::State::Playing:
                state = backward ? "Playing backwards" : "Playing forwards";
                dot = accent.wave;
                break;
            case RecordTransport::State::ReadyGap:
                state = "Next take in " + juce::String (std::max (0.0, static_cast<double> (s.stateLength - s.stateFrame) * perFrame), 1) + " s";
                hint = "Repeat Session is on. Press Stop to end.";
                dot = col::warn;
                break;
        }
    }
    else if (m == Mode::Live)
    {
        switch (static_cast<LiveTransport::State> (s.liveState))
        {
            case LiveTransport::State::Ready:
                state = "Ready";
                hint = "Headphones recommended for Live Reverse.";
                break;
            case LiveTransport::State::Filling:
                state = "Filling buffer - first sound in " + juce::String (std::max (0.0, (cfg.liveChunkSeconds + cfg.liveDelaySeconds) * (1.0 - static_cast<double> (s.fillProgress))), 1) + " s";
                hint = "Headphones recommended";
                dot = col::warn;
                break;
            case LiveTransport::State::Running:
                state = "Live - reversing " + fmtMillis (cfg.liveChunkSeconds) + " chunks";
                dot = accent.wave;
                break;
            case LiveTransport::State::Freezing:
                state = "Freezing...";
                dot = col::warn;
                break;
            case LiveTransport::State::Frozen:
                state = "Frozen - looping last chunk";
                hint = "Press Resume to go live again.";
                dot = accent.wave;
                break;
        }
    }
    else
    {
        const auto file = proc_.fileAsset();
        if (proc_.isLoadingFile())
        {
            state = "Loading... " + juce::String (juce::roundToInt (proc_.loadProgress() * 100.0f)) + " %";
            hint = "Cancel to keep the current file.";
            dot = col::warn;
        }
        else if (! file)
        {
            state = "No file";
            hint = "Open or drop a WAV, AIFF or FLAC file.";
            dot = col::text3;
        }
        else if (s.filePlaying)
        {
            state = backward ? "Playing selection backwards" : "Playing selection forwards";
            dot = accent.wave;
        }
        else
        {
            state = "Ready";
            hint = file->name + "  -  " + fmtClock (file->durationSeconds()) + ", " + juce::String (juce::roundToInt (file->sampleRate)) + " Hz, "
                   + (file->channels == 1 ? "mono" : "stereo");
        }
    }

    juce::String right;
    if (auto* h = proc_.hostServices())
    {
        right = h->getDeviceSummary();
        if (proc_.getLatencySamples() > 0)
            right += "  +  limiter " + juce::String (proc_.getLatencySamples() * 1000.0 / rate, 1) + " ms";
    }
    status_.set (state, dot, hint, right);
    status_.setDescription (state + ". " + hint);
    sourceButton_.setLabel (m == Mode::File ? (proc_.fileAsset() ? proc_.fileAsset()->name : juce::String ("No file loaded"))
                                            : (proc_.hostServices() != nullptr ? "Microphone: " + (proc_.hostServices()->getDeviceManager() != nullptr && proc_.hostServices()->getDeviceManager()->getCurrentAudioDevice() != nullptr
                                                                                                    ? proc_.hostServices()->getDeviceManager()->getAudioDeviceSetup().inputDeviceName.substring (0, 40)
                                                                                                    : juce::String ("none"))
                                                                               : juce::String ("Input: host track")));
    if (sourceButton_.getButtonText().endsWith (": ") || sourceButton_.getButtonText() == "Microphone: ")
        sourceButton_.setLabel ("Microphone: System default");
    sourceButton_.setTooltip (m == Mode::File ? "The loaded file" : (proc_.hostServices() != nullptr ? "Choose the microphone or open audio settings" : "ReverseBack records the audio your host sends to this track"));
}

// ------------------------------------------------------------------------------------------ actions
void ReverseBackEditor::primaryAction()
{
    proc_.actionStartStop();
}

void ReverseBackEditor::toggleDirection()
{
    if (proc_.mode() == Mode::Live)
        return;
    if (auto* p = proc_.apvts.getParameter (ids::direction))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ReverseBackEditor::openFileDialog()
{
    const juce::String last = proc_.lastFolder();
    juce::File start = last.isNotEmpty() ? juce::File (last) : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser_ = std::make_unique<juce::FileChooser> ("Open audio file", start, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<ReverseBackEditor> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr)
                                   return;
                               const auto f = fc.getResult();
                               if (f == juce::File())
                                   return;
                               if (auto* p = safe->proc_.apvts.getParameter (ids::mode))
                                   p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
                               safe->proc_.loadFile (f);
                           });
}

void ReverseBackEditor::openSettings (int tab)
{
    sheets_.show (std::make_unique<SettingsSheet> (proc_, [this] { applyPrefs(); refresh(); }, tab));
}

void ReverseBackEditor::openExport()
{
    const ExportSource src = proc_.exportSource();
    if (! src.valid())
        return;
    sheets_.show (std::make_unique<ExportSheet> (proc_, src));
}

void ReverseBackEditor::showMainMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&laf_);
    const auto mode = proc_.mode();
    const bool busy = proc_.isBusy();
    auto item = [&m] (int id, const juce::String& text, const juce::String& key, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item it (text);
        it.itemID = id;
        it.isEnabled = enabled;
        it.isTicked = ticked;
        it.shortcutKeyDescription = key;
        m.addItem (it);
    };
    item (1, "Open file...", "Ctrl+O");
    item (2, "Save WAV...", "Ctrl+S", proc_.exportSource().valid());
    m.addSeparator();
    item (3, busy ? "Stop" : (mode == Mode::Record ? "Record & Reverse" : (mode == Mode::Live ? "Start Live" : "Play")), "Space");
    item (4, "Replay", "R", mode == Mode::Record && proc_.currentTake() != nullptr && ! busy);
    item (5, "Toggle direction", "F", mode != Mode::Live);
    item (6, "Stop / cancel", "Esc", busy);
    item (7, "Hold to Record: hold the button or key", juce::String::charToString (static_cast<juce::juce_wchar> (holdKeyCode_)).toUpperCase(), false);
    m.addSeparator();
    item (8, "Settings...", {});
    item (9, "Keyboard shortcuts", {}, true, shortcutsEnabled_);
    item (10, "Advanced panel", {}, true, advancedOpen_);
    item (11, "About ReverseBack", {});
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton_).withMinimumWidth (300),
                     [safe = juce::Component::SafePointer<ReverseBackEditor> (this)] (int r)
                     {
                         if (safe == nullptr)
                             return;
                         auto& e = *safe;
                         switch (r)
                         {
                             case 1: e.openFileDialog(); break;
                             case 2: e.openExport(); break;
                             case 3: e.proc_.actionStartStop(); break;
                             case 4: e.proc_.actionReplay(); break;
                             case 5: e.toggleDirection(); break;
                             case 6: e.proc_.actionStop(); break;
                             case 8: e.openSettings(); break;
                             case 9:
                                 e.proc_.storedSettings().ui.shortcutsEnabled = ! e.shortcutsEnabled_;
                                 e.proc_.saveStoredSettings();
                                 e.applyPrefs();
                                 break;
                             case 10: e.setAdvancedOpen (! e.advancedOpen_); break;
                             case 11: e.openSettings (2); break;
                             default: break;
                         }
                     });
}

void ReverseBackEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&laf_);
    const auto& builtin = builtinPresets();
    int id = 100;
    for (const auto& p : builtin)
        m.addItem (id++, p.name);
    const auto user = proc_.userPresets();
    if (! user.empty())
    {
        m.addSeparator();
        int uid = 200;
        for (const auto& p : user)
            m.addItem (uid++, p.name);
    }
    m.addSeparator();
    m.addItem (300, "Save current settings as...");
    if (! user.empty())
    {
        juce::PopupMenu del;
        del.setLookAndFeel (&laf_);
        int did = 400;
        for (const auto& p : user)
            del.addItem (did++, p.name);
        m.addSubMenu ("Delete preset", del);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton_).withMinimumWidth (240),
                     [safe = juce::Component::SafePointer<ReverseBackEditor> (this), user] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         auto& e = *safe;
                         if (r >= 100 && r < 200)
                             e.proc_.applyPreset (builtinPresets()[static_cast<std::size_t> (r - 100)].values);
                         else if (r >= 200 && r < 300)
                             e.proc_.applyPreset (user[static_cast<std::size_t> (r - 200)].values);
                         else if (r == 300)
                             e.sheets_.show (std::make_unique<PromptSheet> ("Save preset", "Preset name", "My preset", "Save",
                                                                           [safe2 = safe] (const juce::String& name) { if (safe2 != nullptr) safe2->proc_.saveUserPreset (name); }));
                         else if (r >= 400)
                         {
                             const auto name = user[static_cast<std::size_t> (r - 400)].name;
                             e.sheets_.show (std::make_unique<ConfirmSheet> ("Delete preset", "Delete the preset \"" + name + "\"?", "Delete",
                                                                            [safe2 = safe, name] { if (safe2 != nullptr) safe2->proc_.deleteUserPreset (name); }));
                         }
                     });
}

void ReverseBackEditor::showDeviceMenu()
{
    if (proc_.mode() == Mode::File)
    {
        if (proc_.fileAsset())
            return;
        openFileDialog();
        return;
    }
    auto* host = proc_.hostServices();
    if (host == nullptr || host->getDeviceManager() == nullptr)
        return;
    auto* dm = host->getDeviceManager();
    juce::PopupMenu m;
    m.setLookAndFeel (&laf_);
    juce::StringArray names;
    if (auto* type = dm->getCurrentDeviceTypeObject())
        names = type->getDeviceNames (true);
    const auto current = dm->getAudioDeviceSetup().inputDeviceName;
    int id = 1;
    for (const auto& n : names)
        m.addItem (id++, n, true, n == current);
    if (names.isEmpty())
        m.addItem (-1, "No input devices found", false);
    m.addSeparator();
    m.addItem (1000, "Audio settings...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceButton_).withMinimumWidth (320),
                     [safe = juce::Component::SafePointer<ReverseBackEditor> (this), names, dm] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         if (r == 1000)
                         {
                             safe->openSettings();
                             return;
                         }
                         if (r >= 1 && r <= names.size())
                         {
                             auto setup = dm->getAudioDeviceSetup();
                             setup.inputDeviceName = names[r - 1];
                             setup.useDefaultInputChannels = true;
                             const auto err = dm->setAudioDeviceSetup (setup, true);
                             if (err.isNotEmpty())
                                 safe->proc_.showBanner (BannerKind::Device, err, true, true);
                         }
                     });
}

// ------------------------------------------------------------------------------------------ keyboard
void ReverseBackEditor::releaseHold()
{
    if (holdKeyDown_)
    {
        holdKeyDown_ = false;
        proc_.actionHoldUp();
    }
    if (holdMouseDown_)
    {
        holdMouseDown_ = false;
        proc_.actionHoldUp();
    }
}

void ReverseBackEditor::globalFocusChanged (juce::Component* focused)
{
    // Losing focus (window deactivated) releases a held recording.
    if (focused == nullptr || ! (focused == this || isParentOf (focused)))
    {
        if (holdKeyDown_ || holdMouseDown_)
            releaseHold();
    }
}

bool ReverseBackEditor::keyPressed (const juce::KeyPress& k)
{
    if (sheets_.isOpen() || ! shortcutsEnabled_)
        return false;
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    // Holding a shortcut key makes the OS repeat it: only the first press may act (otherwise Space would
    // start and stop a recording over and over, discarding the take).
    if (heldShortcutKeys_.count (std::tolower (code)) != 0 && juce::KeyPress::isKeyCurrentlyDown (code))
        return true;
    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::escapeKey || std::tolower (code) == 'r' || std::tolower (code) == 'f'
        || ((mods.isCommandDown() || mods.isCtrlDown()) && (std::tolower (code) == 'o' || std::tolower (code) == 's')))
        heldShortcutKeys_.insert (std::tolower (code));
    if (mods.isCommandDown() || mods.isCtrlDown())
    {
        if (code == 'o' || code == 'O')
        {
            openFileDialog();
            return true;
        }
        if (code == 's' || code == 'S')
        {
            openExport();
            return true;
        }
        return false;
    }
    if (code == juce::KeyPress::spaceKey)
    {
        proc_.actionStartStop();
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        if (proc_.isLoadingFile())
            proc_.cancelLoad();
        else if (proc_.isBusy())
            proc_.actionStop();
        else if (advancedOpen_)
            setAdvancedOpen (false);
        return true;
    }
    if (std::tolower (code) == 'r')
    {
        proc_.actionReplay();
        return true;
    }
    if (std::tolower (code) == 'f')
    {
        toggleDirection();
        return true;
    }
    if (std::tolower (code) == std::tolower (holdKeyCode_))
    {
        if (! holdKeyDown_)   // key repeat must not start more captures
        {
            holdKeyDown_ = true;
            proc_.actionHoldDown();
        }
        return true;
    }
    return false;
}

bool ReverseBackEditor::keyStateChanged (bool)
{
    for (auto it = heldShortcutKeys_.begin(); it != heldShortcutKeys_.end();)
        it = juce::KeyPress::isKeyCurrentlyDown (*it) ? std::next (it) : heldShortcutKeys_.erase (it);
    if (holdKeyDown_ && ! juce::KeyPress::isKeyCurrentlyDown (holdKeyCode_))
        releaseHold();
    return false;
}

// ------------------------------------------------------------------------------------------ drag and drop
bool ReverseBackEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! sheets_.isOpen() && files.size() >= 1;
}

void ReverseBackEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHover_ = true;
    refresh();
}

void ReverseBackEditor::fileDragExit (const juce::StringArray&)
{
    dropHover_ = false;
    refresh();
}

void ReverseBackEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover_ = false;
    if (files.isEmpty())
        return;
    const juce::File f (files[0]);
    if (! (f.hasFileExtension ("wav;aif;aiff;flac")))
    {
        proc_.showBanner (BannerKind::Info, "Unsupported file. ReverseBack opens WAV, AIFF and FLAC.", false, false, 4.0);
        refresh();
        return;
    }
    if (auto* p = proc_.apvts.getParameter (ids::mode))
        p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
    proc_.loadFile (f);
    refresh();
}
}  // namespace rb::ui
