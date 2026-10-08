// Loads a .clap file the way a host does and exercises it: entry, factory, descriptor, parameters,
// audio ports, activate, one processed block (must be finite), deactivate, destroy.
// Build:  g++ -std=c++17 scripts/clap_probe.cpp -I<clap>/include -ldl -o clap_probe
// Usage:  clap_probe /path/to/ReverseBack.clap      (exit code 0 = all checks passed)
#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

static int failures = 0;
#define EXPECT(cond, msg)                                         \
    do                                                            \
    {                                                             \
        if (cond)                                                 \
            std::printf ("  ok    %s\n", msg);                    \
        else                                                      \
        {                                                         \
            std::printf ("  FAIL  %s\n", msg);                    \
            ++failures;                                           \
        }                                                         \
    } while (0)

static const void* hostGetExtension (const clap_host_t*, const char*) { return nullptr; }
static void hostNoop (const clap_host_t*) {}

static uint32_t inSize (const clap_input_events_t*) { return 0; }
static const clap_event_header_t* inGet (const clap_input_events_t*, uint32_t) { return nullptr; }
static bool outPush (const clap_output_events_t*, const clap_event_header_t*) { return true; }

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s plugin.clap\n", argv[0]);
        return 2;
    }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    EXPECT (lib != nullptr, "dlopen");
    if (lib == nullptr)
    {
        std::printf ("  %s\n", dlerror());
        return 1;
    }
    auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
    EXPECT (entry != nullptr, "exports clap_entry");
    if (entry == nullptr)
        return 1;
    EXPECT (clap_version_is_compatible (entry->clap_version), "clap version compatible");
    EXPECT (entry->init (argv[1]), "entry.init");

    auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    EXPECT (factory != nullptr, "plugin factory");
    EXPECT (factory != nullptr && factory->get_plugin_count (factory) == 1, "exactly one plugin");
    const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor (factory, 0);
    EXPECT (d != nullptr, "descriptor");
    std::printf ("  id=%s name=%s vendor=%s version=%s\n", d->id, d->name, d->vendor, d->version);
    EXPECT (std::strcmp (d->id, "labs.circuitdrift.reverseback") == 0, "id matches the published id");
    bool audioEffect = false;
    for (const char* const* f = d->features; f != nullptr && *f != nullptr; ++f)
        audioEffect = audioEffect || std::strcmp (*f, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) == 0;
    EXPECT (audioEffect, "feature: audio-effect");

    clap_host_t host {};
    host.clap_version = CLAP_VERSION;
    host.name = "clap_probe";
    host.vendor = "ReverseBack";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostNoop;
    host.request_process = hostNoop;
    host.request_callback = hostNoop;

    const clap_plugin_t* plugin = factory->create_plugin (factory, &host, d->id);
    EXPECT (plugin != nullptr, "create_plugin");
    if (plugin == nullptr)
        return 1;
    EXPECT (plugin->init (plugin), "plugin.init");

    auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    EXPECT (params != nullptr, "params extension");
    if (params != nullptr)
    {
        const uint32_t n = params->count (plugin);
        std::printf ("  %u parameters\n", n);
        EXPECT (n >= 24, "at least the 24 published parameters");
        bool names = true;
        for (uint32_t i = 0; i < n; ++i)
        {
            clap_param_info_t info {};
            names = names && params->get_info (plugin, i, &info) && info.name[0] != 0;
        }
        EXPECT (names, "every parameter has a name");
    }
    auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
    EXPECT (ports != nullptr && ports->count (plugin, true) == 1 && ports->count (plugin, false) == 1, "one audio input and one audio output");
    EXPECT (plugin->get_extension (plugin, CLAP_EXT_STATE) != nullptr, "state extension");

    EXPECT (plugin->activate (plugin, 48000.0, 32, 512), "activate 48 kHz");
    EXPECT (plugin->start_processing (plugin), "start_processing");

    std::vector<float> inL (512, 0.25f), inR (512, 0.25f), outL (512, 9.0f), outR (512, 9.0f);
    float* inCh[2] = { inL.data(), inR.data() };
    float* outCh[2] = { outL.data(), outR.data() };
    clap_audio_buffer_t in {}, out {};
    in.channel_count = 2;
    in.data32 = inCh;
    out.channel_count = 2;
    out.data32 = outCh;
    clap_input_events_t ie { nullptr, inSize, inGet };
    clap_output_events_t oe { nullptr, outPush };
    clap_process_t proc {};
    proc.frames_count = 512;
    proc.audio_inputs = &in;
    proc.audio_inputs_count = 1;
    proc.audio_outputs = &out;
    proc.audio_outputs_count = 1;
    proc.in_events = &ie;
    proc.out_events = &oe;
    clap_process_status st = CLAP_PROCESS_ERROR;
    for (int i = 0; i < 8; ++i)
        st = plugin->process (plugin, &proc);
    EXPECT (st != CLAP_PROCESS_ERROR, "process returns a valid status");
    bool finite = true, silent = true;
    for (int i = 0; i < 512; ++i)
    {
        finite = finite && std::isfinite (outL[i]) && std::isfinite (outR[i]);
        silent = silent && outL[i] == 0.0f && outR[i] == 0.0f;
    }
    EXPECT (finite, "output is finite");
    EXPECT (silent, "idle plugin is silent (wet only, nothing starts by itself)");

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    entry->deinit();
    std::printf (failures == 0 ? "CLAP probe: PASS\n" : "CLAP probe: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
