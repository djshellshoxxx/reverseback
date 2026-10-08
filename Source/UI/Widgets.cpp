#include "Widgets.h"

namespace rb::ui
{
// =============================================================================== ActionButton
ActionButton::ActionButton (const juce::String& name) : juce::Button (name)
{
    setButtonText ({});   // juce::Button copies its name into the text; labels are set explicitly
    setTitle (name);      // screen readers: icon-only buttons are announced by their name
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setRepaintsOnMouseActivity (false);
}

void ActionButton::setLabel (const juce::String& s)
{
    if (getButtonText() != s)
    {
        setButtonText (s);
        if (s.isNotEmpty())
            setTitle (s);   // the visible label is the better spoken name
        repaint();
    }
}

bool ActionButton::keyPressed (const juce::KeyPress& k)
{
    // Space belongs to the global Start/Stop shortcut, so a focused button must not swallow it.
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
        return false;
    return juce::Button::keyPressed (k);
}

void ActionButton::buttonStateChanged()
{
    const bool down = isDown();
    if (down != wasDown_)
    {
        wasDown_ = down;
        if (onPressedChanged)
            onPressedChanged (down);
    }
    const float target = (isOver() || isDown()) ? 1.0f : 0.0f;
    if (! motionEnabled())
    {
        hover_ = target;
        repaint();
    }
    else if (hover_ != target)
    {
        startTimerHz (60);
    }
}

void ActionButton::timerCallback()
{
    const float target = (isOver() || isDown()) ? 1.0f : 0.0f;
    const float step = 1.0f / 7.0f;   // ~120 ms at 60 Hz
    hover_ = hover_ < target ? std::min (target, hover_ + step) : std::max (target, hover_ - step);
    repaint();
    if (hover_ == target)
        stopTimer();
}

void ActionButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = style_ == Style::Primary ? metric::primaryRadius : metric::controlRadius;
    juce::Colour fill = col::bg2, border = juce::Colours::transparentBlack, textCol = col::text;
    const bool on = isEnabled();

    switch (style_)
    {
        case Style::Primary:
            fill = accent_.fill.brighter (0.12f * hover_);
            break;
        case Style::Secondary:
            fill = col::bg2.interpolatedWith (col::bg3, hover_);
            border = col::line;
            break;
        case Style::Danger:
            fill = juce::Colour (0xffc42b3f).interpolatedWith (juce::Colour (0xffd63a4f), hover_);
            break;
        case Style::Ghost:
        case Style::Icon:
            fill = col::bg2.withAlpha (0.85f * hover_);
            textCol = col::text2.interpolatedWith (col::text, hover_);
            break;
        case Style::Tab:
            if (selected_)
                fill = accent_.fill.brighter (0.08f * hover_);
            else
            {
                fill = col::bg2.withAlpha (0.8f * hover_);
                textCol = col::text2.interpolatedWith (col::text, hover_);
            }
            break;
    }
    if (down)
        fill = fill.darker (0.14f);
    if (! on)
    {
        fill = fill.withMultipliedAlpha (0.45f);
        textCol = textCol.withMultipliedAlpha (0.5f);
    }

