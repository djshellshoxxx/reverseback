#include "ReverseBackProcessor.h"

#include "SafeParse.h"

#include <cmath>

namespace rb
{
namespace
{
constexpr std::uint32_t kTrigStart = 1u, kTrigHoldDown = 2u, kTrigHoldUp = 4u, kTrigReplay = 8u, kTrigFreeze = 16u;

}  // namespace

ReverseBackProcessor::ReverseBackProcessor() : ReverseBackProcessor (SettingsStore::defaultFile()) {}

ReverseBackProcessor::ReverseBackProcessor (const juce::File& settingsFile)
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ReverseBackState", createParameterLayout (juce::JUCEApplicationBase::isStandaloneApp())),
      params_ (apvts),
      standalone_ (juce::JUCEApplicationBase::isStandaloneApp()),
      store_ (settingsFile)
{
    settings_ = store_.load();
    startTimerHz (30);
}

ReverseBackProcessor::~ReverseBackProcessor()
{
    stopTimer();
    *alive_ = false;
    files_.cancel();
    exporter_.cancel();
    // Drop engine-side references on this (non-audio) thread.
    engine_.stopImmediate();
    engine_.events().drainRetired();
}

juce::AudioProcessorEditor* ReverseBackProcessor::createEditor() { return createReverseBackEditor (*this); }

// =================================================================================================
// Real-time side
// =================================================================================================
bool ReverseBackProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    const auto& in = l.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

namespace
{
// Counts processBlock calls in flight so prepareToPlay/releaseResources can wait for them: hosts are not
// required to serialise those calls (the VST3 wrapper only takes the callback lock around processBlock).
struct ProcessGuard
{
    explicit ProcessGuard (std::atomic<int>& c) : count_ (c) { count_.fetch_add (1, std::memory_order_seq_cst); }
    ~ProcessGuard() { count_.fetch_sub (1, std::memory_order_release); }
    std::atomic<int>& count_;
};
}  // namespace

void ReverseBackProcessor::quiesce()
{
    prepared_.store (false, std::memory_order_seq_cst);   // new blocks see this and bail out ...
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (inProcess_.load (std::memory_order_seq_cst) != 0 && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (1);                            // ... and the one that may still be running finishes
}

void ReverseBackProcessor::releaseResources()
{
    quiesce();
    startPending_.store (false);
    // Nothing is running any more: leave a clean, silent engine and tell the UI so.
    Command stale;
    while (commands_.pop (stale)) {}
    engine_.stopImmediate();
    engine_.publishNow();
    engine_.events().drainRetired();
}

void ReverseBackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0)
        return;
    quiesce();
    maxBlock_ = static_cast<std::size_t> (std::clamp (samplesPerBlock, 512, 16384));
    Command stale;
    while (commands_.pop (stale)) {}   // commands issued for an old device configuration are void

    engine_.prepare (sampleRate, maxBlock_);
    inputStage_.prepare (sampleRate, maxBlock_);
    outputStage_.prepare (sampleRate, maxBlock_, standalone_);
    engine_.setLimiterLatency (outputStage_.latencyFrames());
    setLatencySamples (static_cast<int> (outputStage_.latencyFrames()));   // 0 in plugin formats (no limiter)

    outStore_.assign (2 * maxBlock_, 0.0f);
    out_[0] = outStore_.data();
    out_[1] = outStore_.data() + maxBlock_;
    rate_.store (sampleRate);
    faultFlag_.store (false);
    // A trigger parameter that is already high is not a new edge: a device change must never start recording.
    prevTrig_[0] = params_.trgStart->load() >= 0.5f;
    prevTrig_[1] = params_.trgHold->load() >= 0.5f;
    prevTrig_[2] = params_.trgReplay->load() >= 0.5f;
    prevTrig_[3] = params_.trgFreeze->load() >= 0.5f;
    startPending_.store (false);
    prepared_.store (true, std::memory_order_seq_cst);

    // The file the UI shows must also be the file the engine has, even if it was loaded (or restored from a
    // saved state) while no audio device was running.
    PublishedFile f;
    {
        const std::lock_guard<std::mutex> lock (fileStateMutex_);
        f = published_;
    }
    if (f.source != nullptr)
    {
        Command c;
        c.type = CommandType::SetFile;
        c.source = f.source;
        c.a = f.sel.begin;
        c.b = f.sel.end;
        commands_.push (std::move (c));
    }
    juce::MessageManager::callAsync ([w = alive_, this]
    {
        if (*w)
            sendChangeMessage();
    });
}

