# Implementation status — ReverseBack 0.1.0-beta.1

This document says what is implemented, what evidence exists for each acceptance criterion, and what has **not** been verified. "Verified" always means an automated test or a command that was run; nothing here is claimed from reading code alone.

## 1. Summary

| Area | State |
| --- | --- |
| Engine (Record & Reverse, Live Reverse, Reverse File, resampler, limiter, triggers, presets, disk-backed clips) | Implemented, framework-free (`Source/Core`) |
| JUCE layer (processor, 24 parameters, state, file import, export, settings) | Implemented |
| GUI (three modes, advanced drawer, waveform, sheets, presets menu, shortcuts, drag and drop) | Implemented, screenshots in `docs/screenshots` |
| Formats | Standalone, VST3, CLAP (Linux x86_64 built and validated); Windows defined in CI only |
| Tests | 86 core tests (221 323 checks) and 50 integration tests (22 277 checks) |
| Sanitizers | Core suite under ASan+UBSan and under TSan; the whole JUCE integration suite (GUI, processor, plugin, IO) under ASan+UBSan |
| Plugin validation | pluginval at strictness 5 on the VST3: **SUCCESS**; scripted CLAP host probe: **PASS** |

## 2. Acceptance criteria (spec/REVERSEBACK_V1.md section 7)

| ID | Evidence (test names; `Tests/Core` = no JUCE, `Tests/Integration` = JUCE) | Status |
| --- | --- | --- |
| A01 | `A01_reverse_1234`, `A01_stereo_channels_keep_identity` | Verified |
| A02 | `A02_A03_timeline_is_exact_for_every_block_size`, `A02_zero_wait_starts_playback_on_the_next_sample` | Verified |
| A03 | same test, block sizes 1, 64, 127, 256, 512, 1024 and more; also `P06_P11_timeline_is_identical_for_any_block_size_and_in_offline_mode` through the processor | Verified |
| A04 | `A04_live_first_output_and_chunk_order`, `live_zero_delay_chunks_play_back_to_back` | Verified |
| A05 | `A05_sixty_minute_run_has_bounded_storage_and_no_missing_chunks` (simulated clock, 60 minutes of audio, no real-time wait), `A05_forced_slot_shortage_stops_with_fell_behind_and_silence` | Verified (simulated time) |
| A06 | `A06_stop_in_every_live_state_silences_without_later_audio`, `A06_stop_in_every_state_silences_and_preserves_previous_take`, `A06_stop_during_playback_fades_within_stop_fade`, `rapid_live_restart_does_not_cut_old_tail_or_leak_into_new_run` | Verified |
| A07 | `A07_input_ignored_during_wait_and_playback_and_repeat_starts_fresh_capture` | Verified |
| A08 | `A08_hold_release_short_tap_key_repeat_and_limit`, `A08_hold_stop_and_key_repeat_at_processor_level`, `typing_and_repeat_keys_never_start_or_multiply_recordings` | Verified |
| A09 | `A09_freeze_adopts_complete_chunk_at_output_boundary_and_repeats`, `A09_freeze_before_output_starts_loops_immediately`, `A09_frozen_chunk_is_exportable_via_storage`, `live_actions_freeze_and_frozen_chunk_export` | Verified |
| A10 | `A10_output_length_is_round_N_over_ratio`, `A10_import_wav_aiff_flac_at_44k1_48k_96k_192k_mono_and_stereo`, `A10_export_length_rate_and_speed_follow_the_playback_mapping` | Verified |
| A11 | `A11_double_reversal_reconstructs_original`, `A11_A20_float_export_is_exact_reversal_and_preserves_over_unity` | Verified |
| A12 | `A12_fades_keep_duration_and_shape_edges`, `A12_tiny_selection_and_fade_larger_than_clip` | Verified |
| A13 | `A13_no_input_device_shows_inline_error_and_file_mode_still_works`, `A13_device_stop_or_rate_change_stops_transport_and_keeps_takes` | Verified with simulated devices; **not** with unplugging real hardware |
| A14 | `A14_import_rejects_bad_files_with_typed_errors` (incl. truncated WAV), `A14_cancelled_load_returns_cancelled_and_leaves_no_cache`, `A14_disk_backed_file_reads_back_exactly_and_cleans_up`, `A14_disk_source_miss_returns_false_and_prefetch_recovers`, `A14_player_pauses_on_underrun_keeps_position_and_resumes_after_prime`, `A14_export_failures_never_touch_the_destination_or_leave_temp_files`, `A14_failed_load_keeps_the_previous_file_and_selection_rules_hold` | Verified |
| A15 | `allocation_free_process`, `record_process_does_not_allocate_in_any_state`, `live_process_does_not_allocate_in_any_state`, `A15_random_command_fuzz_keeps_output_finite_and_never_allocates_on_audio_path`, `A15_threaded_control_and_audio_with_rapid_asset_replacement`, `spsc_queue_preserves_order_across_threads`, `seqlock_readers_never_see_torn_snapshots`; ASan/UBSan and TSan runs | Verified (allocation hook counts `operator new` on the audio path; it cannot see malloc/locks inside third-party code) |
| A16 | `A16_every_main_and_advanced_control_changes_the_intended_property`, `A16_actions_drive_the_engine_through_real_parameters`, `typing_and_repeat_keys_never_start_or_multiply_recordings` | Verified (headless under Xvfb) |
| A17 | `A17_presets_match_the_spec_and_surprise_is_bounded`, `A17_presets_surprise_and_state_restore_never_record_or_touch_gain`, `A17_settings_roundtrip_corruption_and_future_versions` | Verified |
| A18 | `A18_voice_trigger_requires_sustained_crossing`, `A18_voice_trigger_includes_preroll_and_total_length_is_exact`, `A18_armed_can_be_cancelled_and_short_arming_limits_preroll` | Verified with synthetic signals |
| A19 | `A19_trim_selection_pads_clamps_and_handles_silence`, `take_selection_trim_and_undo_apply_to_replay` | Verified |
| A20 | `A11_A20_float_export_is_exact_reversal_and_preserves_over_unity`, `A20_pcm_export_refuses_to_clip_unless_normalised_and_normalise_hits_minus_1dBFS`, `A20_export_level_does_not_depend_on_output_volume` | Verified |

