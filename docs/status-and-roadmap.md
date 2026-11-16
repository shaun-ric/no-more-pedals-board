# RigBench: status and roadmap (update after each round)

## Working on the rig (owner-tested)
Core audio path, NAM amps with loudness matching, IR, effects, scenes/presets, MIDI switching and learn, setlists, delay compensation, VST3 effect hosting (OldSkoolVerb works), library files, model/IR loading from scene presets (fixed), IR loading.

## Built and syntax-checked, not yet confirmed on the rig
VSTi nodes with note path; delete node preset / scene preset / scene; setlist entry naming; song slots and Stage tab; configurable slot count; MIDI output node (PC/CC rows); output level default -10.5 dB; amp `passthrough` default true; scene-preset save dialog shows the preset's real name.

## Built 2026-11-16, not yet confirmed on the rig (README sections 35-38)
Gapless part changes (with the Setup fallback toggle), preset cache, EQ and delay smoothing, tuner, per-node CPU timing, per-key warnings, dry-leak rule for unlisted connections, IR swap without the 300 ms wait, plugin state in presets, MIDI output port checks. Listen first for clicks on part changes that switch amp captures.

## Built 2026-11-16 on branch feature/master-output-volume, not yet confirmed on the rig (README section 39)
Master output volume per output, outside scenes: Stage channel strips (minimisable) and Setup rows.

## Known issues / unverified
- Output 2 meter anomaly: owner to retest, noting which meter row reads 0/-inf with and without a model.
- Possible transient on re-enabling a capture (amp has no `onActivate` reset). More exposed since part changes stopped muting the outputs (README section 35).
- All nodes mono (stereo deferred). EQ coefficient smoothing done 2026-11-16.
- Pass-through shown false: owner may be on an older build or old preset files; confirm after reinstall.
- Intel Celeron N5095 would lack AVX2 and likely be too slow for NAM; the N97 in the current PC is fine. Always verify with the Diagnostics CPU load on the heaviest scene (Still into You: two amps + cab).
- See `docs/code-review-2026-11-11.md` for the defects found in the code review (A1-A8) and the suggested order. A1-A3 fixed on 2026-11-15 (README section 32): syntax-checked and harness-tested, not yet confirmed on the rig. A4-A8 addressed on 2026-11-16 (README section 37); A7 is a warning, not silence, on purpose.

## Roadmap
1. Update the Requirements doc (Claude Docs "Live Guitar Rig Host: Requirements and Architecture", project id 2bed332c-8fe9-421d-be22-795bd1630f2f) for: delay compensation, VST3/VSTi hosting, files in presets, deleting, slots/Stage tab, MIDI output, scene consolidation approach.
2. UI and preset-handling fixes the owner is collecting (rename setlist entries exists under Setlist > Rename...; consider double-click).
3. Real SY-200 patch numbers/channel on the MIDI-out presets; pick the VSTi plugin for Since U Been Gone.
4. Audition the library and tune captures (the owner picks; agents cannot hear). 96 AC30 and 6 Twin captures were added but only one bright and one noon AC30 and one Twin are used.
5. Stage hardening. Built 2026-11-15 (README sections 33-34; syntax-checked and harness-tested, not yet confirmed on the rig): audio device reconnection, device settings saved on change, start on the Stage tab at the last song, footswitch debounce, event log, Windows setup checklist. Still to do: 60-minute soak test with a Duo unplug (owner), engine/GUI process split (a crashing plugin still takes the app down), optional full-screen start.
6. Smaller features: clear a single slot binding, scene-level slot defaults UI, stereo edges.
7. CI: GitHub Actions validates every pull request and builds the Windows exe after the owner approves (README section 31). Working: the first approved build succeeded (owner-confirmed 2026-11-15).
8. v2 ideas: Linux port, single enclosure, ARM benchmark.

## Per-round checklist
Reproduce or locate the cause; make the change on a branch; syntax-check; update README section; regenerate and validate library files if presets are affected; open a pull request with changes, tests and risks; update this file.
