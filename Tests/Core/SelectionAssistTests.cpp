#include "TestHarness.h"
#include "SelectionAssist.h"

RB_TEST (selection_context_padding_clamps_to_file)
{
    CHECK (rb::selectionAssist::pad ({20, 40}, 100, 10) == (rb::Selection {10, 50}));
    CHECK (rb::selectionAssist::pad ({2, 95}, 100, 10) == (rb::Selection {0, 100}));
    CHECK (rb::selectionAssist::pad ({90, 90}, 100, 10).empty());
}

RB_TEST (selection_zero_snap_uses_low_energy_boundaries)
{
    auto clip = rb::AudioClip::create (1000, 1, 200);
    for (rb::Frame i = 0; i < 200; ++i) clip->channel (0)[i] = 1.0f;
    clip->channel (0)[18] = 0.0f;
    clip->channel (0)[82] = 0.0f;
    clip->seal (200);
    const auto s = rb::selectionAssist::snapToZero (*clip, {20, 80}, 5);
    CHECK (s.begin >= 18 && s.begin <= 19);
    CHECK (s.end >= 82 && s.end <= 83);
    CHECK (s.begin < s.end);
}

RB_TEST (selection_loudest_window_is_frame_accurate)
{
    auto clip = rb::AudioClip::create (1000, 2, 100);
    clip->channel (0)[70] = 2.0f;
    clip->channel (1)[70] = 2.0f;
    clip->seal (100);
    const auto s = rb::selectionAssist::loudestWindow (*clip, {0, 100}, 10, 100);
    CHECK_EQ (s.length(), 10u);
    CHECK (s.begin <= 70 && s.end > 70);
    CHECK (rb::selectionAssist::loudestWindow (*clip, {5, 5}, 10, 100).empty());
}
