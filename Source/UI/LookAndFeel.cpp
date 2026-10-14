#include "LookAndFeel.h"

namespace rb::ui
{
RbLookAndFeel::RbLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Inter");
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, col::bg0);
    setColour (Label::textColourId, col::text);
    setColour (Label::textWhenEditingColourId, col::text);
    setColour (TextEditor::backgroundColourId, col::bg0);
    setColour (TextEditor::textColourId, col::text);
    setColour (TextEditor::highlightColourId, col::bg3);
    setColour (TextEditor::highlightedTextColourId, col::text);
    setColour (TextEditor::outlineColourId, col::line);
    setColour (TextEditor::focusedOutlineColourId, col::text2);
    setColour (CaretComponent::caretColourId, col::text);
    setColour (ComboBox::backgroundColourId, col::bg2);
    setColour (ComboBox::textColourId, col::text);
    setColour (ComboBox::outlineColourId, col::line);
    setColour (ComboBox::arrowColourId, col::text2);
    setColour (ComboBox::focusedOutlineColourId, col::text);
    setColour (PopupMenu::backgroundColourId, col::bg1);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::bg3);
    setColour (PopupMenu::highlightedTextColourId, col::text);
    setColour (TextButton::buttonColourId, col::bg2);
    setColour (TextButton::buttonOnColourId, col::bg3);
    setColour (TextButton::textColourOffId, col::text);
    setColour (TextButton::textColourOnId, col::text);
    setColour (ToggleButton::textColourId, col::text);
    setColour (ToggleButton::tickColourId, col::text);
    setColour (ToggleButton::tickDisabledColourId, col::text3);
    setColour (Slider::backgroundColourId, col::bg2);
    setColour (Slider::trackColourId, accentFor (Mode::Record).fill);
    setColour (Slider::thumbColourId, col::text);
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, col::bg3);
    setColour (ListBox::backgroundColourId, col::bg1);
    setColour (ListBox::outlineColourId, col::line);
    setColour (ListBox::textColourId, col::text);
    setColour (ScrollBar::thumbColourId, col::bg3);
    setColour (TooltipWindow::backgroundColourId, col::bg3);
    setColour (TooltipWindow::textColourId, col::text);
    setColour (TooltipWindow::outlineColourId, col::line);
    setColour (AlertWindow::backgroundColourId, col::bg1);
    setColour (AlertWindow::textColourId, col::text);
    setColour (AlertWindow::outlineColourId, col::line);
}

void RbLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> b, float radius)
{
    g.setColour (col::text);
    g.drawRoundedRectangle (b.expanded (3.0f), radius + 3.0f, 2.0f);
}

// ------------------------------------------------------------------------------------------ buttons
void RbLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    juce::Colour fill = b.getToggleState() ? col::bg3 : base;
    if (highlighted)
        fill = fill.brighter (0.12f);
    if (down)
        fill = fill.darker (0.12f);
    if (! b.isEnabled())
        fill = fill.withMultipliedAlpha (0.45f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, metric::controlRadius);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);
    if (b.hasKeyboardFocus (true))
        drawFocusRing (g, r, metric::controlRadius);
}

juce::Font RbLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return fonts::medium (16.0f); }

void RbLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float trackW = 40.0f, trackH = 22.0f;
    juce::Rectangle<float> track (r.getX() + 2.0f, r.getCentreY() - trackH * 0.5f, trackW, trackH);
    const bool on = b.getToggleState();
    const juce::Colour accent = b.findColour (juce::Slider::trackColourId);
    g.setColour ((on ? accent : col::bg3).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.fillRoundedRectangle (track, trackH * 0.5f);
    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (track, trackH * 0.5f);
    }
    const float knob = trackH - 6.0f;
    g.setColour (col::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.6f));
    g.fillEllipse (on ? track.getRight() - knob - 3.0f : track.getX() + 3.0f, track.getY() + 3.0f, knob, knob);
    g.setFont (fonts::regular (15.0f));
    g.setColour ((b.isEnabled() ? col::text : col::text3));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (trackW + 12.0f), juce::Justification::centredLeft, true);
    if (b.hasKeyboardFocus (true))
        drawFocusRing (g, track, trackH * 0.5f);
}

// ------------------------------------------------------------------------------------------ combo box
void RbLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    juce::Colour fill = box.findColour (juce::ComboBox::backgroundColourId);
    if (box.isMouseOver (true))
        fill = fill.brighter (0.12f);
    if (down)
        fill = fill.darker (0.1f);
    g.setColour (fill.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, metric::controlRadius);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);
    drawIcon (g, Icon::Chevron, juce::Rectangle<float> (static_cast<float> (w) - 30.0f, 0.0f, 22.0f, static_cast<float> (h)),
              col::text2.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f), 1.8f);
    if (box.hasKeyboardFocus (true))
        drawFocusRing (g, r, metric::controlRadius);
}