    if (style_ == Style::Primary && on)
    {
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.12f), r.getX(), r.getY(), fill.darker (0.08f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
    }
    else
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
    }
    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    const juce::String label = getButtonText();
    const float fontSize = style_ == Style::Primary ? 21.0f : (style_ == Style::Tab ? 18.0f : (style_ == Style::Icon ? 16.0f : 17.0f));
    const juce::Font font = style_ == Style::Primary ? fonts::semibold (fontSize) : fonts::medium (fontSize);
    const float iconSize = style_ == Style::Primary ? 24.0f : (style_ == Style::Icon ? 22.0f : 20.0f);
    const float gap = 10.0f;
    const float textW = label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, label);
    const float iconW = icon_ == Icon::None ? 0.0f : iconSize;
    const float total = textW + iconW + (textW > 0.0f && iconW > 0.0f ? gap : 0.0f);
    float x = r.getCentreX() - total * 0.5f;

    auto drawLabelText = [&]
    {
        if (textW > 0.0f)
        {
            g.setColour (textCol);
            g.setFont (font);
            g.drawText (label, juce::Rectangle<float> (x, r.getY(), textW + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
            x += textW + gap;
        }
    };
    auto drawIconGlyph = [&]
    {
        if (iconW > 0.0f)
        {
            drawIcon (g, icon_, juce::Rectangle<float> (x, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize), textCol, 1.9f);
            x += iconW + gap;
        }
    };
    if (iconTrailing_)
    {
        drawLabelText();
        drawIconGlyph();
    }
    else
    {
        drawIconGlyph();
        drawLabelText();
    }
    if (hasKeyboardFocus (true))
        RbLookAndFeel::drawFocusRing (g, r, radius);
}

// =============================================================================== SegmentedControl
void SegmentedControl::setItems (std::vector<Item> items)
{
    items_ = std::move (items);
    selected_ = juce::jlimit (0, std::max (0, static_cast<int> (items_.size()) - 1), selected_);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    repaint();
}

void SegmentedControl::setItemEnabled (int index, bool enabled, const juce::String& tip)
{
    if (index >= 0 && index < static_cast<int> (items_.size()))
    {
        items_[static_cast<std::size_t> (index)].enabled = enabled;
        items_[static_cast<std::size_t> (index)].tip = tip;
        repaint();
    }
}

void SegmentedControl::setSelected (int index, bool notify)
{
    index = juce::jlimit (0, std::max (0, static_cast<int> (items_.size()) - 1), index);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
    if (notify && onChange)
        onChange (selected_);
}

juce::Rectangle<float> SegmentedControl::segment (int i) const
{
    const float w = static_cast<float> (getWidth()) / static_cast<float> (std::max<std::size_t> (1, items_.size()));
    return { w * static_cast<float> (i), 0.0f, w, static_cast<float> (getHeight()) };
}

int SegmentedControl::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < static_cast<int> (items_.size()); ++i)
        if (segment (i).contains (p.toFloat()))
            return i;
    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::bg2.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, metric::controlRadius);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);

    for (int i = 0; i < static_cast<int> (items_.size()); ++i)
    {
        const auto& it = items_[static_cast<std::size_t> (i)];
        const auto s = segment (i).reduced (3.0f);
        const bool on = i == selected_;
        const bool enabled = isEnabled() && it.enabled;
        if (on)
        {
            g.setColour (accent_.withMultipliedAlpha (enabled ? 1.0f : 0.45f));
            g.fillRoundedRectangle (s, metric::controlRadius - 3.0f);
        }
        const juce::Colour tc = (on ? col::text : col::text2).withMultipliedAlpha (enabled ? 1.0f : 0.45f);
        const juce::Font f = fonts::medium (fontSize_);
        const float tw = juce::GlyphArrangement::getStringWidth (f, it.label);
        const float iw = it.icon == Icon::None ? 0.0f : 18.0f;
        float x = s.getCentreX() - (tw + iw + (iw > 0 ? 8.0f : 0.0f)) * 0.5f;
        if (iw > 0.0f)
        {
            drawIcon (g, it.icon, { x, s.getCentreY() - 9.0f, 18.0f, 18.0f }, tc, 1.7f);
            x += iw + 8.0f;
        }
        g.setColour (tc);
        g.setFont (f);
        g.drawText (it.label, juce::Rectangle<float> (x, s.getY(), tw + 4.0f, s.getHeight()), juce::Justification::centredLeft, false);
    }
    if (hasKeyboardFocus (true))
        RbLookAndFeel::drawFocusRing (g, r, metric::controlRadius);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    setTooltip (i >= 0 ? items_[static_cast<std::size_t> (i)].tip : juce::String());
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i >= 0 && isEnabled() && items_[static_cast<std::size_t> (i)].enabled)
        setSelected (i, true);
}

bool SegmentedControl::keyPressed (const juce::KeyPress& k)
{
    if (! isEnabled())
        return false;
    const int dir = k == juce::KeyPress::leftKey ? -1 : (k == juce::KeyPress::rightKey ? 1 : 0);
    if (dir == 0)
        return false;
    for (int i = selected_ + dir; i >= 0 && i < static_cast<int> (items_.size()); i += dir)
        if (items_[static_cast<std::size_t> (i)].enabled)
        {
            setSelected (i, true);
            return true;
        }
    return true;
}

// =============================================================================== NumberField
NumberField::NumberField (const juce::String& name) : juce::Slider (name)
{
    setTitle (name);   // JUCE's slider accessibility handler does not fall back to the component name
    setSliderStyle (juce::Slider::LinearBar);
    setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 20);
    setTextBoxIsEditable (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setPopupDisplayEnabled (false, false, nullptr);
    setScrollWheelEnabled (true);
}

