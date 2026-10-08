// The framework-free engine: composes Record, Live and File playback behind one real-time API
// (ENGINE_DESIGN section 9). The JUCE processor drives exactly this class.
#pragma once

#include "ClipPlayer.h"
#include "Events.h"
#include "LiveTransport.h"
#include "RecordTransport.h"

namespace rb
{
struct Settings
{
    Mode mode = Mode::Record;

    // Record
    double captureSeconds = 5.0;
    double waitSeconds = 2.0;
    double tailGapSeconds = 0.5;
    double countdownSeconds = 0.0;
    bool autoStart = false;
    double thresholdDb = -45.0;
    bool repeatSession = false;

    // Live
    double liveChunkSeconds = 0.5;
    double liveDelaySeconds = 2.0;
    double liveFadeMs = 2.0;

    // Playback of clips (Record take, File)
    double speed = 1.0;
    Direction direction = Direction::Backward;
    LoopPattern loop = LoopPattern::Once;
    double edgeFadeMs = 3.0;
    bool exactSamples = false;
    double stopFadeMs = 10.0;
};

enum class CommandType : std::uint8_t
{
    StartRecord,
    StartHold,
    ReleaseHold,
    FinishEarly,
    Replay,
    StartLive,
    Freeze,
    Resume,
    Stop,
    SetFile,           // source, a = selection begin, b = selection end
    SetSelection,      // a, b
    PlayFile,          // flag = fromStart
    ProvideSpareTake,  // take
    SetTakeSelection   // a, b ({0,0} = whole take)
};

struct Command
{
    CommandType type = CommandType::Stop;
    bool flag = false;
    Frame a = 0, b = 0;
    std::shared_ptr<AudioClip> take;
    std::shared_ptr<LiveStorage> live;
    std::shared_ptr<const ClipSource> source;
};

// Trivially copyable state published to the UI (sequence-locked).
struct Snapshot
{
    std::uint8_t mode = 0;
    std::uint8_t recordState = 0;
    std::uint8_t liveState = 0;
    std::uint8_t filePlaying = 0;
    std::uint8_t error = 0;
    std::uint8_t fileUnderrun = 0;
    std::uint8_t recordHeld = 0;
    std::uint8_t reserved = 0;

    std::uint32_t takeId = 0;
    std::uint32_t frozenGeneration = 0;
    std::uint32_t overloadBlocks = 0;
    std::uint32_t sanitizedCount = 0;

    std::uint64_t stateFrame = 0;
    std::uint64_t stateLength = 0;
    std::uint64_t takeFrames = 0;
    std::uint64_t countdownLeft = 0;
    std::int64_t liveChunkIndex = -1;
    std::int64_t liveCaptureChunk = -1;
    std::int32_t liveSlot = -1;          // slot to display (playing chunk, or the frozen chunk)
    std::int32_t liveCaptureSlot = -1;

    float playhead = 0.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float fillProgress = 0.0f;
    double sampleRate = 0.0;
    std::uint32_t blocks = 0;
    std::uint32_t limiterLatency = 0;
};

class Engine
{
public:
    // Non-real-time: allocates. Stops everything; retained takes survive a re-prepare.
    void prepare (double sampleRate, std::size_t maxBlock);

    // ----- real-time (audio thread) -----
    void applySettings (const Settings& s) noexcept;
    void handle (Command&& c) noexcept;
    RecordSettings recordSettings() const noexcept;
    // `in`: captured channels already mapped/gained (1 or 2); `out`: two planar stereo buffers (overwritten).
    void process (const float* const* in, std::size_t inChannels, float* const* out, std::size_t frames) noexcept;
    void stopAll() noexcept;
    void stopImmediate() noexcept;
    void setLimiterLatency (std::size_t frames) noexcept { limiterLatency_ = static_cast<std::uint32_t> (frames); }
    void noteSanitized (std::uint32_t total) noexcept { sanitized_ = total; }
    // Direct monitoring is muted while Record mode is waiting or playing back (V1 section 5.5).
    bool monitorMuted() const noexcept
    {
        return mode_ == Mode::Record && (rec_.state() == RecordTransport::State::Waiting || rec_.state() == RecordTransport::State::Playing);
    }

    // ----- any thread -----
    Snapshot snapshot() const noexcept { return snapshot_.load(); }

    // ----- control thread -----
    EventSink& events() noexcept { return sink_; }
    double sampleRate() const noexcept { return rate_; }
    // For tests and the UI: the live storage / take owned by the audio side must not be touched here.

    // Direct access for tests only (not thread-safe).
    RecordTransport& recordForTest() noexcept { return rec_; }
    LiveTransport& liveForTest() noexcept { return live_; }
    ClipPlayer& fileForTest() noexcept { return file_; }

private:
    void configurePlayers() noexcept;
    void publish (std::size_t frames) noexcept;

    double rate_ = 48000.0;
    std::size_t maxBlock_ = 512;
    Settings s_ {};
    bool settingsValid_ = false;
    Mode mode_ = Mode::Record;

    EventSink sink_;
    RecordTransport rec_;
    LiveTransport live_;
    ClipPlayer file_;
    std::shared_ptr<const ClipSource> fileSource_;
    Selection fileSel_ {};
    bool fileUnderrun_ = false;

    ErrorCode lastError_ = ErrorCode::None;
    float inPeak_ = 0.0f, outPeak_ = 0.0f;
    std::uint32_t overload_ = 0, blocks_ = 0, sanitized_ = 0, limiterLatency_ = 0;
    SeqLock<Snapshot> snapshot_;
};
}  // namespace rb