void ReverseBackProcessor::detectTriggers (const ParamRefs& p) noexcept
{
    const bool now[4] = { p.trgStart->load() >= 0.5f, p.trgHold->load() >= 0.5f, p.trgReplay->load() >= 0.5f, p.trgFreeze->load() >= 0.5f };
    std::uint32_t bits = 0;
    if (now[0] && ! prevTrig_[0]) bits |= kTrigStart;
    if (now[1] && ! prevTrig_[1]) bits |= kTrigHoldDown;
    if (! now[1] && prevTrig_[1]) bits |= kTrigHoldUp;
    if (now[2] && ! prevTrig_[2]) bits |= kTrigReplay;
    if (now[3] && ! prevTrig_[3]) bits |= kTrigFreeze;
    for (int i = 0; i < 4; ++i)
        prevTrig_[i] = now[i];
    if (bits != 0)
        triggerBits_.fetch_or (bits, std::memory_order_relaxed);
}

void ReverseBackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const ProcessGuard guard (inProcess_);
    const int total = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (! prepared_.load (std::memory_order_seq_cst) || total <= 0)
    {
        buffer.clear();
        return;
    }

    engine_.applySettings (params_.readSettings());
    inputStage_.setMode (params_.readChannelMode());
    inputStage_.setGainDb (params_.inGain->load());
    outputStage_.setVolumeDb (params_.outVol->load());
    outputStage_.setMonitor (standalone_ ? params_.monitor->load() * 0.01f : 0.0f);
    detectTriggers (params_);

    Command c;
    while (commands_.pop (c))
        engine_.handle (std::move (c));

    for (int off = 0; off < total; off += static_cast<int> (maxBlock_))
    {
        const std::size_t m = static_cast<std::size_t> (std::min (static_cast<int> (maxBlock_), total - off));
        const float* hostIn[2] = { numIn > 0 ? buffer.getReadPointer (0) + off : nullptr, numIn > 1 ? buffer.getReadPointer (1) + off : nullptr };
        const float* const* mapped = inputStage_.process (hostIn, static_cast<std::size_t> (std::min (numIn, 2)), m);
        const std::size_t mappedCh = static_cast<std::size_t> (inputStage_.outputChannels());

        engine_.noteSanitized (inputStage_.sanitizedCount());
        engine_.process (numIn > 0 ? mapped : nullptr, numIn > 0 ? mappedCh : 0, out_, m);
        if (! outputStage_.process (out_, numIn > 0 ? mapped : nullptr, numIn > 0 ? mappedCh : 0, engine_.monitorMuted(), m))
        {
            engine_.stopImmediate();   // a DSP fault silences output and stops the transport
            faultFlag_.store (true, std::memory_order_relaxed);
        }

        if (numOut >= 2)
        {
            std::memcpy (buffer.getWritePointer (0) + off, out_[0], m * sizeof (float));
            std::memcpy (buffer.getWritePointer (1) + off, out_[1], m * sizeof (float));
            for (int ch = 2; ch < numOut; ++ch)
                juce::FloatVectorOperations::clear (buffer.getWritePointer (ch) + off, static_cast<int> (m));
        }
        else if (numOut == 1)
        {
            float* d = buffer.getWritePointer (0) + off;
            for (std::size_t i = 0; i < m; ++i)
                d[i] = 0.5f * (out_[0][i] + out_[1][i]);
        }
    }
}

void ReverseBackProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Bypass cancels capture and playback; the host's pass-through then applies. Start-type commands issued
    // meanwhile are handed back instead of piling up, state commands (file, selection) are still applied.
    {
        const ProcessGuard guard (inProcess_);
        if (prepared_.load (std::memory_order_seq_cst))
        {
            Command c;
            while (commands_.pop (c))
                engine_.handleBypassed (std::move (c));
            engine_.bypassBlock();
        }
    }
    juce::AudioProcessor::processBlockBypassed (buffer, midi);
}