juce::Font RbLookAndFeel::getComboBoxFont (juce::ComboBox&) { return fonts::regular (16.0f); }

void RbLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, box.getWidth() - 44, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ------------------------------------------------------------------------------------------ sliders
void RbLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    // Number field: a pill whose tint shows the value's position inside its range.
    auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    const float alpha = s.isEnabled() ? 1.0f : 0.45f;
    g.setColour (col::bg2.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, metric::controlRadius);
    const float fillW = juce::jlimit (0.0f, r.getWidth(), pos - r.getX());
    if (fillW > 2.0f)
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, metric::controlRadius);
        g.reduceClipRegion (clip);
        g.setColour (s.findColour (juce::Slider::trackColourId).withAlpha (0.38f * alpha));
        g.fillRect (r.withWidth (fillW));
    }
    g.setColour ((s.isMouseOverOrDragging() ? col::text3 : col::line).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);
    if (s.hasKeyboardFocus (true))
        drawFocusRing (g, r, metric::controlRadius);
}

juce::Label* RbLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (fonts::medium (17.0f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, col::text);
    l->setColour (juce::Label::textWhenEditingColourId, col::text);
    l->setColour (juce::Label::backgroundWhenEditingColourId, col::bg0);
    l->setColour (juce::Label::outlineWhenEditingColourId, col::text2);
    l->setMinimumHorizontalScale (0.9f);
    return l;
}

juce::Font RbLookAndFeel::getLabelFont (juce::Label& l)
{
    const auto f = l.getFont();
    return f.getHeight() > 0.0f ? f : fonts::regular (16.0f);
}

// ------------------------------------------------------------------------------------------ popup menus
void RbLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h));
    g.setColour (col::bg1);
    g.fillRoundedRectangle (r.reduced (0.5f), 10.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

juce::Font RbLookAndFeel::getPopupMenuFont() { return fonts::regular (16.0f); }

void RbLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator)
    {
        w = 50;
        h = 10;
        return;
    }
    w = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 64;
    h = 36;
}

void RbLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                       bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                       const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (12, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (col::bg3);
        g.fillRoundedRectangle (r, 8.0f);
    }
    const juce::Colour tc = isActive ? col::text : col::text3;
    g.setColour (tc);
    g.setFont (getPopupMenuFont());
    auto textArea = area.reduced (14, 0);
    if (isTicked)
        drawIcon (g, Icon::Check, juce::Rectangle<float> (static_cast<float> (area.getX()) + 8.0f, static_cast<float> (area.getY()), 20.0f, static_cast<float> (area.getHeight())), col::ok, 2.0f);
    g.drawText (text, textArea.withTrimmedLeft (isTicked ? 18 : 0), juce::Justification::centredLeft, true);
    if (shortcut.isNotEmpty())
    {
        g.setColour (col::text3);
        g.setFont (fonts::regular (14.0f));
        g.drawText (shortcut, textArea, juce::Justification::centredRight, true);
    }
    if (hasSubMenu)
        drawIcon (g, Icon::Chevron, juce::Rectangle<float> (static_cast<float> (area.getRight()) - 24.0f, static_cast<float> (area.getY()), 16.0f, static_cast<float> (area.getHeight())), tc, 1.6f);
}

// ------------------------------------------------------------------------------------------ tooltip, scrollbar, editors
juce::Rectangle<int> RbLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = fonts::regular (14.0f);
    juce::AttributedString as;
    as.append (tip, font, col::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (as, 320.0f);
    const int w = static_cast<int> (std::ceil (layout.getWidth())) + 24;
    const int h = static_cast<int> (std::ceil (layout.getHeight())) + 16;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - w - 12 : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - h - 6 : screenPos.y + 22, w, h)
        .constrainedWithin (parentArea);
}

void RbLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h));
    g.setColour (col::bg3);
    g.fillRoundedRectangle (r.reduced (0.5f), 8.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    juce::AttributedString as;
    as.append (text, fonts::regular (14.0f), col::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (as, 320.0f);
    layout.draw (g, r.reduced (12.0f, 8.0f));
}

void RbLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int start, int size, bool over, bool down)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> (static_cast<float> (x) + 2.0f, static_cast<float> (y + start), static_cast<float> (w) - 4.0f, static_cast<float> (size))
                                            : juce::Rectangle<float> (static_cast<float> (x + start), static_cast<float> (y) + 2.0f, static_cast<float> (size), static_cast<float> (h) - 4.0f);
    g.setColour ((down ? col::text3 : over ? col::text3.withAlpha (0.8f) : col::bg3));
    g.fillRoundedRectangle (thumb, 4.0f);
}

void RbLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), metric::controlRadius);
}

void RbLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? col::text2 : col::line);
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (w) - 1.0f, static_cast<float> (h) - 1.0f, metric::controlRadius, 1.0f);
}
}  // namespace rb::ui
