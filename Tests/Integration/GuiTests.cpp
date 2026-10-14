// A16, A08 (GUI), P12: the real editor driven through its components, plus the screenshot harness.
#include "ProcHarness.h"
#include "ReverseBackEditor.h"
#include "Sheets.h"
#include "Theme.h"
#include "TestUtil.h"

using namespace rbt;
using RS = rb::RecordTransport::State;
using LS = rb::LiveTransport::State;

namespace
{
void clickAndPump (juce::Button& b)
{
    b.triggerClick();   // delivered asynchronously through the message queue
    pump (40);
}

struct GuiRig
{
    GuiRig() : h (48000.0, 512)
    {
        ed = std::make_unique<rb::ui::ReverseBackEditor> (*h.p);
        ed->setBounds (0, 0, 960, 700);
        ed->refreshNow();
    }

    template <class T>
    T& ctl (const char* id)
    {
        auto* c = dynamic_cast<T*> (ed->findControl (id));
        if (c == nullptr)
        {
            fail (__FILE__, __LINE__, std::string ("control not found: ") + id);
            throw TestAbort();
        }
        return *c;
    }

    void settle (juce::int64 frames = 1024)
    {
        h.run (frames);
        ed->refreshNow();
    }

    ProcHarness h;
    std::unique_ptr<rb::ui::ReverseBackEditor> ed;
};
}  // namespace