// =================================================================================================
// Control side
// =================================================================================================
bool ReverseBackProcessor::post (Command&& c)
{
    // Not running (no device yet): state is re-sent by prepareToPlay, transport actions need a running engine.
    if (! prepared_.load())
        return false;
    if (! commands_.push (std::move (c)))
    {
        showBanner (BannerKind::Engine, "ReverseBack is busy. Please try again.", false, false, 3.0);
        return false;
    }
    return true;
}

// A Start that was posted but not yet seen by the audio thread: further presses must not queue more takes.
bool ReverseBackProcessor::isStartPending() const
{
    return startPending_.load() && engine_.snapshot().blocks == startPendingBlocks_.load();
}

void ReverseBackProcessor::markStartPosted()
{
    startPendingBlocks_.store (engine_.snapshot().blocks);
    startPending_.store (true);
}

bool ReverseBackProcessor::isBusy() const
{
    const Snapshot s = engine_.snapshot();
    switch (static_cast<Mode> (s.mode))
    {
        case Mode::Record: return s.recordState != static_cast<std::uint8_t> (RecordTransport::State::Ready);
        case Mode::Live: return s.liveState != static_cast<std::uint8_t> (LiveTransport::State::Ready);
        case Mode::File: return s.filePlaying != 0;
    }
    return false;
}

int ReverseBackProcessor::captureChannels() const { return static_cast<int> (params_.readChannelMode() == InputChannelMode::Stereo ? 2 : 1); }

bool ReverseBackProcessor::takeBudgetOk (Frame frames, int channels, bool repeat) const
{
    if (frames == 0 || frames > kTakeBudgetBytes)   // also rules out overflow of the product below
        return false;
    const std::size_t one = static_cast<std::size_t> (frames) * static_cast<std::size_t> (channels) * sizeof (float);
    const std::size_t retained = take_ && take_->clip ? static_cast<const AudioClip*> (take_->clip.get())->byteSize() : 0;
    return one * (repeat ? 2 : 1) + retained <= kTakeBudgetBytes;
}

void ReverseBackProcessor::startRecording (bool hold)
{
    if (! prepared_.load() || isStartPending())
        return;
    if (getTotalNumInputChannels() == 0 || (host_ != nullptr && ! host_->hasInputDevice()))
    {
        showBanner (BannerKind::Device, host_ != nullptr ? "No microphone is available. Choose an input device in Settings, or use Reverse File."
                                                         : "No input is routed to ReverseBack. Route a track or input to the plugin.",
                    host_ != nullptr, host_ != nullptr);
        return;
    }
    const Settings s = params_.readSettings();
    const double rate = rate_.load();
    const Frame frames = hold ? framesFor (kMaxCaptureSeconds, rate, 1) : framesFor (s.captureSeconds, rate, 1);
    const int ch = captureChannels();
    const bool repeat = s.repeatSession && s.loop == LoopPattern::Once && ! hold;
    if (! takeBudgetOk (frames, ch, repeat))
    {
        showBanner (BannerKind::Engine, "This recording would use more than the 256 MiB memory budget. Shorten the recording length.");
        return;
    }
    try
    {
        Command c;
        c.type = hold ? CommandType::StartHold : CommandType::StartRecord;
        c.take = AudioClip::create (rate, ch, frames);
        if (post (std::move (c)))
        {
            markStartPosted();
            dismissBanner();
        }
    }
    catch (const std::exception&)   // bad_alloc, or length_error for an absurd size
    {
        showBanner (BannerKind::Engine, "Not enough memory is available for this recording.");
    }
}

void ReverseBackProcessor::startLive()
{
    if (! prepared_.load() || isStartPending())
        return;
    if (getTotalNumInputChannels() == 0 || (host_ != nullptr && ! host_->hasInputDevice()))
    {
        showBanner (BannerKind::Device, host_ != nullptr ? "No microphone is available. Choose an input device in Settings."
                                                         : "No input is routed to ReverseBack. Route a track or input to the plugin.",
                    host_ != nullptr, host_ != nullptr);
        return;
    }
    const Settings s = params_.readSettings();
    const double rate = rate_.load();
    auto storage = LiveStorage::create (rate, captureChannels(), framesFor (s.liveChunkSeconds, rate, 1), framesFor (s.liveDelaySeconds, rate));
    if (storage == nullptr)
    {
        showBanner (BannerKind::Engine, "These Live settings need too much memory. Use a shorter chunk or delay.");
        return;
    }
    Command c;
    c.type = CommandType::StartLive;
    c.live = storage;
    if (post (std::move (c)))
    {
        liveStorage_ = std::move (storage);   // only what the engine was actually given
        markStartPosted();
        dismissBanner();
    }
}

