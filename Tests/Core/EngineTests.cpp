// Engine-level behaviour: File mode, mode switching, settings, rate change, snapshot.
#include "Rig.h"

using namespace rbt;

namespace
{
void setFile (Rig& r, std::shared_ptr<const ClipSource> clip, Frame a, Frame b)
{
    Command c;
    c.type = CommandType::SetFile;
    c.source = std::move (clip);
    c.a = a;
    c.b = b;
    r.eng.handle (std::move (c));
}
}  // namespace

RB_TEST (file_mode_plays_selection_reversed_and_reports_finished)
{
    Rig r;
    Settings s = r.s;
    s.mode = Mode::File;
    r.apply (s);
    auto clip = makeRampClip (48000.0, 60000);
    setFile (r, clip, 10000, 30000);
    r.cmd (CommandType::PlayFile, true);
    r.run (30000, 300);
    CHECK_EQ (r.out[0][0], 30000.0f);          // last selected frame first (ramp value = index + 1)
    CHECK_EQ (r.out[0][19999], 10001.0f);
    CHECK (allZero (r.out[0], 20000, 30000));
    CHECK_EQ (r.count (EventType::PlaybackFinished), 1);
}

RB_TEST (file_mode_stop_keeps_position_and_play_resumes_or_restarts)
{
    Rig r;
    Settings s = r.s;
    s.mode = Mode::File;
    s.direction = Direction::Forward;
    r.apply (s);
    setFile (r, makeRampClip (48000.0, 48000), 0, 48000);
    r.cmd (CommandType::PlayFile, true);
    r.run (4800, 256);
    r.cmd (CommandType::Stop);
    r.run (2048, 256);
    const auto snap = r.eng.snapshot();
    CHECK (snap.playhead > 0.1f && snap.playhead < 0.12f);
    r.cmd (CommandType::PlayFile, false);
    r.run (256, 256);
    CHECK (r.eng.snapshot().playhead > snap.playhead);
    r.cmd (CommandType::PlayFile, true);
    r.run (1024, 256);
    CHECK (r.eng.snapshot().playhead < 0.05f);
}

RB_TEST (file_direction_and_speed_changes_mid_play_are_smooth)
{
    Rig r;
    Settings s = r.s;
    s.mode = Mode::File;
    s.exactSamples = false;
    r.apply (s);
    r.input = [] (Frame) { return 0.0f; };
    setFile (r, makeSineClip (48000.0, 96000, 440.0), 0, 96000);
    r.cmd (CommandType::PlayFile, true);
    r.run (20000, 256);
    s.direction = Direction::Forward;
    r.apply (s);
    r.run (20000, 256);
    s.speed = 1.5;
    r.apply (s);
    r.run (20000, 256);
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < r.out[0].size(); ++i)
        maxStep = std::max (maxStep, std::abs (r.out[0][i] - r.out[0][i - 1]));
    CHECK (maxStep < 0.12f);   // no discontinuity larger than a few sine steps
}

RB_TEST (mode_change_stops_the_previous_mode_with_a_fade)
{
    Rig r;
    r.input = [] (Frame) { return 0.5f; };
    r.apply ([&] { Settings s = r.s; s.mode = Mode::Record; s.captureSeconds = 1.0; s.waitSeconds = 0.0; return s; }());
    r.startRecord();
    r.run (48000 + 4800, 256);
    const std::size_t at = r.out[0].size();
    Settings live = r.s;
    live.mode = Mode::Live;
    r.apply (live);
    r.run (4800, 256);
    CHECK_EQ (r.out[0][at + 480], 0.0f);
    CHECK (allZero (r.out[0], at + 480, r.out[0].size()));
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
    CHECK_EQ (static_cast<int> (r.eng.snapshot().mode), static_cast<int> (Mode::Live));
    // the take survives the mode change and replays once back in Record mode
    live.mode = Mode::Record;
    r.apply (live);
    const std::size_t again = r.out[0].size();
    r.cmd (CommandType::Replay);
    r.run (1000, 256);
    CHECK_EQ (r.out[0][again + 100], 0.5f);
}

RB_TEST (wrong_mode_commands_are_rejected)
{
    Rig r;
    Settings s = r.s;
    s.mode = Mode::File;
    r.apply (s);
    r.startRecord();
    r.startLive();
    r.run (256, 256);
    CHECK_EQ (r.count (EventType::Error), 2);
    CHECK (r.hasError (ErrorCode::WrongMode));
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
}