RB_TEST (A16_every_main_and_advanced_control_changes_the_intended_property)
{
    GuiRig g;
    using namespace rb::ui;
    auto& h = g.h;

    g.ctl<SegmentedControl> ("modeTabs").setSelected (1, true);
    CHECK_EQ (juce::roundToInt (h.get ("mode")), 1);
    g.ctl<SegmentedControl> ("modeTabs").setSelected (2, true);
    CHECK_EQ (juce::roundToInt (h.get ("mode")), 2);
    g.ctl<SegmentedControl> ("modeTabs").setSelected (0, true);
    g.settle();

    g.ctl<NumberField> ("capture").setValue (7.5, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("capture"), 7.5, 1e-3);
    g.ctl<NumberField> ("wait").setValue (3.25, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("wait"), 3.25, 1e-3);
    g.ctl<NumberField> ("chunk").setValue (0.75, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("chunk"), 0.75, 1e-3);
    g.ctl<NumberField> ("delay").setValue (1.5, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("delay"), 1.5, 1e-3);
    g.ctl<NumberField> ("volume").setValue (-30.0, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("outVol"), -30.0, 1e-2);

    g.ctl<juce::ToggleButton> ("repeat").setToggleState (true, juce::sendNotificationSync);
    CHECK (h.get ("repeat") > 0.5);
    g.ctl<SegmentedControl> ("direction").setSelected (0, true);
    CHECK_EQ (juce::roundToInt (h.get ("direction")), 0);
    g.ctl<SegmentedControl> ("loop").setSelected (2, true);
    CHECK_EQ (juce::roundToInt (h.get ("loop")), 2);
    g.settle (2048);
    CHECK (h.get ("repeat") < 0.5);   // choosing a loop pattern turns Repeat Session off (C12)

    // advanced drawer
    g.ed->setAdvancedOpen (true);
    g.settle();
    CHECK (g.ctl<juce::Component> ("drawer").isVisible());
    auto& tabs = g.ctl<SegmentedControl> ("drawer.tabs");
    tabs.setSelected (0, true);
    g.settle();
    g.ctl<SegmentedControl> ("adv.countdown").setSelected (2, true);
    CHECK_EQ (juce::roundToInt (h.get ("countdown")), 2);
    g.ctl<juce::ToggleButton> ("adv.autoStart").setToggleState (true, juce::sendNotificationSync);
    CHECK (h.get ("autoStart") > 0.5);
    g.ctl<NumberField> ("adv.threshold").setValue (-30.0, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("threshold"), -30.0, 1e-2);
    g.ctl<NumberField> ("adv.repeatGap").setValue (1.25, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("tailGap"), 1.25, 1e-2);
    tabs.setSelected (1, true);
    g.settle();
    clickAndPump (g.ctl<ActionButton> ("adv.speed.3"));   // 1.5x
    CHECK_NEAR (h.get ("speed"), 1.5, 1e-3);
    g.ctl<NumberField> ("adv.speed").setValue (0.6, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("speed"), 0.6, 1e-2);
    g.ctl<NumberField> ("adv.edgeFade").setValue (6.0, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("edgeFade"), 6.0, 1e-2);
    g.ctl<juce::ToggleButton> ("adv.exact").setToggleState (true, juce::sendNotificationSync);
    CHECK (h.get ("exact") > 0.5);
    tabs.setSelected (2, true);
    g.settle();
    g.ctl<NumberField> ("adv.inGain").setValue (9.0, juce::sendNotificationSync);
    CHECK_NEAR (h.get ("inGain"), 9.0, 1e-2);
    CHECK (! g.ctl<NumberField> ("adv.monitor").isEnabled());   // monitor is standalone only
    g.ed->setAdvancedOpen (false);
    CHECK (! g.ctl<juce::Component> ("drawer").isVisible());

    // transport buttons reach the engine
    h.set ("loop", 0);
    h.set ("speed", 1.0);
    h.set ("exact", 1);
    h.set ("capture", 0.5);
    h.set ("wait", 0);
    h.set ("countdown", 0);
    h.set ("autoStart", 0);
    g.settle();
    auto& primary = g.ctl<ActionButton> ("primary");
    CHECK_EQ (primary.getButtonText(), juce::String ("Record & Reverse"));
    clickAndPump (primary);
    g.settle (2048);
    CHECK (h.recordState (RS::Recording));
    CHECK_EQ (primary.getButtonText(), juce::String ("Stop"));       // one unmistakable Stop while busy
    CHECK (! g.ctl<NumberField> ("capture").isEnabled());            // disabled controls say why
    CHECK (g.ctl<NumberField> ("capture").getTooltip().contains ("next recording"));
    CHECK_EQ (g.ctl<ActionButton> ("hold").getButtonText(), juce::String ("Finish Early"));
    clickAndPump (primary);
    g.settle (2048);
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->currentTake() == nullptr);   // Stop discards the unfinished capture

    clickAndPump (primary);
    g.settle (52000);   // 0.5 s capture + 0.5 s playback
    CHECK (h.p->currentTake() != nullptr);
    CHECK (g.ctl<ActionButton> ("replay").isEnabled());
    CHECK (g.ctl<ActionButton> ("save").isEnabled());
    clickAndPump (g.ctl<ActionButton> ("replay"));
    g.settle (512);
    CHECK (h.recordState (RS::Playing));

    // hold-to-record through the button's press/release callbacks
    h.p->actionStop();
    g.settle (2048);
    auto& hold = g.ctl<ActionButton> ("hold");
    CHECK (hold.onPressedChanged != nullptr);
    hold.onPressedChanged (true);
    g.settle (8192);
    CHECK (h.recordState (RS::Recording));
    hold.onPressedChanged (false);
    g.settle (2048);
    CHECK_EQ (h.p->currentTake()->id, 2u);

    // Live
    g.ctl<SegmentedControl> ("modeTabs").setSelected (1, true);
    g.settle();
    CHECK_EQ (g.ctl<ActionButton> ("primary").getButtonText(), juce::String ("Start Live"));
    CHECK (! g.ctl<SegmentedControl> ("direction").isEnabled());     // Live always reverses
    CHECK (g.ctl<juce::Label> ("firstSound").getText().contains ("First sound after"));
    clickAndPump (g.ctl<ActionButton> ("primary"));
    g.settle (4096);
    CHECK (! h.liveState (LS::Ready));
    CHECK (! g.ctl<NumberField> ("chunk").isEnabled());
    CHECK (g.ctl<NumberField> ("chunk").getTooltip().contains ("Stop Live"));
    clickAndPump (g.ctl<ActionButton> ("primary"));
    g.settle (2048);
    CHECK (h.liveState (LS::Ready));

    // File
    g.ctl<SegmentedControl> ("modeTabs").setSelected (2, true);
    g.settle();
    CHECK (! g.ctl<ActionButton> ("primary").isEnabled());           // nothing loaded yet
    CHECK (g.ctl<ActionButton> ("primary").getTooltip().contains ("Open a file first"));
}