void ReverseBackProcessor::actionStart()
{
    switch (mode())
    {
        case Mode::Record: startRecording (false); break;
        case Mode::Live: startLive(); break;
        case Mode::File: playFile (false); break;
    }
}

void ReverseBackProcessor::actionStop()
{
    Command c;
    c.type = CommandType::Stop;
    post (std::move (c));
}

void ReverseBackProcessor::actionStartStop()
{
    if (isBusy())
        actionStop();
    else
        actionStart();
}

void ReverseBackProcessor::actionHoldDown()
{
    if (mode() == Mode::Record && ! isBusy())
        startRecording (true);
}

void ReverseBackProcessor::actionHoldUp()
{
    Command c;
    c.type = CommandType::ReleaseHold;
    post (std::move (c));
}

void ReverseBackProcessor::actionFinishEarly()
{
    Command c;
    c.type = CommandType::FinishEarly;
    post (std::move (c));
}

void ReverseBackProcessor::actionReplay()
{
    if (mode() == Mode::Record && take_ != nullptr && ! isBusy())
    {
        Command c;
        c.type = CommandType::Replay;
        post (std::move (c));
    }
    else if (mode() == Mode::File)
    {
        playFile (true);
    }
}

void ReverseBackProcessor::actionFreezeResume()
{
    if (mode() != Mode::Live)
        return;
    const auto st = static_cast<LiveTransport::State> (engine_.snapshot().liveState);
    Command c;
    if (st == LiveTransport::State::Frozen || st == LiveTransport::State::Freezing)
        c.type = CommandType::Resume;
    else if (st == LiveTransport::State::Filling || st == LiveTransport::State::Running)
        c.type = CommandType::Freeze;
    else
        return;
    post (std::move (c));
}

// ---- file mode -----------------------------------------------------------------------------------
void ReverseBackProcessor::loadFile (const juce::File& file, bool keepSelection, Selection selection)
{
    loadProgress_ = 0.0f;
    files_.load (file,
                 [w = alive_, this] (float p)
                 {
                     if (*w)
                     {
                         loadProgress_ = p;
                         sendChangeMessage();
                     }
                 },
                 [w = alive_, this, keepSelection, selection] (LoadOutcome o)
                 {
                     if (*w)
                         finishLoad (std::move (o), keepSelection, selection);
                 });
    sendChangeMessage();
}

void ReverseBackProcessor::cancelLoad()
{
    files_.cancel();
    sendChangeMessage();
}

void ReverseBackProcessor::finishLoad (LoadOutcome outcome, bool keepSelection, Selection selection)
{
    loadProgress_ = 1.0f;
    if (! outcome.ok())
    {
        if (outcome.error != LoadError::Cancelled)
            showBanner (BannerKind::File, "Could not load the file: " + outcome.message);
        sendChangeMessage();
        return;   // the previously loaded file stays in place
    }
    fileAsset_ = std::move (outcome.asset);
    fileSel_ = { 0, fileAsset_->frames };
    if (keepSelection && ! selection.empty() && selection.end <= fileAsset_->frames
        && selection.length() >= framesFor (kMinSelectionSeconds, fileAsset_->sampleRate, 1))
        fileSel_ = selection;
    ++fileVersion_;
    publishFileState();
    rememberFolder (fileAsset_->file);
    dismissBanner();

    Command c;
    c.type = CommandType::SetFile;
    c.source = fileAsset_->source;
    c.a = fileSel_.begin;
    c.b = fileSel_.end;
    post (std::move (c));
    sendChangeMessage();
}

void ReverseBackProcessor::publishFileState()
{
    PublishedFile f;
    if (fileAsset_)
    {
        f.path = fileAsset_->file;
        f.source = fileAsset_->source;
        f.sel = fileSel_;
    }
    const std::lock_guard<std::mutex> lock (fileStateMutex_);
    published_ = std::move (f);
}

