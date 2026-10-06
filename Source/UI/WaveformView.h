#pragma once

#include "AudioClip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace reverseback
{
class WaveformView final : public juce::Component
{
public:
    WaveformView();

    void setClip(std::shared_ptr<const AudioClip> clip);
    void setSelection(Selection selection);
    [[nodiscard]] Selection selection() const noexcept { return selection_; }

    void setPlayhead(Frame frame, Direction direction);
    void clearPlayhead();

    std::function<void(Selection)> onSelectionChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    enum class DragHandle { None, Begin, End };

    [[nodiscard]] int xForFrame(Frame frame) const noexcept;
    [[nodiscard]] Frame frameForX(int x) const noexcept;
    void updateDraggedHandle(int x);

    std::shared_ptr<const AudioClip> clip_;
    Selection selection_{};
    Frame playhead_{0};
    Direction playheadDirection_{Direction::Forward};
    bool showPlayhead_{false};
    DragHandle dragHandle_{DragHandle::None};
};
}