## 3. Plugin criteria (spec/PLUGIN_FORMATS.md section 9)

| ID | Evidence | Status |
| --- | --- | --- |
| P01 | `P01_bus_layouts_mono_and_stereo_accepted_others_rejected`, `mono_input_and_mono_output_layouts_process_correctly` | Verified |
| P02 | `P02_parameters_match_the_published_table`; CLAP probe reads 24 parameters | Verified |
| P03 | `P03_state_round_trip_contains_no_audio_and_excludes_monitor_and_triggers` | Verified |
| P04 | `P04_restoring_state_with_arm_or_trigger_flags_never_starts_capture`, `trigger_parameters_act_on_rising_edges_only` | Verified |
| P05 | `P05_missing_file_in_state_leaves_file_slot_empty_with_a_message` | Verified |
| P06 | `P06_P11_timeline_is_identical_for_any_block_size_and_in_offline_mode` | Verified |
| P07 | `P07_sample_rate_change_keeps_take_and_plays_it_at_the_new_rate` | Verified |
| P08 | `P08_bypass_cancels_capture_keeps_the_previous_take_and_passes_audio_through` | Verified |
| P09 | `P09_reported_latency_is_zero_in_plugin_builds` | Verified |
| P10 | pluginval strictness 5 on `ReverseBack.vst3`: SUCCESS (19 test groups, log kept by `scripts/validate-linux.sh`); `scripts/clap_probe.cpp` loads `ReverseBack.clap`, checks entry/descriptor/24 params/ports/state and processes audio: PASS; exported symbols are only the format entry points | Verified on Linux; **no commercial DAW has loaded either plugin** |
| P11 | same test as P06 (offline render equals block-wise run) | Verified |
| P12 | `P12_editor_opens_resizes_and_closes_repeatedly` | Verified under Xvfb |

## 4. Tooling runs

| Check | Result |
| --- | --- |
| Core tests, Release | 86 / 86 pass |
| Core tests, ASan + UBSan | 86 / 86 pass |
| Core tests, TSan | 86 / 86 pass |
| Integration tests, Release (`xvfb-run`) | 50 / 50 pass |
| Integration tests, ASan + UBSan (JUCE build, `xvfb-run`) | 50 / 50 pass, no reports |
| pluginval strictness 5 (VST3) | SUCCESS |
| CLAP probe | PASS |
| `ldd` on every binary | nothing missing; only ALSA, X11, FreeType, fontconfig, GL, libc/libstdc++ |
| GitHub Actions on the PR head | Linux build + tests + validation + packaging, core release / ASan+UBSan / TSan: green. Windows job: first run failed at configure (runner generator), fixed, re-run pending |
| Standalone `--selftest` (offline engine check without display or device) | PASS |
| Debian package builds (`dpkg-deb`), per-user installer and uninstaller | Exercised in a scratch HOME |

## 5. Not verified (please test)

- **Real audio hardware.** The build environment has no sound card or microphone. Latency, glitching, device hot-plug and rate-change behaviour on real ALSA/JACK/PipeWire devices are untested; the engine is exercised through simulated callbacks only.
- **Windows.** The code avoids POSIX-only constructs and `.github/workflows/ci.yml` defines an MSVC job, but that job has not run. No Windows binary exists yet.
- **macOS.** Out of scope for the beta.
- **DAWs.** No Reaper/Bitwig/Ardour or other host has loaded the plugins. pluginval and the CLAP probe are the only host-side checks.
- **GUI on a real desktop.** Layout and behaviour were checked under Xvfb with screenshots; high-DPI scaling, Wayland and the platform file/colour dialogs have not been tried.
- **Long soak.** The 60-minute Live run (A05) uses a simulated clock; no 60-minute real-time run was made.
- **CI.** The workflow file has not been executed by GitHub yet.