RB_TEST (typing_and_repeat_keys_never_start_or_multiply_recordings)
{
    GuiRig g;
    auto& h = g.h;
    h.set ("capture", 5.0);
    g.settle();

    // Typing into a number field: the focused text editor consumes shortcut letters, so they never reach the editor.
    auto& cap = g.ctl<rb::ui::NumberField> ("capture");
    cap.showTextBox();
    pump (100);
    juce::TextEditor* te = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        if (te == nullptr)
            te = dynamic_cast<juce::TextEditor*> (&c);
        for (auto* child : c.getChildren())
            find (*child);
    };
    find (cap);
    CHECK (te != nullptr);
    if (te != nullptr)
    {
        te->setText ("", false);
        for (const char c : { 'h', 'r', 'f', ' ' })
            CHECK (te->keyPressed (juce::KeyPress (static_cast<int> (c), juce::ModifierKeys(), static_cast<juce::juce_wchar> (c))));
        CHECK (te->getText().contains ("hrf"));   // the characters were typed into the field
        te->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    }
    g.settle (4096);
    CHECK (h.recordState (RS::Ready));
    CHECK (h.p->currentTake() == nullptr);

    // with a sheet open the global shortcuts are inert
    g.ed->openSettings();
    CHECK (! g.ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    g.ed->sheetHost().close();

    // Hold: key repeat does not start extra captures, release closes a valid take
    h.set ("wait", 0);
    CHECK (g.ed->keyPressed (juce::KeyPress ('h')));
    CHECK (g.ed->keyPressed (juce::KeyPress ('h')));   // auto-repeat
    CHECK (g.ed->keyPressed (juce::KeyPress ('h')));
    g.settle (8192);
    CHECK (h.recordState (RS::Recording));
    g.ed->keyStateChanged (false);   // key is no longer down
    g.settle (4096);
    CHECK (! h.recordState (RS::Recording));
    CHECK_EQ (h.p->currentTake()->id, 1u);
    CHECK_EQ (h.p->currentTake()->clip->frameCount(), rb::Frame { 8192 });   // exactly one capture of the held duration; key repeat added none
    g.settle (20000);
    CHECK (h.recordState (RS::Ready));

    // Escape stops, F flips direction, R replays
    const auto before = h.get ("direction");
    CHECK (g.ed->keyPressed (juce::KeyPress ('f')));
    h.run (512);
    CHECK (h.get ("direction") != before);
    CHECK (g.ed->keyPressed (juce::KeyPress ('r')));
    g.settle (2048);
    CHECK (h.recordState (RS::Playing));
    CHECK (g.ed->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    g.settle (4096);
    CHECK (h.recordState (RS::Ready));
}

RB_TEST (P12_editor_opens_resizes_and_closes_repeatedly)
{
    ProcHarness h;
    for (int i = 0; i < 25; ++i)
    {
        auto ed = std::make_unique<rb::ui::ReverseBackEditor> (*h.p);
        ed->setBounds (0, 0, 960, 700);
        ed->setSize (820, 620);
        ed->refreshNow();
        ed->setSize (1920, 1280);
        ed->refreshNow();
        ed->setAdvancedOpen (i % 2 == 0);
        ed->setSize (960, 700);
        h.run (2048);
        ed->refreshNow();
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img.isValid());
    }
    CHECK (true);
}

