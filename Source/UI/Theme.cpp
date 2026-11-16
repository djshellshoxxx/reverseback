#include "Theme.h"

#include <cmath>

#include <BinaryData.h>

namespace rb::ui
{
Accent accentFor (Mode m)
{
    switch (m)
    {
        case Mode::Record: return { juce::Colour (0xff6454dc), juce::Colour (0xffa699ff) };
        case Mode::Live: return { juce::Colour (0xff0e7490), juce::Colour (0xff4cd3f5) };
        case Mode::File: return { juce::Colour (0xffa04a07), juce::Colour (0xfffbbf24) };
    }
    return { juce::Colour (0xff6454dc), juce::Colour (0xffa699ff) };
}

namespace fonts
{
namespace
{
juce::Typeface::Ptr load (const void* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, static_cast<std::size_t> (size));
}

juce::Font make (const juce::Typeface::Ptr& tf, float size)
{
    if (tf == nullptr)
        return juce::Font (juce::FontOptions (size));   // falls back to the system font
    return juce::Font (juce::FontOptions (tf).withHeight (size));
}
}  // namespace

juce::Font regular (float size)
{
    static const auto tf = load (RBAssets::InterRegular_otf, RBAssets::InterRegular_otfSize);
    return make (tf, size);
}
juce::Font medium (float size)
{
    static const auto tf = load (RBAssets::InterMedium_otf, RBAssets::InterMedium_otfSize);
    return make (tf, size);
}
juce::Font semibold (float size)
{
    static const auto tf = load (RBAssets::InterSemiBold_otf, RBAssets::InterSemiBold_otfSize);
    return make (tf, size);
}
juce::Font bold (float size)
{
    static const auto tf = load (RBAssets::InterBold_otf, RBAssets::InterBold_otfSize);
    return make (tf, size);
}
}  // namespace fonts

namespace
{
std::atomic<int> reducedMotionOverride { 0 };
}

void setReducedMotionOverride (int mode) { reducedMotionOverride.store (mode); }

bool motionEnabled()
{
    const int m = reducedMotionOverride.load();
    if (m == 1)
        return false;
    if (m == 2)
        return true;
    // Honour the common desktop hints when present.
    const auto env = juce::SystemStats::getEnvironmentVariable ("REDUCE_MOTION", {});
    return ! (env == "1" || env.equalsIgnoreCase ("true"));
}