RB_TEST (C12_repeat_session_is_ignored_while_loop_is_active)
{
    Rig r;
    Settings s = r.s;
    s.captureSeconds = 0.5;
    s.waitSeconds = 0.0;
    s.repeatSession = true;
    s.loop = LoopPattern::Loop;
    r.apply (s);
    r.startRecord();
    r.run (48000 * 3, 256);
    CHECK_EQ (r.takes().size(), std::size_t { 1 });   // looped playback, no second capture
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Playing));
    r.cmd (CommandType::Stop);
    r.run (2000, 256);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
}

RB_TEST (speed_scales_record_playback_length_and_edge_fades_keep_duration)
{
    Rig r;
    Settings s = r.s;
    s.captureSeconds = 1.0;
    s.waitSeconds = 0.0;
    s.speed = 2.0;
    s.exactSamples = false;
    s.edgeFadeMs = 3.0;
    r.apply (s);
    r.input = [] (Frame i) { return 0.3f * static_cast<float> (std::sin (0.05 * static_cast<double> (i))); };
    r.startRecord();
    r.run (48000 * 2, 256);
    std::size_t last = 0;
    for (std::size_t i = 0; i < r.out[0].size(); ++i)
        if (r.out[0][i] != 0.0f)
            last = i;
    CHECK (last >= 48000 + 23990 && last < 48000 + 24000);   // 48000 frames at 2x -> 24000 frames
}

RB_TEST (P07_sample_rate_change_keeps_take_and_plays_it_at_the_new_rate)
{
    Rig r;
    r.input = [] (Frame i) { return 0.4f * static_cast<float> (std::sin (0.02 * static_cast<double> (i))); };
    Settings s = r.s;
    s.captureSeconds = 1.0;
    s.waitSeconds = 0.0;
    r.apply (s);
    r.startRecord();
    r.run (48000 + 100, 256);
    r.eng.prepare (44100.0, 512);      // device changed; transports are rebuilt, the take is kept
    r.eng.applySettings (s);
    CHECK_EQ (static_cast<int> (r.eng.snapshot().recordState), static_cast<int> (RecordTransport::State::Ready));
    // replay on the re-prepared engine
    Command c;
    c.type = CommandType::Replay;
    r.eng.handle (std::move (c));
    std::vector<float> a (512, 0.0f), oL (512), oR (512);
    const float* in[1] = { a.data() };
    float* o[2] = { oL.data(), oR.data() };
    std::size_t produced = 0;
    for (int i = 0; i < 200; ++i)
    {
        r.eng.process (in, 1, o, 512);
        for (std::size_t k = 0; k < 512; ++k)
            if (oL[k] != 0.0f)
                produced = static_cast<std::size_t> (i) * 512 + k + 1;
    }
    CHECK_NEAR (produced, 44100.0, 2.0);   // 48000 frames * 44100/48000
    CHECK_NEAR (r.eng.snapshot().sampleRate, 44100.0, 0);
}

RB_TEST (snapshot_reports_progress_for_countdown_recording_and_waiting)
{
    Rig r;
    Settings s = r.s;
    s.captureSeconds = 1.0;
    s.waitSeconds = 1.0;
    s.countdownSeconds = 1.0;
    r.apply (s);
    r.startRecord();
    r.run (24000, 256);
    auto a = r.eng.snapshot();
    CHECK_EQ (static_cast<int> (a.recordState), static_cast<int> (RecordTransport::State::Countdown));
    CHECK_EQ (a.stateLength, std::uint64_t { 48000 });
    CHECK (a.countdownLeft > 23000 && a.countdownLeft < 24100);
    r.run (48000, 256);
    auto b = r.eng.snapshot();
    CHECK_EQ (static_cast<int> (b.recordState), static_cast<int> (RecordTransport::State::Recording));
    CHECK (b.stateFrame > 23000 && b.stateFrame < 24100);
    r.run (30000, 256);
    auto c = r.eng.snapshot();
    CHECK_EQ (static_cast<int> (c.recordState), static_cast<int> (RecordTransport::State::Waiting));
    CHECK_EQ (c.takeId, std::uint32_t { 1 });
    CHECK_EQ (c.takeFrames, std::uint64_t { 48000 });
}

RB_TEST (input_overload_is_flagged_in_the_snapshot)
{
    Rig r;
    r.input = [] (Frame i) { return i > 1000 ? 1.5f : 0.1f; };
    r.run (4096, 256);
    CHECK (r.eng.snapshot().overloadBlocks > 0);
    CHECK (r.eng.snapshot().inputPeak >= 1.5f);
}