// Renders reference screenshots when RB_SCREENSHOT_DIR is set (used for docs/screenshots).
RB_TEST (screenshots_of_every_mode_and_sheet)
{
    const auto dirName = juce::SystemStats::getEnvironmentVariable ("RB_SCREENSHOT_DIR", {});
    if (dirName.isEmpty())
        return;
    const juce::File dir (dirName);
    dir.createDirectory();
    GuiRig g;
    using namespace rb::ui;
    auto& h = g.h;
    h.set ("outVol", -12);
    h.input = [] (juce::int64 i)
    {
        const double env = std::abs (std::sin (0.00009 * static_cast<double> (i))) * (0.3 + 0.7 * std::abs (std::sin (0.0007 * static_cast<double> (i))));
        return static_cast<float> (0.7 * env * std::sin (0.07 * static_cast<double> (i)) * std::sin (0.0123 * static_cast<double> (i) + 1.0));
    };
    auto shot = [&] (const char* name)
    {
        g.ed->refreshNow();
        const auto img = g.ed->createComponentSnapshot (g.ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        std::unique_ptr<juce::FileOutputStream> out (dir.getChildFile (juce::String (name) + ".png").createOutputStream());
        if (out != nullptr)
        {
            out->setPosition (0);
            out->truncate();
            png.writeImageToStream (img, *out);
        }
    };

    h.set ("capture", 5.0);
    h.set ("wait", 2.0);
    shot ("01-record-ready");
    h.p->actionStart();
    h.run (48000 * 2 + 20000);
    shot ("02-record-recording");
    h.run (48000 * 3 + 24000);
    shot ("03-record-waiting");
    h.run (48000 + 1000);
    shot ("04-record-playing-backwards");
    h.run (48000 * 6);
    shot ("05-record-take");
    g.ed->setAdvancedOpen (true);
    shot ("06-record-advanced");
    g.ed->setAdvancedOpen (false);

    g.ctl<SegmentedControl> ("modeTabs").setSelected (1, true);
    h.set ("chunk", 0.5);
    h.set ("delay", 2.0);
    shot ("07-live-ready");
    h.p->actionStart();
    h.run (48000);
    shot ("08-live-filling");
    h.run (48000 * 4);
    shot ("09-live-running");
    h.p->actionFreezeResume();
    h.run (48000 * 2);
    shot ("10-live-frozen");
    h.p->actionStop();
    h.run (2048);

    TempDir t;
    juce::WavAudioFormat wav;
    CHECK (writeFixture (t.file ("interview.wav"), wav, 48000.0, 2, 24, 48000 * 31));
    g.ctl<SegmentedControl> ("modeTabs").setSelected (2, true);
    shot ("11-file-empty");
    h.p->loadFile (t.file ("interview.wav"));
    pumpUntil ([&] { return h.p->fileAsset() != nullptr; });
    h.p->setFileSelection ({ 48000 * 8, 48000 * 21 });
    h.run (512);
    shot ("12-file-selection");
    h.p->playFile (true);
    h.run (48000 * 4);
    shot ("13-file-playing");
    h.p->actionStop();
    h.run (4096);
    g.ed->openSettings (1);
    shot ("14-settings-interface");
    g.ed->sheetHost().close();
    g.ed->openExport();
    shot ("15-export-sheet");
    g.ed->sheetHost().close();
    h.p->showBanner (rb::BannerKind::Device, "No microphone is available. Choose an input device in Settings, or use Reverse File.", true, true);
    g.ctl<SegmentedControl> ("modeTabs").setSelected (0, true);
    shot ("16-error-banner");
    CHECK (true);
}


RB_TEST (audit_export_sheet_decision_buttons_survive_being_clicked)
{
    ProcHarness h (48000.0, 512);
    rb::ExportSource src;
    src.source = rbt::makeSineClip (48000.0, 24000, 440.0, 1.6);
    src.selection = { 0, 24000 };
    src.channels = 1;
    src.suggestedName = "test";
    for (int which = 0; which < 2; ++which)
    {
        rb::ui::ExportSheet sheet (*h.p, src);
        sheet.setBounds (0, 0, 560, 440);
        rb::ExportOutcome o;
        o.error = which == 0 ? rb::ExportError::WouldClip : rb::ExportError::DestinationExists;
        o.message = "A question for the user.";
        sheet.simulateOutcomeForTest (o);
        pump (20);
        CHECK (sheet.getNumChildComponents() > 0);
        // the decision buttons are added last; the layout gives the question the whole card
        auto* last = dynamic_cast<juce::Button*> (sheet.getChildComponent (sheet.getNumChildComponents() - 1));
        CHECK (last != nullptr);
        if (last != nullptr)
        {
            CHECK (last->isShowing() || last->getWidth() > 0);
            clickAndPump (*last);   // used to destroy the button (and its own closure) while it was running
            pump (200);
        }
    }
}

RB_TEST (audit_options_row_fits_at_the_minimum_window_size_in_every_mode)
{
    GuiRig g;
    using namespace rb::ui;
    g.ed->setBounds (0, 0, 820, 620);
    for (int mode = 0; mode < 3; ++mode)
    {
        g.h.set ("mode", mode);
        g.settle();
        g.ed->setBounds (0, 0, 820, 620);   // layout again for the mode
        auto& volume = g.ctl<NumberField> ("volume");
        auto& advanced = g.ctl<juce::Component> ("advancedToggle");
        auto& direction = g.ctl<SegmentedControl> ("direction");
        auto& loop = g.ctl<SegmentedControl> ("loop");
        CHECK (volume.getWidth() >= 80);             // used to be 0 px wide
        CHECK (advanced.getWidth() >= 100);
        CHECK (g.ed->getLocalBounds().contains (volume.getBounds()));
        CHECK (g.ed->getLocalBounds().contains (advanced.getBounds()));
        CHECK (! direction.getBounds().intersects (loop.getBounds()));
        CHECK (! loop.getBounds().intersects (volume.getBounds()));
        CHECK (! volume.getBounds().intersects (advanced.getBounds()));
        CHECK (direction.getWidth() >= 190 && loop.getWidth() >= 220);   // still readable
    }
}

RB_TEST (audit_sheets_keep_keyboard_focus_inside_the_sheet)
{
    juce::Component parent;
    parent.setBounds (0, 0, 900, 700);
    juce::TextButton under ("under the scrim");
    under.setWantsKeyboardFocus (true);
    parent.addAndMakeVisible (under);
    rb::ui::SheetHost host;
    parent.addAndMakeVisible (host);
    host.setBounds (parent.getLocalBounds());
    host.show (std::make_unique<rb::ui::PromptSheet> ("Save preset", "Name", "My preset", "Save", [] (const juce::String&) {}));

    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        if (c.getWantsKeyboardFocus() && &c != &host)
            CHECK (c.findFocusContainer() == &host);   // Tab can never reach "under the scrim"
        for (auto* child : c.getChildren())
            visit (*child);
    };
    visit (host);
    CHECK (host.findFocusContainer() != &host);
}