void NumberField::configure (Kind kind, int decimals)
{
    kind_ = kind;
    decimals_ = decimals;
    textFromValueFunction = [this] (double v)
    {
        switch (kind_)
        {
            case Kind::Seconds: return fmtSeconds (v, decimals_);
            case Kind::Millis: return fmtMillis (v);
            case Kind::Ms: return juce::String (v, decimals_) + " ms";
            case Kind::Db: return fmtDb (v, decimals_);
            case Kind::Dbfs: return juce::String (v, decimals_) + " dBFS";
            case Kind::Speed: return juce::String (v, 2) + "x";
            case Kind::Percent: return juce::String (juce::roundToInt (v)) + " %";
            case Kind::Plain: return juce::String (v, decimals_);
        }
        return juce::String (v);
    };
    valueFromTextFunction = [this] (const juce::String& t)
    {
        const double fallback = getValue();
        const juce::String s = t.trim().toLowerCase();
        if (s == "-inf" || s == "-infinity")
            return getMinimum();
        switch (kind_)
        {
            case Kind::Seconds: return parseSeconds (s, fallback);
            case Kind::Millis:
            {
                const double v = parseSeconds (s, fallback);
                const bool hasUnit = s.endsWithChar ('s');
                // A bare number >= 10 is milliseconds ("500"), a smaller one is seconds ("0.5").
                return (! hasUnit && v >= 10.0) ? v * 0.001 : v;
            }
            default: break;
        }
        const juce::String digits = s.retainCharacters ("0123456789.-+");
        return digits.isEmpty() ? fallback : digits.getDoubleValue();
    };
    updateText();
}

bool NumberField::keyPressed (const juce::KeyPress& k)
{
    const double base = arrowStep_ > 0.0 ? arrowStep_ : std::max (getInterval(), (getMaximum() - getMinimum()) * 0.002);
    const double step = base * (k.getModifiers().isShiftDown() ? 10.0 : 1.0);
    const int code = k.getKeyCode();   // not operator==: that also compares modifiers, which broke Shift+arrow
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
        setValue (getValue() + step, juce::sendNotificationSync);
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
        setValue (getValue() - step, juce::sendNotificationSync);
    else if (code == juce::KeyPress::returnKey)
        showTextBox();
    else
        return false;
    return true;
}

// =============================================================================== LevelMeter
namespace
{
float dbFromLinear (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : -120.0f; }
}

void LevelMeter::update (float peak, std::uint32_t overloadCount)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float db = dbFromLinear (peak);
    if (db > levelDb_)
        levelDb_ = db;
    else
        levelDb_ = std::max (db, levelDb_ - 1.2f);   // ~36 dB/s fall at 30 Hz
    if (db >= holdDb_ || now > holdUntil_)
    {
        holdDb_ = db;
        holdUntil_ = now + 1200.0;
    }
    if (init_ && overloadCount != lastOverload_)
        overUntil_ = now + 1500.0;
    lastOverload_ = overloadCount;
    init_ = true;
    repaint();
}

juce::String LevelMeter::summary() const
{
    return overActive() ? "Input level: overload" : "Input level " + juce::String (juce::roundToInt (levelDb_)) + " dB";
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto bar = r.withTrimmedRight (overActive() ? 56.0f : 0.0f).withSizeKeepingCentre (r.getWidth() - (overActive() ? 56.0f : 0.0f), 14.0f)
                         .withX (r.getX());
    g.setColour (col::bg2);
    g.fillRoundedRectangle (bar, 7.0f);
    auto frac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float w = bar.getWidth() * frac (levelDb_);
    if (w > 1.0f)
    {
        juce::ColourGradient grad (col::ok, bar.getX(), 0.0f, col::rec, bar.getRight(), 0.0f, false);
        grad.addColour (static_cast<double> (frac (-12.0f)), col::ok);
        grad.addColour (static_cast<double> (frac (-6.0f)), col::warn);
        grad.addColour (static_cast<double> (frac (-1.0f)), col::rec);
        g.setGradientFill (grad);
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (bar, 7.0f);
        g.reduceClipRegion (clip);
        g.fillRect (bar.withWidth (w));
    }
    if (holdDb_ > -58.0f)
    {
        const float x = bar.getX() + bar.getWidth() * frac (holdDb_);
        g.setColour (col::text.withAlpha (0.85f));
        g.fillRect (x - 1.0f, bar.getY() + 2.0f, 2.0f, bar.getHeight() - 4.0f);
    }
    if (overActive())
    {
        const auto badge = juce::Rectangle<float> (bar.getRight() + 8.0f, r.getCentreY() - 10.0f, 48.0f, 20.0f);
        g.setColour (col::rec);
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (fonts::bold (13.0f));
        g.drawText ("OVER", badge, juce::Justification::centred, false);
    }
}