## 6. Known limitations (by design for the beta)

- The standalone is the only place with a limiter, input monitoring and an output volume stage; the plugins pass levels unchanged and report zero latency.
- Disk-backed (large) files are played from a prefetching cache; random access into a cold region pauses playback with an "underrun" status until the cache is primed rather than blocking the audio thread.
- Supported formats are WAV, AIFF and FLAC, mono or stereo, up to 192 kHz and 30 minutes. MP3/AAC are future work.

## 7. Pre-release audit

Three independent reviewers read the code (real-time safety and threading; DSP and transport correctness; file I/O, processor, GUI and standalone). Each finding was treated as a claim: reproduced or traced, fixed, and covered by a regression test that fails on the old code. An ASan build of the GUI/plugin tests also found one bug by itself (editor destruction order). About 50 defects were fixed; the test counts above include the 30+ regression tests added for them.

**Fixed (with regression tests)**

| Area | Defect | Effect before the fix |
| --- | --- | --- |
| Real-time | Commands the engine rejected (double Start, wrong mode, bypass) were freed on the audio thread | Up to 88 MiB `free()` in the audio callback |
| Real-time | Retire/event queues dropped or freed on overflow | Lost `TakeCompleted`; audio-thread frees when the UI thread stalled |
| Threading | `prepareToPlay` could run while `processBlock` was running (VST3/CLAP hosts) | Races on queues/snapshot, use-after-free reproduced under ASan, possible hang |
| Threading | `getStateInformation` read the file state while the UI thread replaced it | Possible use-after-free in a host save |
| Export | Exporting any disk-backed file (over 256 MiB decoded) failed on the first cache miss | "Save WAV" always failed for long files |
| Export | An existing file could be replaced without the Replace question when PCM would also clip; replace was unlink + rename | Silent overwrite / lost file on failure |
| Export | Decision buttons destroyed their own click handler | Use-after-free on every clip/replace choice |
| Export | Question layout overlapped; Cancel left the sheet disabled | Unusable dialog states |
| Engine | Repeat Session stayed armed after a cancel or re-prepare; stale/undersized spare buffers were used | Microphone capture started by itself; short takes |
| Engine | Finish Early under 50 ms cancelled the capture (spec: ignored); undersized take buffers truncated silently | Lost takes |
| Engine | Ping-pong leaked its flipped direction; direction/speed/restart requests near a pass end were lost; selection change cut the audio | Wrong direction, dead controls, clicks |
| Engine | Limiter release stalled at 0.9999 forever and could exceed the ceiling by rounding | Permanent -0.001 dB, "never exceeds -1 dBFS" false |
| Engine | Live Stop/Resume tails ignored the chunk edge fades; fade changes landed mid-chunk | Clicks |
| Engine | Missing second input plane crashed the engine | Crash for a malformed host buffer |
| Disk cache | Prefetch thread spun at 100 % CPU when a block could not be loaded; condvar notify on the audio thread | Burned a core; priority inversion |
| Disk cache | Cleanup deleted live caches (Windows always, Linux by age) and never ran in plugins | Playback underruns / leaked caches |
| Processor | A file loaded while no device ran was shown but never reached the engine; Start pressed twice queued two takes; bypass left stale UI state and piled up commands; a trigger parameter left high started recording after a device restart | "Play does nothing", phantom recordings |
| Input hardening | NaN in state/settings, 100 000-deep JSON/XML, decoder exceptions | Crash on hostile or corrupt files |
| Settings | Several instances overwrote each other's presets; a newer settings file was rewritten and lost fields | Lost presets |
| UI | Volume control had zero width at the 820 px minimum; Shift+Arrow never worked; shortcut keys fired on OS key repeat; Tab left a sheet; the device banner replaced other messages every 33 ms; clock rounding, junk time input, arrow steps, toast time, hold key conflicts, accessible names | See `AuditTests`/`GuiTests` |

**Found by the sanitizer run, not by review:** the editor destroyed a slider before the parameter attachment that listened to it (use-after-free when any host closes the window).

**Known and accepted for the beta**

- Playing a disk-backed file waits up to 400 ms on the message thread for the first cache blocks; stopping a load or export joins its worker (bounded, 10 s).
- Take memory budget counts only the retained take plus the one being requested; the Start gate keeps the number of queued takes to one.
- The Advanced drawer remembers its tab globally rather than per mode; "follow system" reduced motion reads only the `REDUCE_MOTION` environment variable.
- Segmented controls expose no accessibility role (labels are set on buttons and number fields); the meter description updates at the UI rate.
- A truncated FLAC cannot be told from a complete one (the decoder zero-fills); truncated WAV/AIFF are detected.
- Unreferenced leftovers: `LevelFollower` (dead code), `CaptureCancelled` reason code (unused).