bool ReverseBackProcessor::setFileSelection (Selection sel)
{
    if (! fileAsset_ || sel.end > fileAsset_->frames
        || sel.length() < framesFor (kMinSelectionSeconds, fileAsset_->sampleRate, 1))
        return false;
    if (sel == fileSel_)
        return true;
    fileSel_ = sel;
    publishFileState();
    Command c;
    c.type = CommandType::SetSelection;
    c.a = sel.begin;
    c.b = sel.end;
    post (std::move (c));
    sendChangeMessage();
    return true;
}

void ReverseBackProcessor::playFile (bool fromStart)
{
    if (! fileAsset_ || mode() != Mode::File)
        return;
    if (fileAsset_->disk != nullptr)
    {
        // Make sure the first blocks are resident before playback begins (the audio thread never waits).
        const bool backward = params_.direction->load() >= 0.5f;
        const Frame pos = backward ? fileSel_.end - 1 : fileSel_.begin;
        fileAsset_->disk->prime (fromStart ? pos : fileSel_.begin + static_cast<Frame> (engine_.snapshot().playhead * static_cast<float> (fileSel_.length())),
                                 backward, 400);
    }
    Command c;
    c.type = CommandType::PlayFile;
    c.flag = fromStart;
    post (std::move (c));
    dismissBanner();
}

// ---- take tools ----------------------------------------------------------------------------------
bool ReverseBackProcessor::trimSilence()
{
    if (! take_ || ! take_->clip)
        return false;
    const Selection sel = findNonSilentSelection (*take_->clip);
    if (sel.empty())
        return false;
    auto next = std::make_shared<TakeAsset> (*take_);
    next->trim = sel;
    take_ = next;
    ++takeVersion_;
    Command c;
    c.type = CommandType::SetTakeSelection;
    c.a = sel.begin;
    c.b = sel.end;
    post (std::move (c));
    sendChangeMessage();
    return true;
}

void ReverseBackProcessor::undoTrim()
{
    if (! take_)
        return;
    auto next = std::make_shared<TakeAsset> (*take_);
    next->trim = {};
    take_ = next;
    ++takeVersion_;
    Command c;
    c.type = CommandType::SetTakeSelection;
    post (std::move (c));
    sendChangeMessage();
}

// ---- live / frozen chunk -------------------------------------------------------------------------
std::shared_ptr<AudioClip> ReverseBackProcessor::copyFrozenChunk() const
{
    const Snapshot a = engine_.snapshot();
    if (! liveStorage_ || a.liveState != static_cast<std::uint8_t> (LiveTransport::State::Frozen) || a.liveSlot < 0)
        return nullptr;
    const LiveStorage& st = *liveStorage_;
    auto clip = AudioClip::create (st.rate, st.channels, st.W);
    for (int c = 0; c < st.channels; ++c)
        std::memcpy (clip->channel (c), st.plane (static_cast<Frame> (a.liveSlot), c), static_cast<std::size_t> (st.W) * sizeof (float));
    clip->seal (st.W);
    const Snapshot b = engine_.snapshot();   // the chunk is immutable while Frozen; confirm it stayed that way
    if (b.frozenGeneration != a.frozenGeneration || b.liveState != a.liveState)
        return nullptr;
    return clip;
}

// ---- presets -------------------------------------------------------------------------------------
void ReverseBackProcessor::setParam (const char* id, double plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (plainValue)));
        p->endChangeGesture();
    }
}

void ReverseBackProcessor::applyPreset (const PresetValues& v)
{
    actionStop();   // a preset never leaves anything running...
    setParam (ids::mode, static_cast<int> (v.mode));
    setParam (ids::capture, v.captureSeconds);
    setParam (ids::wait, v.waitSeconds);
    setParam (ids::chunk, v.liveChunkSeconds);
    setParam (ids::delay, v.liveDelaySeconds);
    setParam (ids::speed, v.speed);
    setParam (ids::loop, static_cast<int> (v.loop));
    setParam (ids::direction, v.direction == Direction::Forward ? 0 : 1);
    setParam (ids::repeat, v.repeatSession ? 1 : 0);
    // ...and never records, touches gain, devices or the monitor.
    sendChangeMessage();
}

