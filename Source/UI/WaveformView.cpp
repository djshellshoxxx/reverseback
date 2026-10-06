#include "WaveformView.h"

#include <algorithm>
#include <cmath>

namespace reverseback
{
WaveformView::WaveformView()
{
    setTitle("Audio waveform and selection");
    setDescription("Drag the left or right selection handle to choose part of the audio.");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void WaveformView::setClip(std::shared_ptr<const AudioClip> clip)
{
    clip_ = std::move(clip);
    if (clip_ != nullptr)
        selection_ = {0, clip_->frameCount()};
    else
        selection_ = {};
    showPlayhead_ = false;
    repaint();
}

void WaveformView::setSelection(Selection selection)
{
    if (clip_ == nullptr)
        return;

    const auto minFrames = std::max<Frame>(
        1, static_cast<Frame>(std::llround(clip_->sampleRate() * 0.05)));
    selection.begin = std::min(selection.begin, clip_->frameCount());
    selection.end = std::min(selection.end, clip_->frameCount());

    if (selection.end <= selection.begin + minFrames)
        selection.end = std::min<Frame>(clip_->frameCount(), selection.begin + minFrames);

    if (selection.end <= selection.begin)
        selection.begin = selection.end > minFrames ? selection.end - minFrames : 0;

    selection_ = selection;
    repaint();
}

void WaveformView::setPlayhead(Frame frame, Direction direction)
{
    playhead_ = frame;
    playheadDirection_ = direction;
    showPlayhead_ = true;
    repaint();
}

void WaveformView::clearPlayhead()
{
    showPlayhead_ = false;
    repaint();
}

void WaveformView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1b2535));
    g.fillRoundedRectangle(bounds, 12.0f);

    if (clip_ == nullptr || clip_->frameCount() == 0)
    {
        g.setColour(juce::Colour(0xffa8b7cd));
        g.setFont(15.0f);
        g.drawFittedText("No audio loaded or recorded yet", getLocalBounds().reduced(20),
                         juce::Justification::centred, 1);
        return;
    }

    const auto midY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.42f;
    const int width = std::max(1, getWidth());

    g.setColour(juce::Colour(0xffa699ff));
    for (int x = 0; x < width; ++x)
    {
        const auto begin = static_cast<Frame>(
            static_cast<long double>(x) * clip_->frameCount() / width);
        const auto end = std::max<Frame>(
            begin + 1,
            static_cast<Frame>(static_cast<long double>(x + 1) * clip_->frameCount() / width));

        float lo = 1.0f;
        float hi = -1.0f;
        for (std::size_t channel = 0; channel < clip_->channels(); ++channel)
        {
            for (Frame frame = begin; frame < std::min(end, clip_->frameCount()); ++frame)
            {
                const auto sample = clip_->sample(channel, frame);
                lo = std::min(lo, sample);
                hi = std::max(hi, sample);
            }
        }

        if (hi < lo)
            lo = hi = 0.0f;

        const auto y1 = midY - std::clamp(hi, -1.0f, 1.0f) * halfHeight;
        const auto y2 = midY - std::clamp(lo, -1.0f, 1.0f) * halfHeight;
        g.drawVerticalLine(x, y1, y2);
    }

    const auto beginX = xForFrame(selection_.begin);
    const auto endX = xForFrame(selection_.end);

    g.setColour(juce::Colour(0x88101621));
    if (beginX > 0)
        g.fillRect(0, 0, beginX, getHeight());
    if (endX < getWidth())
        g.fillRect(endX, 0, getWidth() - endX, getHeight());

    g.setColour(juce::Colour(0xff48d5af));
    g.drawLine(static_cast<float>(beginX), 0.0f, static_cast<float>(beginX),
               static_cast<float>(getHeight()), 2.0f);
    g.drawLine(static_cast<float>(endX), 0.0f, static_cast<float>(endX),
               static_cast<float>(getHeight()), 2.0f);

    if (showPlayhead_)
    {
        const auto x = xForFrame(std::min(playhead_, clip_->frameCount()));
        g.setColour(playheadDirection_ == Direction::Reverse
                        ? juce::Colour(0xffffbd69)
                        : juce::Colour(0xff67d8ff));
        g.drawLine(static_cast<float>(x), 0.0f, static_cast<float>(x),
                   static_cast<float>(getHeight()), 2.0f);
    }
}

void WaveformView::mouseDown(const juce::MouseEvent& event)
{
    if (clip_ == nullptr)
        return;

    const auto beginX = xForFrame(selection_.begin);
    const auto endX = xForFrame(selection_.end);
    dragHandle_ = std::abs(event.x - beginX) <= std::abs(event.x - endX)
        ? DragHandle::Begin
        : DragHandle::End;
    updateDraggedHandle(event.x);
}

void WaveformView::mouseDrag(const juce::MouseEvent& event)
{
    updateDraggedHandle(event.x);
}

int WaveformView::xForFrame(Frame frame) const noexcept
{
    if (clip_ == nullptr || clip_->frameCount() == 0)
        return 0;

    return static_cast<int>(std::llround(
        static_cast<long double>(frame) * getWidth() / clip_->frameCount()));
}

Frame WaveformView::frameForX(int x) const noexcept
{
    if (clip_ == nullptr || getWidth() <= 0)
        return 0;

    x = std::clamp(x, 0, getWidth());
    return static_cast<Frame>(
        static_cast<long double>(x) * clip_->frameCount() / getWidth());
}

void WaveformView::updateDraggedHandle(int x)
{
    if (clip_ == nullptr || dragHandle_ == DragHandle::None)
        return;

    const auto minFrames = std::max<Frame>(
        1, static_cast<Frame>(std::llround(clip_->sampleRate() * 0.05)));
    const auto frame = frameForX(x);

    if (dragHandle_ == DragHandle::Begin)
        selection_.begin = std::min(frame, selection_.end > minFrames ? selection_.end - minFrames : 0);
    else
        selection_.end = std::max(frame, std::min<Frame>(clip_->frameCount(), selection_.begin + minFrames));

    if (onSelectionChanged)
        onSelectionChanged(selection_);
    repaint();
}
}