// =============================================================================== BannerBar
BannerBar::BannerBar() : retry_ ("Retry"), settings_ ("Settings"), reveal_ ("Show in folder"), close_ ("Dismiss")
{
    for (ActionButton* b : { &retry_, &settings_, &reveal_ })
    {
        b->setStyle (ActionButton::Style::Secondary);
        addAndMakeVisible (*b);
    }
    retry_.setLabel ("Retry");
    settings_.setLabel ("Settings");
    reveal_.setLabel ("Show in folder");
    close_.setStyle (ActionButton::Style::Icon);
    close_.setIcon (Icon::Close);
    close_.setTooltip ("Dismiss");
    addAndMakeVisible (close_);
    retry_.onClick = [this] { if (onRetry) onRetry(); };
    settings_.onClick = [this] { if (onSettings) onSettings(); };
    reveal_.onClick = [this] { if (onReveal) onReveal(); };
    close_.onClick = [this] { if (onDismiss) onDismiss(); };
}

void BannerBar::setContent (const juce::String& text, bool error, bool retry, bool settings, bool reveal)
{
    text_ = text;
    error_ = error;
    retry_.setVisible (retry);
    settings_.setVisible (settings);
    reveal_.setVisible (reveal);
    resized();
    repaint();
}

void BannerBar::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    close_.setBounds (r.removeFromRight (36));
    for (ActionButton* b : { &reveal_, &settings_, &retry_ })
        if (b->isVisible())
        {
            const int w = b == &reveal_ ? 140 : 96;
            b->setBounds (r.removeFromRight (w));
            r.removeFromRight (6);
        }
}

void BannerBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const juce::Colour tint = error_ ? col::rec : col::ok;
    g.setColour (tint.withAlpha (0.14f));
    g.fillRoundedRectangle (r, metric::controlRadius);
    g.setColour (tint.withAlpha (0.7f));
    g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);
    drawIcon (g, error_ ? Icon::Warning : Icon::Info, { 14.0f, r.getCentreY() - 11.0f, 22.0f, 22.0f }, tint, 1.8f);
    g.setColour (col::text);
    g.setFont (fonts::regular (15.0f));
    int right = getWidth() - 44;
    for (const ActionButton* b : { &retry_, &settings_, &reveal_ })
        if (b->isVisible())
            right = std::min (right, b->getX() - 8);
    g.drawFittedText (text_, juce::Rectangle<int> (44, 2, std::max (40, right - 44), getHeight() - 4), juce::Justification::centredLeft, 2, 0.9f);
}

// =============================================================================== StatusBar
void StatusBar::set (const juce::String& state, juce::Colour dot, const juce::String& hint, const juce::String& right)
{
    if (state == state_ && dot == dot_ && hint == hint_ && right == right_)
        return;
    state_ = state;
    dot_ = dot;
    hint_ = hint;
    right_ = right;
    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (dot_);
    g.fillEllipse (r.getX() + 2.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (col::text);
    g.setFont (fonts::semibold (16.0f));
    const float sw = juce::GlyphArrangement::getStringWidth (fonts::semibold (16.0f), state_);
    g.drawText (state_, juce::Rectangle<float> (r.getX() + 22.0f, r.getY(), sw + 6.0f, r.getHeight()), juce::Justification::centredLeft, false);

    const float rightW = right_.isEmpty() ? 0.0f : juce::jmin (r.getWidth() * 0.4f, juce::GlyphArrangement::getStringWidth (fonts::regular (13.0f), right_) + 8.0f);
    g.setColour (col::text2);
    g.setFont (fonts::regular (14.0f));
    const float hx = r.getX() + 22.0f + sw + 18.0f;
    g.drawText (hint_, juce::Rectangle<float> (hx, r.getY(), std::max (20.0f, r.getRight() - hx - rightW - 12.0f), r.getHeight()),
                juce::Justification::centredLeft, true);
    if (rightW > 0.0f)
    {
        g.setColour (col::text3);
        g.setFont (fonts::regular (13.0f));
        g.drawText (right_, juce::Rectangle<float> (r.getRight() - rightW, r.getY(), rightW, r.getHeight()), juce::Justification::centredRight, true);
    }
}

// =============================================================================== SegmentedAttachment
SegmentedAttachment::SegmentedAttachment (juce::RangedAudioParameter& p, SegmentedControl& c)
    : control_ (c), attachment_ (p, [this, &c] (float v)
{
    updating_ = true;
    c.setSelected (juce::roundToInt (v));
    updating_ = false;
})
{
    control_.onChange = [this] (int i)
    {
        if (! updating_)
            attachment_.setValueAsCompleteGesture (static_cast<float> (i));
    };
    attachment_.sendInitialUpdate();
}
}  // namespace rb::ui
