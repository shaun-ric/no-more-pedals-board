# RigBench: architecture and formats

## Hardware and signal path
- PC: GMKtec mini PC, Intel N97 (AVX2), Windows 11, M-Audio Duo via ASIO. Earlier benchmark at 64-sample buffer: 26.7% mean / 54.1% peak CPU with the baseline graph; all effects on 27.4% mean / 51.6% peak.
- Input 1: guitar via an external A/B switch (Les Paul-type humbuckers, or Tele-type P-90s). Input 2: Boss SY-200 return.
- Output 1: FOH, through a cab IR. Output 2: a power amp and real guitar cab, no IR.
- Controllers: Behringer FCB1010 and a MIDI Captain (2x4 = 8 buttons, bottom row preferred). Single global MIDI channel. The MIDI Captain's switcher sends notes to a VSTi node for the song that needs it.

## Code layout (Source/)
- `Core/Param.h`: parameter registry (stable IDs, flags `hidden`, `structural`, `persist`, `customUi`).
- `Core/Node.h`: node interface; every node has an `active` param. Virtuals include `latencySamples`, `acceptsMidi/setMidiIn`, text properties, `presetApplied`, `processInactive`.
- `Core/Graph.*`: ordered plan, edges with level and enable (gain ramps), per-edge delay lines for latency compensation, `NoteBus` feeding instrument nodes.
- `Core/Rig.*`: control thread. Commands applied off the audio thread; `loadNodeFile`, `loadPresetFile` (skips if same path), `applyScenePreset` (loads preset files, then settings, then `presetApplied`), nested `fade()`.
- `Core/Library.*`: reads/writes scenes, presets, setlists, assets; `Library::slug` for ids; `lib:` file references relative to the library root.
- `Nodes/`: input, output, NAM amp (loudness matching, input calibration, `passthrough`), IR, gate, compressor, overdrive, chorus, delay, reverb, eq3, eqshelf, eq10, VST3 effect, VSTi, MIDI out; `NodeFactory.cpp` registers types.
- `MidiRouter.*`: bindings (in scene files), global bindings in `midi-map.json` (`setlist:next/prev`, `slot:N`), note/pitch bend forwarding to instruments.
- `SceneManager.*`, `SetlistManager.h`, `MainComponent.*`: tabs Play, Stage, Edit, Setlist, Setup, Diagnostics; `SlotsPanel.h`, `StageView.h`, `PluginWindow.h`, `NodesPanel.h`, `GraphView.h`.
- Libraries: NeuralAmpModelerCore (third_party), JUCE 8.0.6 via CMake, optional ASIO SDK (`-DASIO_SDK_DIR`).

## Library layout (Documents\RigBench)
- `NAM/`, `IR/`: user model/IR folders (asset folders are exactly `NAM` and `IR`).
- `scenes/<scene>.scene.json`: `{name, pc, midi?, graph{nodes[{id,type,name,active,props,file?,text?,x?,y?,colour?}], edges[{from,to,enabled}]}}`
- `scenes/<scene>/presets/<id>.preset.json`: scene preset `{version,id,name,slot,nodes[{node,preset,active}],edges[{edge:"from>to",enabled,level}]}`; `slot` is unique per scene.
- `presets/<nodeType>/<id>.preset.json`: node preset `{version,id,name,nodeType,slot,values{},file?}`; `file` is `lib:NAM/...` or `lib:IR/...`.
- `setlists/<id>.setlist.json`: `{version,name,entries[{scene,preset,name,slots[8]}]}`; slot i = preset id for button i+1.
- `midi-map.json`: global bindings.

## Behaviours that bite
- A scene preset only lists nodes it uses; unlisted nodes return to defaults and switch off. Defaults therefore matter (e.g. `passthrough` is true by default).
- Output level is a node param, not persisted by default, but is stored in node presets ("Output -10.5 dB").
- Preset id is a slug (`teenage-dirtbag-verse-clean`); the display name lives in `name`.
- NAM amp node: gain character comes from the capture; loudness matching (default target -18 dB) and input calibration against `input_level_dbu`.
- Model/IR loads are slow: they happen on the control thread inside a fade. A preset naming the file already loaded costs nothing.
- Windows macro clashes (e.g. `interface`) have caused compile errors before; avoid such identifiers.

## Pop-Punk Set library (generated)
- Generator scripts (`gen*.py`, Python) produce 9 scenes for 24 songs: `hi-gain-rect`, `crunch-rect`, `marshall`, `ac30-pop`, plus single scenes for the SY-200 / VSTi / acoustic songs (`sk8er-boi`, `still-into-you`, `i-don-t-care`, `just-a-girl`, `since-u-been-gone`, `teenage-dirtbag`).
- Each part is a scene preset; first part on button 5 (bottom row). Setlist entry names carry `[HB]` or `[P90]`.
- SY-200 patch numbers (10-13) are placeholders; the VSTi plugin for Since U Been Gone is not chosen.

## Tools
See `tools/README.md`. The Pop-Punk library is regenerated with `tools/gen_poppunk_library.py` and checked with `tools/validate_library.py`.
