// The waveform card: take / live chunk / file display, ruler, playhead, selection handles and the
// activity overlays (countdown, recording progress, buffer fill). GUI_DESIGN.md section 5.
#pragma once

#include "Theme.h"
#include "WaveformOverview.h"

#include <functional>
#include <memory>

namespace rb::ui
{
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Overlay { None, Countdown, Recording, Waiting, Filling };

    struct Model
    {
        Mode mode = Mode::Record;
        Accent accent = accentFor (Mode::Record);
        std::shared_ptr<const WaveformOverview> overview;   // Record / File
        std::vector<float> liveEnv;                         // Live: min/max pairs for the shown chunk
        bool dimmed = false;
        juce::String header, chip, emptyMessage, rulerLeft, rulerRight;
        double durationSeconds = 0.0;                       // for ruler ticks
        bool showPlayhead = false;
        float playhead = 0.0f;                              // 0..1 left to right
        Direction chipDirection = Direction::Backward;
        bool showSelection = false;
        double selBegin = 0.0, selEnd = 1.0;                // normalised
        bool selectionEditable = false;
        Overlay overlay = Overlay::None;
        juce::String overlayText, overlaySub;
        float overlayProgress = 0.0f;
        int countdownNumber = 0;
        bool dropHighlight = false;
        bool frozenBadge = false;
        std::uint64_t version = 0;                          // bump when the waveform data changes
    };

    WaveformView();
    void setModel (Model m);
    const Model& model() const noexcept { return model_; }

    std::function<void (double begin, double end)> onSelectionChanged;   // normalised, begin < end
    std::function<void()> onSelectAll;
    std::function<juce::String()> describe;                               // accessible description

    void paint (juce::Graphics&) override;
    void resized() override { cacheValid_ = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::Rectangle<float> plotArea() const;
    double xToNorm (float x) const;

private:
    enum class Drag { None, Begin, End, Create };
    float normToX (double n) const;
    void rebuildCache();
    void drawOverlay (juce::Graphics&, juce::Rectangle<float> plot);
    void drawHandles (juce::Graphics&, juce::Rectangle<float> plot);
    void emit (double b, double e);

    Model model_;
    juce::Image cache_;
    bool cacheValid_ = false;
    std::uint64_t cachedVersion_ = ~0ull;
    juce::Rectangle<int> cachedBounds_;
    Drag drag_ = Drag::None;
    double anchor_ = 0.0;
    int activeHandle_ = 0;   // 0 begin, 1 end (keyboard)
};
}  // namespace rb::ui