void ReverseBackProcessor::applySurprise()
{
    if (isBusy() || mode() == Mode::Live)
        return;
    static SimpleRng rng (static_cast<std::uint64_t> (juce::Time::currentTimeMillis()) | 1u);
    const SurpriseResult r = makeSurprise (rng);
    setParam (ids::speed, r.speed);
    setParam (ids::direction, r.direction == Direction::Forward ? 0 : 1);
    setParam (ids::loop, static_cast<int> (r.loop));
    if (r.loop != LoopPattern::Once)
        setParam (ids::repeat, 0);
    sendChangeMessage();
}

PresetValues ReverseBackProcessor::currentPresetValues() const
{
    const Settings s = params_.readSettings();
    PresetValues v;
    v.mode = s.mode;
    v.captureSeconds = s.captureSeconds;
    v.waitSeconds = s.waitSeconds;
    v.liveChunkSeconds = s.liveChunkSeconds;
    v.liveDelaySeconds = s.liveDelaySeconds;
    v.speed = s.speed;
    v.loop = s.loop;
    v.direction = s.direction;
    v.repeatSession = s.repeatSession;
    return v;
}

void ReverseBackProcessor::saveUserPreset (const juce::String& name)
{
    const juce::String n = name.trim().substring (0, 40);
    if (n.isEmpty())
        return;
    const PresetValues v = currentPresetValues();
    refreshPresetsFromDisk();   // another instance may have saved presets since this one started
    bool replaced = false;
    for (auto& p : settings_.userPresets)
        if (p.name == n)
        {
            p.values = v;
            replaced = true;
        }
    if (! replaced && settings_.userPresets.size() < 64)
        settings_.userPresets.push_back ({ n, v });
    store_.save (settings_);
}

void ReverseBackProcessor::deleteUserPreset (const juce::String& name)
{
    refreshPresetsFromDisk();
    auto& v = settings_.userPresets;
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const UserPreset& p) { return p.name == name; }), v.end());
    store_.save (settings_);
}

// Several plugin instances (and the standalone) share one settings file: presets saved by another one must
// not be overwritten by this instance's older copy.
void ReverseBackProcessor::refreshPresetsFromDisk()
{
    StoredSettings disk;
    if (store_.tryLoad (disk))
        settings_.userPresets = std::move (disk.userPresets);
}

void ReverseBackProcessor::saveStoredSettings()
{
    refreshPresetsFromDisk();   // presets are only ever changed through saveUserPreset/deleteUserPreset
    store_.save (settings_);
}

void ReverseBackProcessor::rememberFolder (const juce::File& f)
{
    const juce::File dir = f.isDirectory() ? f : f.getParentDirectory();
    if (dir.isDirectory() && dir.getFullPathName() != settings_.lastFolder)
    {
        settings_.lastFolder = dir.getFullPathName();
        saveStoredSettings();
    }
}

// ---- export --------------------------------------------------------------------------------------
ExportSource ReverseBackProcessor::exportSource() const
{
    ExportSource e;
    switch (mode())
    {
        case Mode::Record:
            if (take_ && take_->clip)
            {
                e.source = take_->clip;
                e.selection = take_->effective();
                e.sampleRate = take_->clip->sampleRate();
                e.channels = take_->clip->channels();
                e.suggestedName = "ReverseBack take";
            }
            break;
        case Mode::File:
            if (fileAsset_)
            {
                e.source = fileAsset_->source;
                e.selection = fileSel_;
                e.sampleRate = fileAsset_->sampleRate;
                e.channels = fileAsset_->channels;
                e.suggestedName = fileAsset_->file.getFileNameWithoutExtension() + " reversed";
            }
            break;
        case Mode::Live:
            if (auto chunk = copyFrozenChunk())
            {
                e.sampleRate = chunk->sampleRate();
                e.channels = chunk->channels();
                e.selection = { 0, chunk->frameCount() };
                e.source = std::move (chunk);
                e.suggestedName = "ReverseBack frozen chunk";
            }
            break;
    }
    return e;
}

ExportRequest ReverseBackProcessor::makeExportRequest (const ExportSource& src, const ExportSettings& settings,
                                                       const juce::File& destination, bool overwrite) const
{
    const Settings s = params_.readSettings();
    ExportRequest r;
    r.source = src.source;
    r.selection = src.selection;
    r.direction = mode() == Mode::Live ? Direction::Backward : s.direction;
    r.speed = mode() == Mode::Live ? 1.0 : s.speed;
    r.fadeFrames = s.exactSamples ? 0 : framesFor (s.edgeFadeMs * 0.001, src.sampleRate);
    r.settings = settings;
    r.destination = destination;
    r.overwrite = overwrite;
    return r;
}

