#include "OfflineRender.h"

namespace rb
{
Frame renderFrameCount (const RenderSpec& spec)
{
    if (spec.source == nullptr || spec.selection.empty())
        return 0;
    const double outRate = spec.outRate > 0.0 ? spec.outRate : spec.source->sampleRate();
    const double ratio = std::clamp (spec.source->sampleRate() / outRate * spec.speed, 1.0 / 64.0, 16.0);
    return std::max<Frame> (1, static_cast<Frame> (std::llround (static_cast<double> (spec.selection.length()) / ratio)));
}

RenderResult renderOffline (const RenderSpec& spec, const RenderBlockFn& sink, const std::atomic<bool>* cancel, std::size_t block)
{
    if (spec.source == nullptr || spec.selection.empty())
        return RenderResult::Done;

    ClipPlayer player;
    player.configure (block, 16.0);
    player.setOutputRate (spec.outRate > 0.0 ? spec.outRate : spec.source->sampleRate());
    player.setSource (spec.source, spec.selection);
    player.setDirection (spec.direction);
    player.setSpeed (spec.speed);
    player.setFadeFrames (spec.fadeFrames);
    player.setLoop (LoopPattern::Once);
    player.play (true);

    const std::size_t channels = static_cast<std::size_t> (std::min (spec.source->channels(), static_cast<int> (kMaxChannels)));
    std::vector<float> store (kMaxChannels * block);
    float* planes[kMaxChannels] = { store.data(), store.data() + block };

    for (;;)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return RenderResult::Cancelled;

        std::fill (store.begin(), store.end(), 0.0f);
        const std::size_t got = player.process (planes, channels, block);
        if (got > 0 && ! sink (planes, channels, got))
            return RenderResult::Aborted;
        if (player.underrun())
            return RenderResult::ReadFailed;
        if (got < block)
            return RenderResult::Done;
    }
}
}  // namespace rb
