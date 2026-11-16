// Offline rendering through the same ClipPlayer used for playback, so export and preview agree
// exactly (ENGINE_DESIGN section 11).
#pragma once

#include "ClipPlayer.h"

#include <atomic>
#include <functional>

namespace rb
{
struct RenderSpec
{
    const ClipSource* source = nullptr;
    Selection selection {};
    Direction direction = Direction::Backward;
    double speed = 1.0;
    double outRate = 0.0;        // 0 = source rate
    Frame fadeFrames = 0;        // 0 = Exact Samples
};

enum class RenderResult { Done, Cancelled, ReadFailed, Aborted };

// Number of frames the render will produce: round(L / ratio).
Frame renderFrameCount (const RenderSpec& spec);

// Receives planar blocks (channels = source channels). Return false to abort.
using RenderBlockFn = std::function<bool (const float* const* planar, std::size_t channels, std::size_t frames)>;

RenderResult renderOffline (const RenderSpec& spec, const RenderBlockFn& sink, const std::atomic<bool>* cancel = nullptr,
                            std::size_t block = 4096);
}  // namespace rb