// ------------------------------------------------------------------------------------------ icons
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> b, juce::Colour colour, float stroke)
{
    using juce::Path;
    using juce::PathStrokeType;
    g.setColour (colour);
    const float s = std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const juce::Rectangle<float> r (c.x - s * 0.5f, c.y - s * 0.5f, s, s);
    const PathStrokeType st (stroke, PathStrokeType::curved, PathStrokeType::rounded);
    auto at = [&r] (float x, float y) { return juce::Point<float> (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight()); };

    switch (icon)
    {
        case Icon::None: break;
        case Icon::Record: g.fillEllipse (r.reduced (s * 0.14f)); break;
        case Icon::Stop: g.fillRoundedRectangle (r.reduced (s * 0.2f), s * 0.1f); break;
        case Icon::Play:
        {
            Path p;
            p.addTriangle (at (0.28f, 0.16f), at (0.28f, 0.84f), at (0.84f, 0.5f));
            g.fillPath (p.createPathWithRoundedCorners (s * 0.08f));
            break;
        }
        case Icon::Backward:
        {
            Path p;
            p.addTriangle (at (0.72f, 0.16f), at (0.72f, 0.84f), at (0.16f, 0.5f));
            g.fillPath (p.createPathWithRoundedCorners (s * 0.08f));
            break;
        }
        case Icon::Forward:
        {
            Path p;
            p.addTriangle (at (0.28f, 0.16f), at (0.28f, 0.84f), at (0.84f, 0.5f));
            g.fillPath (p.createPathWithRoundedCorners (s * 0.08f));
            break;
        }
        case Icon::Replay:
        {
            Path p;
            p.addCentredArc (c.x, c.y, s * 0.32f, s * 0.32f, 0.0f, juce::MathConstants<float>::pi * 0.35f,
                             juce::MathConstants<float>::pi * 1.95f, true);
            g.strokePath (p, st);
            Path head;
            head.addTriangle (at (0.1f, 0.12f), at (0.1f, 0.5f), at (0.44f, 0.31f));
            g.fillPath (head);
            break;
        }
        case Icon::Save:
        {
            Path p;
            p.startNewSubPath (at (0.5f, 0.12f));
            p.lineTo (at (0.5f, 0.64f));
            p.startNewSubPath (at (0.28f, 0.44f));
            p.lineTo (at (0.5f, 0.66f));
            p.lineTo (at (0.72f, 0.44f));
            p.startNewSubPath (at (0.16f, 0.72f));
            p.lineTo (at (0.16f, 0.88f));
            p.lineTo (at (0.84f, 0.88f));
            p.lineTo (at (0.84f, 0.72f));
            g.strokePath (p, st);
            break;
        }
        case Icon::Folder:
        {
            Path p;
            p.startNewSubPath (at (0.1f, 0.78f));
            p.lineTo (at (0.1f, 0.24f));
            p.lineTo (at (0.4f, 0.24f));
            p.lineTo (at (0.5f, 0.36f));
            p.lineTo (at (0.9f, 0.36f));
            p.lineTo (at (0.9f, 0.78f));
            p.closeSubPath();
            g.strokePath (p.createPathWithRoundedCorners (s * 0.06f), st);
            break;
        }
        case Icon::Gear:
        {
            Path p;
            p.addStar (c, 8, s * 0.3f, s * 0.44f, 0.0f);
            p.addEllipse (c.x - s * 0.13f, c.y - s * 0.13f, s * 0.26f, s * 0.26f);
            p.setUsingNonZeroWinding (false);
            g.fillPath (p.createPathWithRoundedCorners (s * 0.04f));
            break;
        }
        case Icon::Menu:
            for (float y : { 0.28f, 0.5f, 0.72f })
                g.drawLine (at (0.2f, y).x, at (0.2f, y).y, at (0.8f, y).x, at (0.8f, y).y, stroke + 0.2f);
            break;
        case Icon::Freeze:
        {
            Path p;
            for (int i = 0; i < 3; ++i)
            {
                const float a = juce::MathConstants<float>::pi * static_cast<float> (i) / 3.0f;
                p.startNewSubPath (c.x + std::cos (a) * s * 0.4f, c.y + std::sin (a) * s * 0.4f);
                p.lineTo (c.x - std::cos (a) * s * 0.4f, c.y - std::sin (a) * s * 0.4f);
            }
            g.strokePath (p, st);
            break;
        }
        case Icon::Hold:
        {
            g.drawEllipse (r.reduced (s * 0.14f), stroke);
            g.fillEllipse (r.reduced (s * 0.34f));
            break;
        }
        case Icon::Mic:
        {
            g.drawRoundedRectangle (at (0.36f, 0.1f).x, at (0.36f, 0.1f).y, s * 0.28f, s * 0.46f, s * 0.14f, stroke);
            Path p;   // lower half-ellipse around the capsule
            p.addCentredArc (c.x, at (0, 0.46f).y, s * 0.28f, s * 0.26f, 0.0f, juce::MathConstants<float>::halfPi,
                             juce::MathConstants<float>::pi * 1.5f, true);
            g.strokePath (p, st);
            g.drawLine (c.x, at (0, 0.72f).y, c.x, at (0, 0.88f).y, stroke);
            g.drawLine (at (0.34f, 0).x, at (0, 0.88f).y, at (0.66f, 0).x, at (0, 0.88f).y, stroke);
            break;
        }
        case Icon::Headphones:
        {
            Path p;   // headband: upper half-ellipse
            p.addCentredArc (c.x, at (0, 0.6f).y, s * 0.36f, s * 0.42f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (p, st);
            g.fillRoundedRectangle (at (0.1f, 0).x, at (0, 0.58f).y, s * 0.17f, s * 0.3f, s * 0.06f);
            g.fillRoundedRectangle (at (0.73f, 0).x, at (0, 0.58f).y, s * 0.17f, s * 0.3f, s * 0.06f);
            break;
        }
        case Icon::Trim:
        {
            g.drawLine (at (0.22f, 0.2f).x, at (0, 0.2f).y, at (0.22f, 0).x, at (0, 0.8f).y, stroke);
            g.drawLine (at (0.78f, 0).x, at (0, 0.2f).y, at (0.78f, 0).x, at (0, 0.8f).y, stroke);
            g.drawLine (at (0.22f, 0).x, at (0, 0.5f).y, at (0.78f, 0).x, at (0, 0.5f).y, stroke);
            break;
        }
        case Icon::Undo:
        {
            Path p;
            p.startNewSubPath (at (0.2f, 0.4f));
            p.cubicTo (at (0.5f, 0.2f), at (0.85f, 0.35f), at (0.8f, 0.7f));
            g.strokePath (p, st);
            Path head;
            head.addTriangle (at (0.12f, 0.2f), at (0.12f, 0.58f), at (0.42f, 0.4f));
            g.fillPath (head);
            break;
        }
        case Icon::Shuffle:
        {
            Path p;
            p.startNewSubPath (at (0.12f, 0.3f));
            p.cubicTo (at (0.5f, 0.3f), at (0.5f, 0.7f), at (0.82f, 0.7f));
            p.startNewSubPath (at (0.12f, 0.7f));
            p.cubicTo (at (0.5f, 0.7f), at (0.5f, 0.3f), at (0.82f, 0.3f));
            g.strokePath (p, st);
            g.fillPath ([&] { Path h; h.addTriangle (at (0.78f, 0.18f), at (0.78f, 0.42f), at (0.94f, 0.3f)); return h; }());
            g.fillPath ([&] { Path h; h.addTriangle (at (0.78f, 0.58f), at (0.78f, 0.82f), at (0.94f, 0.7f)); return h; }());
            break;
        }
        case Icon::Chevron:
        {
            Path p;
            p.startNewSubPath (at (0.26f, 0.38f));
            p.lineTo (at (0.5f, 0.62f));
            p.lineTo (at (0.74f, 0.38f));
            g.strokePath (p, st);
            break;
        }
        case Icon::ChevronUp:
        {
            Path p;
            p.startNewSubPath (at (0.26f, 0.62f));
            p.lineTo (at (0.5f, 0.38f));
            p.lineTo (at (0.74f, 0.62f));
            g.strokePath (p, st);
            break;
        }
        case Icon::Close:
        {
            Path p;
            p.startNewSubPath (at (0.28f, 0.28f));
            p.lineTo (at (0.72f, 0.72f));
            p.startNewSubPath (at (0.72f, 0.28f));
            p.lineTo (at (0.28f, 0.72f));
            g.strokePath (p, st);
            break;
        }
        case Icon::Check:
        {
            Path p;
            p.startNewSubPath (at (0.22f, 0.54f));
            p.lineTo (at (0.42f, 0.74f));
            p.lineTo (at (0.8f, 0.3f));
            g.strokePath (p, st);
            break;
        }
        case Icon::Warning:
        {
            Path p;
            p.addTriangle (at (0.5f, 0.12f), at (0.92f, 0.84f), at (0.08f, 0.84f));
            g.strokePath (p.createPathWithRoundedCorners (s * 0.06f), st);
            g.drawLine (c.x, at (0, 0.38f).y, c.x, at (0, 0.6f).y, stroke + 0.2f);
            g.fillEllipse (c.x - 1.3f, at (0, 0.7f).y - 1.3f, 2.6f, 2.6f);
            break;
        }
        case Icon::Info:
        {
            g.drawEllipse (r.reduced (s * 0.1f), stroke);
            g.drawLine (c.x, at (0, 0.46f).y, c.x, at (0, 0.7f).y, stroke + 0.2f);
            g.fillEllipse (c.x - 1.3f, at (0, 0.31f).y - 1.3f, 2.6f, 2.6f);
            break;
        }
        case Icon::Speaker:
        {
            Path p;
            p.startNewSubPath (at (0.12f, 0.4f));
            p.lineTo (at (0.3f, 0.4f));
            p.lineTo (at (0.5f, 0.22f));
            p.lineTo (at (0.5f, 0.78f));
            p.lineTo (at (0.3f, 0.6f));
            p.lineTo (at (0.12f, 0.6f));
            p.closeSubPath();
            g.fillPath (p);
            Path w;
            w.addCentredArc (at (0.5f, 0.5f).x, c.y, s * 0.2f, s * 0.2f, 0.0f, juce::MathConstants<float>::pi * 0.35f,
                             juce::MathConstants<float>::pi * 0.65f, true);
            w.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, at (0.5f, 0.5f).x, c.y));
            g.strokePath (w, st);
            break;
        }
    }
}

