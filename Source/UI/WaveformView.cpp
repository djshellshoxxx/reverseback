#include "WaveformView.h"

#include "LookAndFeel.h"

namespace rb::ui
{
namespace
{
constexpr float kPad = 20.0f;
constexpr float kHeaderH = 44.0f;
constexpr float kRulerH = 30.0f;

double niceStep (double span, float pixels, float wantPx)
{
    static constexpr double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800 };
    const double perPx = span / std::max (1.0f, pixels);
    for (double s : steps)
        if (s / perPx >= static_cast<double> (wantPx))
            return s;
    return 3600.0;
}
}  // namespace

WaveformView::WaveformView()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setTitle ("Waveform");
}

void WaveformView::setModel (Model m)
{
    const bool dataChanged = m.version != model_.version || m.mode != model_.mode || m.dimmed != model_.dimmed
                             || m.overview != model_.overview;
    model_ = std::move (m);
    if (dataChanged)
        cacheValid_ = false;
    setMouseCursor (model_.selectionEditable ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (kPad, 0.0f).withTrimmedTop (kHeaderH).withTrimmedBottom (kRulerH);
}

float WaveformView::normToX (double n) const
{
    const auto p = plotArea();
    return p.getX() + static_cast<float> (juce::jlimit (0.0, 1.0, n)) * p.getWidth();
}

double WaveformView::xToNorm (float x) const
{
    const auto p = plotArea();
    return p.getWidth() > 0.0f ? juce::jlimit (0.0, 1.0, static_cast<double> ((x - p.getX()) / p.getWidth())) : 0.0;
}

void WaveformView::rebuildCache()
{
    const auto plot = plotArea();
    cachedBounds_ = getLocalBounds();
    const int w = std::max (1, juce::roundToInt (plot.getWidth()));
    const int h = std::max (1, juce::roundToInt (plot.getHeight()));
    cache_ = juce::Image (juce::Image::ARGB, w, h, true);
    cacheValid_ = true;

    const bool haveOverview = model_.overview && model_.overview->size() > 0;
    const bool haveLive = ! model_.liveEnv.empty();
    if (! haveOverview && ! haveLive)
        return;

    const float cy = static_cast<float> (h) * 0.5f;
    const float half = static_cast<float> (h) * 0.5f * 0.94f;
    std::vector<float> top (static_cast<std::size_t> (w)), bottom (static_cast<std::size_t> (w));
    for (int x = 0; x < w; ++x)
    {
        float mn = 0.0f, mx = 0.0f;
        const double t0 = static_cast<double> (x) / w, t1 = static_cast<double> (x + 1) / w;
        if (haveOverview)
        {
            model_.overview->query (t0, t1, mn, mx);
        }
        else
        {
            const std::size_t n = model_.liveEnv.size() / 2;
            const std::size_t b0 = std::min (n - 1, static_cast<std::size_t> (t0 * static_cast<double> (n)));
            const std::size_t b1 = std::min (n - 1, std::max (b0, static_cast<std::size_t> (t1 * static_cast<double> (n))));
            mn = model_.liveEnv[b0 * 2];
            mx = model_.liveEnv[b0 * 2 + 1];
            for (std::size_t b = b0 + 1; b <= b1; ++b)
            {
                mn = std::min (mn, model_.liveEnv[b * 2]);
                mx = std::max (mx, model_.liveEnv[b * 2 + 1]);
            }
        }
        // keep a hairline visible for silence so an empty-looking take still shows its extent
        const float mxC = std::min (1.0f, std::max (mx, 0.012f)), mnC = std::max (-1.0f, std::min (mn, -0.012f));
        top[static_cast<std::size_t> (x)] = cy - mxC * half;
        bottom[static_cast<std::size_t> (x)] = cy - mnC * half;
    }

    juce::Path p;
    p.startNewSubPath (0.0f, top[0]);
    for (int x = 1; x < w; ++x)
        p.lineTo (static_cast<float> (x), top[static_cast<std::size_t> (x)]);
    for (int x = w - 1; x >= 0; --x)
        p.lineTo (static_cast<float> (x), bottom[static_cast<std::size_t> (x)]);
    p.closeSubPath();

    const float alpha = model_.dimmed ? 0.35f : 1.0f;
    juce::Graphics g (cache_);
    // soft glow, then the body gradient, then a crisp outline
    g.setColour (model_.accent.wave.withAlpha (0.13f * alpha));
    g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
    g.setGradientFill (juce::ColourGradient (model_.accent.wave.withAlpha (0.95f * alpha), 0.0f, cy - half,
                                             model_.accent.wave.withAlpha (0.45f * alpha), 0.0f, cy, false));
    g.fillPath (p);
    g.setColour (model_.accent.wave.brighter (0.25f).withAlpha (0.9f * alpha));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void WaveformView::drawOverlay (juce::Graphics& g, juce::Rectangle<float> plot)
{
    if (model_.overlay == Overlay::None)
        return;
    const auto accent = model_.accent.wave;

    if (model_.overlay == Overlay::Countdown)
    {
        g.setColour (col::bg1.withAlpha (0.55f));
        g.fillRect (plot);
        g.setColour (col::text);
        g.setFont (fonts::bold (96.0f));
        g.drawText (juce::String (model_.countdownNumber), plot, juce::Justification::centred, false);
        return;
    }

    // Progress strip along the bottom of the plot with the text above it.
    const auto strip = juce::Rectangle<float> (plot.getX(), plot.getBottom() - 26.0f, plot.getWidth(), 10.0f);
    g.setColour (col::bg2.withAlpha (0.9f));
    g.fillRoundedRectangle (strip, 5.0f);
    juce::Colour fillColour = model_.overlay == Overlay::Recording ? col::rec : (model_.overlay == Overlay::Waiting ? col::warn : accent);
    g.setColour (fillColour);
    g.fillRoundedRectangle (strip.withWidth (std::max (10.0f, strip.getWidth() * juce::jlimit (0.0f, 1.0f, model_.overlayProgress))), 5.0f);

    auto textArea = plot.withTrimmedBottom (40.0f);
    if (model_.overlay == Overlay::Recording)
    {
        g.setColour (col::bg1.withAlpha (0.35f));
        g.fillRect (plot.withTrimmedBottom (34.0f));
        const bool pulse = motionEnabled() ? (juce::Time::getMillisecondCounterHiRes() * 0.001 - std::floor (juce::Time::getMillisecondCounterHiRes() * 0.001)) < 0.5 : true;
        g.setColour (col::rec.withAlpha (pulse ? 1.0f : 0.45f));
        g.fillEllipse (textArea.getCentreX() - 92.0f, textArea.getCentreY() - 44.0f, 16.0f, 16.0f);
    }
    g.setColour (col::text);
    g.setFont (fonts::semibold (30.0f));
    g.drawText (model_.overlayText, textArea.translated (model_.overlay == Overlay::Recording ? 12.0f : 0.0f, -14.0f),
                juce::Justification::centred, false);
    g.setColour (col::text2);
    g.setFont (fonts::regular (16.0f));
    g.drawText (model_.overlaySub, textArea.translated (0.0f, 24.0f), juce::Justification::centred, false);
}

void WaveformView::drawHandles (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const float xb = normToX (model_.selBegin), xe = normToX (model_.selEnd);
    g.setColour (col::bg0.withAlpha (0.58f));
    g.fillRect (plot.getX(), plot.getY(), std::max (0.0f, xb - plot.getX()), plot.getHeight());
    g.fillRect (xe, plot.getY(), std::max (0.0f, plot.getRight() - xe), plot.getHeight());

    g.setColour (model_.accent.wave.withAlpha (0.10f));
    g.fillRect (xb, plot.getY(), std::max (0.0f, xe - xb), plot.getHeight());

    for (int i = 0; i < 2; ++i)
    {
        const float x = i == 0 ? xb : xe;
        const bool active = hasKeyboardFocus (true) && activeHandle_ == i;
        g.setColour (model_.accent.wave);
        g.fillRect (x - 1.0f, plot.getY(), 2.0f, plot.getHeight());
        const auto grip = juce::Rectangle<float> (x - 6.0f, plot.getCentreY() - 16.0f, 12.0f, 32.0f);
        g.setColour (model_.selectionEditable ? model_.accent.wave : col::text3);
        g.fillRoundedRectangle (grip, 5.0f);
        g.setColour (col::bg0.withAlpha (0.7f));
        for (float dy : { -5.0f, 0.0f, 5.0f })
            g.fillRect (grip.getCentreX() - 2.5f, grip.getCentreY() + dy - 0.5f, 5.0f, 1.0f);
        if (active)
            RbLookAndFeel::drawFocusRing (g, grip, 5.0f);
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto card = getLocalBounds().toFloat().reduced (0.5f);
    // card with a soft shadow, border and (during a drag-and-drop) an accent outline
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (card.translated (0.0f, 3.0f), metric::cardRadius);
    g.setColour (col::bg1);
    g.fillRoundedRectangle (card, metric::cardRadius);
    g.setColour (model_.dropHighlight ? model_.accent.wave : col::line);
    g.drawRoundedRectangle (card, metric::cardRadius, model_.dropHighlight ? 2.0f : 1.0f);

    // header
    g.setColour (col::text2);
    g.setFont (fonts::medium (13.0f));
    g.drawText (model_.header.toUpperCase(), juce::Rectangle<float> (kPad, 12.0f, card.getWidth() * 0.62f, 22.0f), juce::Justification::centredLeft, true);
    if (model_.chip.isNotEmpty())
    {
        const juce::Font f = fonts::medium (14.0f);
        const float tw = juce::GlyphArrangement::getStringWidth (f, model_.chip);
        const auto chip = juce::Rectangle<float> (card.getRight() - kPad - tw - 52.0f, 11.0f, tw + 52.0f, 24.0f);
        g.setColour (model_.accent.fill.withAlpha (0.28f));
        g.fillRoundedRectangle (chip, 12.0f);
        drawIcon (g, model_.chipDirection == Direction::Backward ? Icon::Backward : Icon::Forward,
                  { chip.getX() + 10.0f, chip.getY() + 4.0f, 16.0f, 16.0f }, model_.accent.wave, 1.6f);
        g.setColour (col::text);
        g.setFont (f);
        g.drawText (model_.chip, chip.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
    }
    if (model_.frozenBadge)
    {
        const auto b = juce::Rectangle<float> (card.getRight() - kPad - 190.0f, 11.0f, 80.0f, 24.0f);
        g.setColour (model_.accent.wave.withAlpha (0.25f));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::text);
        g.setFont (fonts::semibold (13.0f));
        g.drawText ("FROZEN", b, juce::Justification::centred, false);
    }

    const auto plot = plotArea();
    if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
        return;

    // grid, centre line, ruler
    const bool hasAny = (model_.overview && model_.overview->size() > 0) || ! model_.liveEnv.empty();
    if (hasAny)
    {
        g.setColour (col::line.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    }
    if (model_.durationSeconds > 0.0)
    {
        const double step = niceStep (model_.durationSeconds, plot.getWidth(), 90.0f);
        g.setFont (fonts::regular (13.0f));
        for (double t = 0.0; t <= model_.durationSeconds + 1.0e-9; t += step)
        {
            const float x = normToX (t / model_.durationSeconds);
            g.setColour (col::line.withAlpha (0.35f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            if (t < 1.0e-9 || t > model_.durationSeconds - step * 0.25)
                continue;
            g.setColour (col::text3);
            const juce::String label = model_.durationSeconds < 20.0 ? juce::String (t, step < 0.1 ? 2 : (step < 1.0 ? 1 : 0)) + " s" : fmtClock (t);
            g.drawText (label, juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 6.0f, 60.0f, 18.0f), juce::Justification::centred, false);
        }
    }
    g.setColour (col::text2);
    g.setFont (fonts::regular (13.0f));
    if (model_.rulerLeft.isNotEmpty())
        g.drawText (model_.rulerLeft, juce::Rectangle<float> (kPad, plot.getBottom() + 6.0f, 90.0f, 18.0f), juce::Justification::centredLeft, false);
    if (model_.rulerRight.isNotEmpty())
        g.drawText (model_.rulerRight, juce::Rectangle<float> (card.getRight() - kPad - 90.0f, plot.getBottom() + 6.0f, 90.0f, 18.0f), juce::Justification::centredRight, false);

    // waveform
    if (! cacheValid_ || cachedBounds_ != getLocalBounds())
        rebuildCache();
    const bool hasData = (model_.overview && model_.overview->size() > 0) || ! model_.liveEnv.empty();
    if (hasData)
    {
        g.drawImageAt (cache_, juce::roundToInt (plot.getX()), juce::roundToInt (plot.getY()));
    }
    else if (model_.emptyMessage.isNotEmpty() && model_.overlay == Overlay::None)
    {
        const auto box = plot.reduced (plot.getWidth() * 0.12f, plot.getHeight() * 0.18f);
        juce::Path dashed;
        dashed.addRoundedRectangle (box, 14.0f);
        juce::Path stroked;
        const float dashes[] = { 6.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (stroked, dashed, dashes, 2);
        g.setColour (col::line);
        g.fillPath (stroked);
        g.setColour (col::text2);
        g.setFont (fonts::medium (17.0f));
        g.drawFittedText (model_.emptyMessage, box.reduced (24.0f, 8.0f).toNearestInt(), juce::Justification::centred, 3, 1.0f);
    }

    if (model_.showSelection && hasData)
        drawHandles (g, plot);

    if (model_.showPlayhead && hasData)
    {
        const float x = normToX (model_.playhead);
        g.setColour (col::text);
        g.fillRect (x - 1.0f, plot.getY(), 2.0f, plot.getHeight());
        juce::Path tri;
        tri.addTriangle (x - 6.0f, plot.getY() - 7.0f, x + 6.0f, plot.getY() - 7.0f, x, plot.getY() + 2.0f);
        g.fillPath (tri);
    }

    drawOverlay (g, plot);
    if (hasKeyboardFocus (true) && ! model_.showSelection)
        RbLookAndFeel::drawFocusRing (g, card, metric::cardRadius);
}

void WaveformView::emit (double b, double e)
{
    if (onSelectionChanged && e > b)
        onSelectionChanged (b, e);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (! model_.showSelection)
        return;
    const float x = static_cast<float> (e.x);
    const bool nearHandle = std::abs (x - normToX (model_.selBegin)) <= 10.0f || std::abs (x - normToX (model_.selEnd)) <= 10.0f;
    setMouseCursor (! model_.selectionEditable ? juce::MouseCursor::NormalCursor
                                               : (nearHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor));
    setTooltip (model_.selectionEditable ? "Drag the handles to choose a region. Left/Right nudge, Up/Down switch handle. Double-click selects all."
                                         : "Stop playback to change the selection.");
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag_ = Drag::None;
    if (! model_.showSelection || ! model_.selectionEditable)
        return;
    const float x = static_cast<float> (e.x);
    if (std::abs (x - normToX (model_.selBegin)) <= 10.0f)
    {
        drag_ = Drag::Begin;
        activeHandle_ = 0;
    }
    else if (std::abs (x - normToX (model_.selEnd)) <= 10.0f)
    {
        drag_ = Drag::End;
        activeHandle_ = 1;
    }
    else if (plotArea().contains (e.position))
    {
        drag_ = Drag::Create;
        anchor_ = xToNorm (x);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag_ == Drag::None)
        return;
    const double n = xToNorm (static_cast<float> (e.x));
    const double minLen = model_.durationSeconds > 0.0 ? kMinSelectionSeconds / model_.durationSeconds : 0.0;
    switch (drag_)
    {
        case Drag::Begin: emit (std::min (n, model_.selEnd - minLen), model_.selEnd); break;
        case Drag::End: emit (model_.selBegin, std::max (n, model_.selBegin + minLen)); break;
        case Drag::Create:
            if (std::abs (n - anchor_) > minLen)
                emit (std::min (n, anchor_), std::max (n, anchor_));
            break;
        case Drag::None: break;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&) { drag_ = Drag::None; }

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (model_.showSelection && model_.selectionEditable && onSelectAll)
        onSelectAll();
}

bool WaveformView::keyPressed (const juce::KeyPress& k)
{
    if (! model_.showSelection || ! model_.selectionEditable || model_.durationSeconds <= 0.0)
        return false;
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        activeHandle_ = 1 - activeHandle_;
        repaint();
        return true;
    }
    const int dir = k == juce::KeyPress::leftKey ? -1 : (k == juce::KeyPress::rightKey ? 1 : 0);
    if (dir == 0)
        return false;
    const auto mods = k.getModifiers();
    const double stepSeconds = mods.isAltDown() ? 0.001 : (mods.isShiftDown() ? 0.1 : 0.01);
    const double d = static_cast<double> (dir) * stepSeconds / model_.durationSeconds;
    const double minLen = kMinSelectionSeconds / model_.durationSeconds;
    double b = model_.selBegin, e = model_.selEnd;
    if (activeHandle_ == 0)
        b = juce::jlimit (0.0, e - minLen, b + d);
    else
        e = juce::jlimit (b + minLen, 1.0, e + d);
    emit (b, e);
    return true;
}
}  // namespace rb::ui