RB_TEST (audit_time_text_helpers_round_and_reject_garbage)
{
    using namespace rb::ui;
    CHECK_EQ (fmtClock (0.99996, true), juce::String ("0:01.000"));    // used to print 0:00.000
    CHECK_EQ (fmtClock (59.9996, true), juce::String ("1:00.000"));
    CHECK_EQ (fmtClock (61.25, true), juce::String ("1:01.250"));
    CHECK_EQ (fmtClock (61.6, false), juce::String ("1:02"));
    CHECK_NEAR (parseSeconds ("abc", 3.5), 3.5, 1e-12);                 // junk keeps the old value instead of 0
    CHECK_NEAR (parseSeconds ("", 3.5), 3.5, 1e-12);
    CHECK_NEAR (parseSeconds ("1:30", 0.0), 90.0, 1e-9);
    CHECK_NEAR (parseSeconds ("250ms", 0.0), 0.25, 1e-9);
}

RB_TEST (audit_arrow_keys_step_the_length_fields_by_the_specified_amounts)
{
    GuiRig g;
    using namespace rb::ui;
    auto& capture = g.ctl<NumberField> ("capture");
    capture.setValue (5.0, juce::sendNotificationSync);
    capture.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    CHECK_NEAR (capture.getValue(), 5.05, 1e-6);                        // V1 4.2: 0.05 s steps
    capture.keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
    CHECK_NEAR (capture.getValue(), 4.55, 1e-6);                        // Shift: x10
    auto& chunk = g.ctl<NumberField> ("chunk");
    chunk.setValue (0.5, juce::sendNotificationSync);
    chunk.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    CHECK_NEAR (chunk.getValue(), 0.51, 1e-6);                          // 0.01 s
}

RB_TEST (audit_captions_stay_at_the_default_width_and_go_only_when_the_window_is_narrow)
{
    GuiRig g;   // 960 px: the designed layout keeps the Direction / Loop captions
    using namespace rb::ui;
    g.ed->setBounds (0, 0, 960, 700);
    CHECK (g.ctl<juce::Label> ("directionLabel").isVisible());
    g.ed->setBounds (0, 0, 820, 620);
    CHECK (! g.ctl<juce::Label> ("directionLabel").isVisible());
}
