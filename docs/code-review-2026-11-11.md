# RigBench: code review, 2026-11-11

Scope: `source/Source` (Core, Nodes, AudioEngine, NamProcessor, MidiRouter, SceneManager, MainComponent, Library, StageView, SetlistManager), `CMakeLists.txt`, and `tools/`. Everything below comes from reading the code. Nothing was compiled, run or heard. Items marked "hypothesis" need measuring on the rig.

## A. Defects (fix first)

1. **Saving a scene preset can swap an amp's model.** `Rig::saveScenePreset` reuses any library node preset whose values match (`sameValues`) and ignores the preset's `file`. All generated amp presets at target -18 / input 0 share the same values, so an amp saved from the app links to the lowest-slot one (a Rectifier capture). On recall, `loadPresetFile` loads that capture into the node. Related: `saveNodePreset` and the new-preset branch never store `file` (`currentFileRef` is declared in `Rig.h` but not defined or used), so presets saved in the app never carry their model/IR.
   Fix: store the node's current file in saved presets, and include it in the match.
2. **A scene file that fails to parse is overwritten.** `Library::readGraphDef` returns false both for "no graph key" and for invalid JSON. `SceneManager::selectScene` then writes the default graph, and `updateJsonFile` starts from an empty object, so name, pc, MIDI bindings and graph are lost. `MidiRouter::loadScene` + `ensureDefaults` + `saveScene` does the same. The generated MIDI map text invites hand edits of these files.
   Fix: distinguish parse failure from a missing key, never write after a parse failure, report it, keep a `.bak`.
3. **SY-200 return is silent in the generated library.** `InputNode::processInactive` clears the buffer, `applyScenePreset` switches off every node a preset does not list, and `gen_poppunk_library.py` never lists `in2`.
   Fix: `on('in2')` for parts with `sy`.
4. **Dry guitar can leak after adding an amp to an existing scene.** Older presets do not list the new node or its edges. The node goes inactive with `passthrough` reset to its default (true), and unlisted edges reset to their default (enabled), so raw DI is summed into the chain. The comment in `AmpNodes.h` still says a switched-off capture is silent by default.
   Fix options: unlisted edges default to off when either end is unlisted; or passthrough defaults true only for pedal captures (gear type from NAM metadata, or a separate node type).
5. **Missing-file warnings can vanish.** In `Rig::applyGraph`, each successful `loadNodeFile` calls `setStatus({})`, wiping a "file not found" from an earlier node. `MidiOutNode::presetApplied` discards its problem string, so a failed SY-200 patch change is invisible.
   Fix: keep a list of current problems per node rather than one string; show the count on the status strip.
6. **IR swap waits the full 300 ms when the new IR has the same length.** `IrNode::loadFile` polls `getCurrentIRSize() != oldSize`; IRs from one pack are usually the same length. The outputs are faded for the whole wait.
7. **An active amp with no model passes the raw DI through** (`NamAmpNode::process`, also while the try-lock fails). With a missing capture on stage this sends unprocessed guitar into the cab IR. Consider silence plus a visible warning.
8. **VST plugin state is not saved.** Presets hold only up to 64 automatable parameters, addressed by index (`p<i>`). A VSTi's chosen patch or sample set is not recalled. Store `getStateInformation` (base64) in the node preset.

## B. Robustness