// ---- banners -------------------------------------------------------------------------------------
void ReverseBackProcessor::showBanner (BannerKind kind, const juce::String& text, bool retry, bool settings, double autoDismiss)
{
    banner_ = { kind, text, retry, settings, ++bannerId_, autoDismiss };
    bannerShownAt_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    sendChangeMessage();
}

void ReverseBackProcessor::showSavedBanner (const juce::File& saved)
{
    showBanner (BannerKind::Info, "Saved " + saved.getFileName(), false, false, 4.0);
    banner_.reveal = saved;
    rememberFolder (saved);
    sendChangeMessage();
}

void ReverseBackProcessor::dismissBanner()
{
    if (banner_.kind != BannerKind::None)
    {
        if (banner_.kind == BannerKind::Device)
            dismissedDeviceError_ = banner_.text;   // do not bring the same complaint back every tick
        banner_ = {};
        banner_.id = ++bannerId_;
        sendChangeMessage();
    }
}

void ReverseBackProcessor::retryBanner()
{
    const BannerKind k = banner_.kind;
    dismissBanner();
    if (k == BannerKind::Device && host_ != nullptr)
    {
        dismissedDeviceError_ = {};   // after an explicit retry a persisting problem is worth showing again
        host_->retryDevices();
    }
    else if (k == BannerKind::File)
        playFile (true);
}

// ---- events and periodic service -----------------------------------------------------------------
void ReverseBackProcessor::handleEvent (Event&& e)
{
    switch (e.type)
    {
        case EventType::TakeCompleted:
        {
            auto t = std::make_shared<TakeAsset>();
            t->clip = e.clip;
            t->overview = WaveformOverview::build (*e.clip);
            t->id = static_cast<std::uint32_t> (++takeVersion_);
            take_ = t;
            sendChangeMessage();
            break;
        }
        case EventType::TooShort:
            showBanner (BannerKind::Info, "Hold longer to record.", false, false, 3.0);
            break;
        case EventType::NeedSpareTake:
        {
            try
            {
                Command c;
                c.type = CommandType::ProvideSpareTake;
                c.take = AudioClip::create (e.rate, e.channels, e.a);
                post (std::move (c));
            }
            catch (const std::bad_alloc&)
            {
                showBanner (BannerKind::Engine, "Not enough memory to continue Repeat Session.");
            }
            break;
        }
        case EventType::FrozenReady:
            ++frozenVersion_;
            sendChangeMessage();
            break;
        case EventType::Underrun:
            showBanner (BannerKind::File, "Reading the file from disk could not keep up, so playback paused. Press Play to retry.", true);
            break;
        case EventType::Error:
            switch (e.code)
            {
                case ErrorCode::FellBehind: showBanner (BannerKind::Engine, "Audio processing fell behind. Live Reverse was stopped; press Start to try again."); break;
                case ErrorCode::SpareTakeUnavailable: showBanner (BannerKind::Engine, "Repeat Session could not prepare its next recording in time and was stopped."); break;
                case ErrorCode::DspFault: showBanner (BannerKind::Engine, "A processing fault was detected and audio was muted."); break;
                default: break;   // WrongMode / Busy are never user-visible faults
            }
            break;
        case EventType::CaptureCancelled:
        case EventType::PlaybackFinished:
        case EventType::Message:
            break;
    }
}

void ReverseBackProcessor::enforceLoopRepeatExclusivity()
{
    const bool repeat = params_.repeat->load() >= 0.5f;
    const bool looping = juce::roundToInt (params_.loop->load()) != 0;
    if (repeat && looping)
    {
        // Whichever control changed since the last tick loses; if both changed together, Repeat Session loses.
        const bool repeatJustEnabled = ! wasRepeat_;
        const bool loopJustChosen = ! wasLooping_;
        if (repeatJustEnabled && ! loopJustChosen)
            setParam (ids::loop, 0);       // Repeat Session was just enabled: loop turns off
        else
            setParam (ids::repeat, 0);     // a loop pattern was chosen (or both changed): Repeat Session turns off
    }
    wasRepeat_ = params_.repeat->load() >= 0.5f;
    wasLooping_ = juce::roundToInt (params_.loop->load()) != 0;
}

