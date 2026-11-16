// Design tokens, fonts, vector icons and text formatting (GUI_DESIGN.md section 2).
#pragma once

#include "Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rb::ui
{
// ---- colour tokens
namespace col
{
inline const juce::Colour bg0 { 0xff0e131c };
inline const juce::Colour bg1 { 0xff151c29 };
inline const juce::Colour bg2 { 0xff1e283a };
inline const juce::Colour bg3 { 0xff2a364c };
inline const juce::Colour line { 0xff2f3b52 };
inline const juce::Colour text { 0xffecf2fa };
inline const juce::Colour text2 { 0xffa8b7cd };
inline const juce::Colour text3 { 0xff7f8ea6 };
inline const juce::Colour ok { 0xff48d5af };
inline const juce::Colour warn { 0xffffc857 };
inline const juce::Colour rec { 0xffff5c6c };
}  // namespace col

struct Accent
{
    juce::Colour fill, wave;
};
Accent accentFor (Mode m);

// ---- geometry tokens (logical pixels)
namespace metric
{
constexpr int margin = 32, gutter = 16, headerH = 64, tabsH = 56, sourceH = 40, paramH = 56, transportH = 62, optionsH = 40,
              statusH = 36, bannerH = 44, drawerW = 320, minW = 820, minH = 620, defaultW = 960, defaultH = 700;
constexpr float cardRadius = 16.0f, controlRadius = 10.0f, primaryRadius = 14.0f;
}  // namespace metric

// ---- fonts (Inter, embedded)
namespace fonts
{
juce::Font regular (float size);
juce::Font medium (float size);
juce::Font semibold (float size);
juce::Font bold (float size);
}  // namespace fonts

// ---- motion
void setReducedMotionOverride (int mode);   // 0 follow the system (animations on), 1 reduced, 2 full motion
bool motionEnabled();

// ---- icons
enum class Icon
{
    None, Record, Stop, Play, Replay, Save, Folder, Gear, Menu, Freeze, Hold, Backward, Forward, Mic, Headphones, Trim, Undo,
    Shuffle, Chevron, ChevronUp, Close, Check, Warning, Info, Speaker
};
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour, float stroke = 1.8f);

// ---- text
juce::String fmtSeconds (double seconds, int decimals = 2);          // "5.00 s"
juce::String fmtMillis (double seconds);                              // "500 ms"
juce::String fmtClock (double seconds, bool millis = false);          // "2:31" or "2:31.400"
juce::String fmtDb (double db, int decimals = 1);                     // "-12.0 dB"
double parseSeconds (const juce::String& text, double fallback);      // accepts "5", "5 s", "500ms", "1:05.5"
}  // namespace rb::ui
