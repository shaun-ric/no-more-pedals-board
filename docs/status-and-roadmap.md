# RigBench: status and roadmap (update after each round)

## Working on the rig (owner-tested)
Core audio path, NAM amps with loudness matching, IR, effects, scenes/presets, MIDI switching and learn, setlists, delay compensation, VST3 effect hosting (OldSkoolVerb works), library files, model/IR loading from scene presets (fixed), IR loading.

## Built and syntax-checked, not yet confirmed on the rig
VSTi nodes with note path; delete node preset / scene preset / scene; setlist entry naming; song slots and Stage tab; configurable slot count; MIDI output node (PC/CC rows); output level default -10.5 dB; amp `passthrough` default true; scene-preset save dialog shows the preset's real name.

## Known issues / unverified
- Output 2 meter anomaly: owner to retest, noting which meter row reads 0/-inf with and without a model.
- Possible transient on re-enabling a capture (amp has no `onActivate` reset).
- EQ coefficient smoothing not done; all nodes mono (stereo deferred).
- Pass-through shown false: owner may be on an older build or old preset files; confirm after reinstall.
- Intel Celeron N5095 would lack AVX2 and likely be too slow for NAM; the N97 in the current PC is fine. Always verify with the Diagnostics CPU load on the heaviest scene (Still into You: two amps + cab).
- See `docs/code-review-2026-11-11.md` for the defects found in the code review (A1-A8) and the suggested order.

## Roadmap
1. Update the Requirements doc (Claude Docs "Live Guitar Rig Host: Requirements and Architecture", project id 2bed332c-8fe9-421d-be22-795bd1630f2f) for: delay compensation, VST3/VSTi hosting, files in presets, deleting, slots/Stage tab, MIDI output, scene consolidation approach.
2. UI and preset-handling fixes the owner is collecting (rename setlist entries exists under Setlist > Rename...; consider double-click).
3. Real SY-200 patch numbers/channel on the MIDI-out presets; pick the VSTi plugin for Since U Been Gone.
4. Audition the library and tune captures (the owner picks; agents cannot hear). 96 AC30 and 6 Twin captures were added but only one bright and one noon AC30 and one Twin are used.
5. Stage hardening: boot straight into the app, Windows lockdown, audio device reconnection, watchdog, logging, 60-minute soak test, engine/GUI process split.
6. Smaller features: clear a single slot binding, scene-level slot defaults UI, stereo edges.
7. CI: GitHub Actions validates every pull request and builds the Windows exe after the owner approves (README section 31). The build job has not run yet; the first run confirms the ASIO SDK download and the MSVC build.
8. v2 ideas: Linux port, single enclosure, ARM benchmark.

## Per-round checklist
Reproduce or locate the cause; make the change on a branch; syntax-check; update README section; regenerate and validate library files if presets are affected; open a pull request with changes, tests and risks; update this file.