void ReverseBackProcessor::service()
{
    Event e;
    while (engine_.events().poll (e))
    {
        handleEvent (std::move (e));
        e = Event {};
    }
    engine_.events().drainRetired();

    if (const std::uint32_t bits = triggerBits_.exchange (0, std::memory_order_relaxed))
    {
        if (bits & kTrigStart) actionStartStop();
        if (bits & kTrigHoldDown) actionHoldDown();
        if (bits & kTrigHoldUp) actionHoldUp();
        if (bits & kTrigReplay) actionReplay();
        if (bits & kTrigFreeze) actionFreezeResume();
    }
    if (faultFlag_.exchange (false))
        showBanner (BannerKind::Engine, "A processing fault was detected and audio was muted.");

    enforceLoopRepeatExclusivity();

    if (banner_.autoDismissSeconds > 0.0 && juce::Time::getMillisecondCounterHiRes() * 0.001 - bannerShownAt_ > banner_.autoDismissSeconds)
        dismissBanner();

    if (host_ != nullptr)
    {
        const juce::String err = host_->getDeviceError();
        if (err.isEmpty())
        {
            dismissedDeviceError_ = {};
            if (banner_.kind == BannerKind::Device)
                dismissBanner();
        }
        else if (banner_.kind == BannerKind::Device && banner_.text != err)
            showBanner (BannerKind::Device, err, true, true);   // the device message changed
        else if (banner_.kind == BannerKind::None && err != dismissedDeviceError_)
            showBanner (BannerKind::Device, err, true, true);   // never replaces another message, never nags after Dismiss
    }
}

// ---- state ---------------------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> ReverseBackProcessor::getStateAsXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("ReverseBackState");
    xml->setAttribute ("version", 1);
    for (const char* id : persistentParameterIds())
        if (auto* v = apvts.getRawParameterValue (id))
            xml->setAttribute (id, static_cast<double> (v->load()));
    PublishedFile file;
    {
        const std::lock_guard<std::mutex> lock (fileStateMutex_);   // hosts may save from any thread
        file = published_;
    }
    if (file.source != nullptr)   // path and selection only: audio is never stored
    {
        auto* f = xml->createNewChildElement ("File");
        f->setAttribute ("path", file.path.getFullPathName());
        f->setAttribute ("begin", juce::String (static_cast<juce::int64> (file.sel.begin)));
        f->setAttribute ("end", juce::String (static_cast<juce::int64> (file.sel.end)));
    }
    return xml;
}

void ReverseBackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = getStateAsXml())
        copyXmlToBinary (*xml, dest);
}

void ReverseBackProcessor::setStateFromXml (const juce::XmlElement& xml, bool restoreFile)
{
    if (! xml.hasTagName ("ReverseBackState"))
        return;
    for (const char* id : persistentParameterIds())
        if (xml.hasAttribute (id))
            if (auto* p = apvts.getParameter (id))
            {
                const double v = xml.getDoubleAttribute (id);
                if (std::isfinite (v))   // "nan" parses to NaN and would poison every derived frame count
                    p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (v)));
            }
    // Never record, play or arm because a state was loaded.
    Command c;
    c.type = CommandType::Stop;
    commands_.push (std::move (c));

    if (restoreFile)
        if (const auto* f = xml.getChildByName ("File"))
        {
            const juce::File file (f->getStringAttribute ("path"));
            const Selection sel { static_cast<Frame> (std::max<juce::int64> (0, f->getStringAttribute ("begin").getLargeIntValue())),
                                  static_cast<Frame> (std::max<juce::int64> (0, f->getStringAttribute ("end").getLargeIntValue())) };
            juce::MessageManager::callAsync ([w = alive_, this, file, sel]
            {
                if (! *w)
                    return;
                if (file.existsAsFile())
                    loadFile (file, true, sel);
                else
                    showBanner (BannerKind::File, "File not found: " + file.getFileName());
            });
        }
}

void ReverseBackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = xmlFromStateBlob (data, size))
        setStateFromXml (*xml, true);
}
}  // namespace rb

// The entry point every JUCE plugin wrapper (and the standalone player) uses.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rb::ReverseBackProcessor();
}