// ------------------------------------------------------------------------------------------ text
juce::String fmtSeconds (double seconds, int decimals) { return juce::String (seconds, decimals) + " s"; }

juce::String fmtMillis (double seconds) { return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms"; }

juce::String fmtClock (double seconds, bool millis)
{
    seconds = std::max (0.0, seconds);
    // Round once, then split: 0.99996 s is 0:01.000, never "0:00.1000" or "0:00.000".
    const juce::int64 units = millis ? static_cast<juce::int64> (std::llround (seconds * 1000.0)) : static_cast<juce::int64> (std::llround (seconds));
    const juce::int64 total = millis ? units / 1000 : units;
    const int m = static_cast<int> (total / 60), s = static_cast<int> (total % 60);
    juce::String out = juce::String (m) + ":" + juce::String (s).paddedLeft ('0', 2);
    if (millis)
        out += "." + juce::String (static_cast<int> (units % 1000)).paddedLeft ('0', 3);
    return out;
}

juce::String fmtDb (double db, int decimals)
{
    return (db <= -59.95 ? juce::String ("-inf") : juce::String (db, decimals)) + " dB";
}

double parseSeconds (const juce::String& textIn, double fallback)
{
    juce::String t = textIn.trim().toLowerCase();
    if (t.isEmpty())
        return fallback;
    if (t.retainCharacters ("0123456789").isEmpty())
        return fallback;   // "abc" is not a time: keep the old value instead of silently jumping to zero
    if (t.contains (":"))   // m:ss(.mmm)
    {
        const double m = t.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
        const double s = t.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
        return m * 60.0 + s;
    }
    const double v = t.retainCharacters ("0123456789.-+e").getDoubleValue();
    if (t.endsWith ("ms"))
        return v * 0.001;
    return v;
}
}  // namespace rb::ui