- No audio device recovery: `audioDeviceStopped` is empty, there is no `audioDeviceError` handler, and nothing reopens the device. The status strip shows "No audio device" and stays there. Add a retry every 2 s from the saved device state.
- `device.xml` is written only in `~MainComponent`. Pulling power after changing the device loses the setting. Save on change.
- `MidiOutNode` keeps a stale `MidiOutput` after the device is unplugged; reopen on failure or on device-list change.
- Startup: opens on the Edit tab, selects the `default` scene, and does not go to a setlist entry. For stage use: open on Stage, go to the first (or last played) setlist entry, optional full screen.
- No debounce on `setlist:next/prev` or slot presses; a bouncing footswitch skips a song. Ignore repeats within ~150 ms.
- `NoteBus` drops events silently when full; a lost note-off is a stuck note. `blockMidi.ensureSize(4096)` is smaller than a full drain (1024 events), so a burst can allocate on the audio thread.
- `Graph::releaseRetired` relies on `sleep(60)`; correct in practice, but an audio-thread block counter would make it certain.
- No event log: status messages, xruns, device changes and scene/preset switches with timestamps would make post-gig diagnosis possible. The CSV only has CPU numbers.
- NAM core is not pinned (`third_party` checkout); `NamProcessor.cpp` depends on a recent API (`NAM/slimmable.h`). Pin a commit (submodule). JUCE is fetched at configure time; fine, but needs network on a fresh build. *(Done 2026-11-12: submodule pinned at `d3189ed`.)*
- `File::replaceWithText` already writes through a temporary file, so saves are atomic. No change needed there.

## C. Efficiency

- **Every scene preset change mutes the outputs.** `Rig::loadScenePreset` always does `fade(true)` (40 ms sleep), applies, then a 20 ms fade-in, plus JSON reads inside the mute (`readScenePreset`, one `readNodePreset` per node, and a full folder parse for slot recalls). Part changes inside a song therefore have a gap of 60 ms or more. Nodes and edges already crossfade on their own. Suggested: when no file needs loading, skip the global fade; compute the target value of every parameter first and write only the differences (no reset-then-set); cache parsed presets in memory per scene.
- **The GUI timer re-parses the library four times a second.** `MainComponent::updateStage` runs on every tick and calls `listScenePresets` (once, plus once per assigned slot) and `listScenes`. That is on the order of 50 preset files and every scene file per tick, permanently. Cache in `Library` keyed by `revision`, or only rebuild when scene, preset, setlist index or revision changes. Hypothesis: this contributes to the CPU peaks on a 4-core machine.
- **Prepared block size is 2048 for a 64-sample device** (`bufferCapacity = max(bs * 2, 2048)`). `juce::dsp::Convolution` sizes its FFT from the prepared block size, so each IR does large FFTs per 64-sample callback. Hypothesis: preparing the convolution with the real block size (or JUCE's non-uniform mode) lowers IR cost and the peak-to-mean ratio. Measure before changing.
- Per-node timing in Diagnostics (mean and peak ns per node) would answer both hypotheses and show which node causes the 54% peak.
- Song changes load files one at a time, each with its own fade. Wrap the graph change, the file loads and the first preset in one fade, and build the next setlist entry's models in the background so the change is immediate.
- Already in place: AVX2, NAM fast tanh, float samples, denormal protection, inactive nodes skipped, disabled edges skipped.

## D. Usability

- Tuner with output mute on a footswitch.
- Tap tempo; BPM per setlist entry with delay time as a note division (the generator hard-codes milliseconds).
- A global HB/P90 input trim instead of per-preset `inp`; show the guitar tag large on Stage.
- Stage tab: larger warning state (missing file, no audio device, xruns) than the bottom strip; "next song" is already there.
- Preflight button: check every setlist entry's scene, presets, files, plugins and MIDI ports before a gig (the in-app version of `validate_library.py`).
- Unsaved-changes marker on the scene preset; library export/restore as a zip.

## E. Tools

- `validate_library.py`: prints missing files but still ends with "ok"; looks up node presets by id across all types; does not check setlists, preset ids against file names, or that used inputs are active (would have caught A3).
- `syntax_check.sh`: exit status is lost in the `grep` pipe.
- `test.cpp`, `t2.cpp`, `t3.cpp`: print values, no assertions.
- `gen_poppunk_library.py`: `NORM_AMP`/`IRFIX` reduce the output to 8 of 21 amps and 4 of 8 IRs, so the per-song choices in the `S(...)` blocks are not what is written; a song with more than four parts loses the extras without warning; output overwrites hand edits.
- A Windows CI build (MSVC) would catch what the g++ syntax check cannot and produce the exe.

## Suggested order
A1, A2, A3 (data and sound correctness), then B device recovery, then C preset-change gap and library caching, then A4-A8, then D.
