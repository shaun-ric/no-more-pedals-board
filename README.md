# RigBench: Phase 1 benchmark

A small JUCE app that answers one question: **can the GMKtec mini PC (Intel N97) run a NAM A2 amp model plus a cab IR on two outputs, with safe CPU headroom and low latency, through the M-Audio Duo?**

```
guitar -> gate -> compressor -> overdrive -> amp -> chorus -> delay -> reverb -+-> cab IR -> Output 1 (FOH)
                                             |                                 |
                                             +--(dry amp, off by default)------+-> Output 2 (stage cab)
```

This version (phase 1 of the roadmap) is no longer a fixed chain. It is a small version of the real architecture: a graph of nodes, a parameter registry, and a command queue. The same rig as before is defined in code in `Source/Core/Rig.cpp`, and the window builds its controls from the parameter registry rather than from hand-written GUI code.

> **Status: written but not yet compiled.** The code was written without access to a compiler or to the JUCE and NAM sources, so expect a few build errors on the first attempt. Paste any errors back and they will be fixed. The NAM-specific calls are all isolated in `Source/NamProcessor.cpp`.

## 1. Prerequisites (Windows 11)

- Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload
- CMake 3.22 or newer, and Git
- The M-Audio Duo's own ASIO driver installed (from M-Audio's website)
- The Steinberg ASIO SDK, unzipped somewhere (for example `C:\sdk\asiosdk`). Without it the app still builds but only uses WASAPI, so latency numbers will not be representative. Check the SDK's licence terms before ever distributing a build.
- A NAM A2 model (`.nam`) and a cab IR (`.wav`) to test with

## 2. Build

From a Developer PowerShell in this folder:

```powershell
# 1. NAM core, with its submodules (Eigen, nlohmann json). It is a git submodule pinned to a tested
#    commit, so a fresh `git clone --recursive` of this repository already has it; otherwise:
git submodule update --init --recursive

# 2. Configure (JUCE is downloaded automatically) and build
cmake -S . -B build -DASIO_SDK_DIR=C:/sdk/asiosdk
cmake --build build --config Release

# 3. Run
.\build\RigBench_artefacts\Release\RigBench.exe
```

Notes:

- **A2 support:** use the newest NAM core. If loading an A2 model reports an unknown architecture, the core you cloned is too old.
- **CPU flags:** the build uses AVX2, which the Intel N97 supports. If you ever build for another machine, add `-DRIGBENCH_AVX2=OFF`.
- **If `NAM/get_dsp.h` or the fast-tanh call fails to compile**, add `-DRIGBENCH_NAM_FAST_TANH=OFF` and tell me which error you got.
- The JUCE version is pinned in `CMakeLists.txt` (`GIT_TAG`). If that tag is not found, change it to the latest JUCE 8 release tag.

## 3. Before you start: safety

- **Output 2 feeds a power amp and a real cab.** It is **off by default**. Turn the power amp volume down before enabling it, and bring the Output 2 level up slowly.
- Both outputs have a hard clip at about -0.2 dBFS, but that is not a substitute for care.
- For the loopback test, **disconnect Output 2 from the power amp**, and mute or disconnect the FOH feed.

## 4. Test procedure

Set the Windows power plan to **High Performance** and close other applications first.

1. Pick the Duo's ASIO driver, set **48000 Hz**, and note the buffer sizes it offers.
2. Load the NAM model. The app warns if the model's training rate differs from the device rate.
3. Load a cab IR.
4. Plug in the guitar, play, and confirm sound on Output 1. Leave Output 2 off for now.
5. For each buffer size you care about (try 64, 128 and 256 samples), click **Reset stats**, play for two minutes, and note mean CPU, peak CPU and overruns.
6. **Loopback latency:** disconnect Output 2 from the power amp, connect a cable from Output 1 to the selected input (use a line-level input, not the Hi-Z instrument input), tick **Loopback latency test**, wait for ten clicks, and read the round-trip time. Untick it afterwards.
7. **Soak test:** at your preferred buffer size, play (or leave running with the guitar plugged in) for 60 minutes. Output 2 can be enabled for this once you are happy with the levels.
8. A CSV log is written every 10 seconds to `Documents\RigBench\bench-<timestamp>.csv`.

## 5. What the numbers mean

| Reading | Meaning |
| --- | --- |
| Callback CPU, mean | Average time spent per audio block, as a percentage of the buffer's duration. The proposed target is under 60%. |
| Callback CPU, peak | Worst single block. If it approaches 100%, a dropout is near. |
| Overruns | Blocks that took longer than the buffer allows. Should be 0. |
| Driver xruns | Dropouts counted by the driver, if it reports them. |
| Reported latency | What the driver claims, plus IR latency. Not measured. |
| Loopback | The measured round trip, including converters and driver buffers. Use this one. |

## 6. What to send back

- Mean and peak CPU and overruns at each buffer size you tested
- The loopback round-trip time at each buffer size
- The model and IR used (file names, and roughly how large they are)
- Whether the 60-minute soak ran clean
- Any build errors, pasted in full

## 7. How this version is organised

| Folder or file | What it is |
| --- | --- |
| `Source/Core/Param.h` | The parameter registry. Every parameter has a stable ID such as `amp/size`, a range, a default and a value. |
| `Source/Core/Node.h` | The one interface every node implements. Every node has an `active` parameter. |
| `Source/Core/Graph.*` | The graph: nodes, edges (each with its own level and on/off), dependency-ordered processing, and summing of several inputs. |
| `Source/Core/Rig.*` | The control layer. It applies commands on its own thread, makes the live graph match a scene's graph, and does the slow model and IR loads off the audio thread. |
| `Source/Nodes/` | The node types: input, output, NAM amp, IR, and the effects (gate, compressor, overdrive, chorus, delay, reverb), and the factory that makes them by name. |
| `Source/NodesPanel.h` | Builds a control for every visible parameter automatically. |
| `Source/AudioEngine.*` | The audio device callback, timing statistics and the loopback test. |

**Things to try**
- **Output 2 source:** in the Routing section, "Reverb -> Output 2" feeds the stage cab with the effects, and "Amp -> Output 2" feeds it the dry amp. Switch one on and the other off. (If both are on they add together.)
- **Active switches:** the tick box at the top of each node's group turns the node on or off. Effects start off. Output 2 starts off because it feeds a power amp.
- **New compressor:** threshold, ratio, attack, release and makeup gain, between the gate and the overdrive.

**Output safety** (unchanged from the last version)
- Loading a model or an IR fades both outputs out (about 20 ms), swaps, and fades back in.
- Every output node has a limiter (about -1 dB) and a hard clip behind it.

**Effects CPU test.** At the buffer size you plan to play at (64 samples):
1. Click **Reset stats** with all effects off, play for a minute, and note the mean and peak CPU. Compare with your earlier baseline of 26.7% mean and 54.1% peak, to see what the graph itself costs.
2. Turn on one effect at a time, including the new compressor, click **Reset stats**, play for a minute, and note the change.
3. Turn everything on at once and check mean CPU stays under about 60% and overruns stay at 0. Your earlier all-on result was 27.4% mean and 51.6% peak.
4. Load a different model and a different IR while playing, and listen for clicks. With Output 2 enabled, do this with the power amp turned down first.

## 8. Level meter and loudness matching

**Level meters** (above the controls, on the right). Six bars show peak level in dBFS: the guitar input before and after the app's input gain, the amp's output after loudness matching, the cab IR's output, and each output after its limiter. Peaks fall back at about 20 dB per second. "CLIP" shows for two seconds if the interface input reaches full scale, and "LIMIT" shows on an output when its signal reaches the limiter's threshold (about -1 dBFS). Aim for input peaks of about -12 to -6 dBFS when playing hard. Set the Duo's own gain first, and use the app's Input gain for fine adjustment. The amp and output rows show where the level is gained or lost, and whether an output is being limited.

**Loudness matching.** NAM files record their loudness. The amp node now trims its output so each model reaches a target loudness (default -18 dB), which stops the volume jumping when you switch models. It reads the loudness of whichever submodel the Model size slider selects, and limits the trim to 12 dB either way. Turn it off with "Match loudness" in the Amp group, or change "Target loudness". A model with no loudness data gets no trim.

**Model information.** The stats box now shows what the file says about itself: its gear type (amp, or amp with cab) and its tone tag. These are hints from whoever published the model, and the tone tag in particular can be wrong. If the gear type says the model includes a cab, the app suggests switching the Cab IR off.

## 9. Input calibration and the amp's Input level

NAM models are captured at a particular input level, and a signal that is too hot pushes even a clean capture into breakup. The amp node now has:

- **Input level** (-60 to +12 dB): drive into the model, saved with the amp's settings. Use it to set how hard each model is driven.
- **Calibrate input** (on by default): if the model records an `input_level_dbu` (the analog level at which it reached 0 dBFS when captured), the amp trims its input automatically so a guitar signal reaches it at a realistic level. Models without that field get no calibration.
- **Interface level at 0 dBFS** (in the new Rig settings group, default +12 dBu): the analog level at which your audio interface reaches full scale at its current gain setting. Higher interface gain means a lower number. Lowering this number increases the attenuation applied to calibrated models, so it is also the quickest way to quieten a model that is still too hot.

The input gain slider on the guitar input now goes down to -48 dB.

## 10. Cab IR level

JUCE normalises each IR to a fixed energy, which makes a typical cab IR about 10 dB quieter than the signal going in. The Cab IR node now has a **Level** control (-24 to +24 dB, default +10 dB) to make that back up, and a meter row shows its output. If the IR still sounds too quiet or too loud, adjust it by the meters. Switching the IR off passes the amp's signal through unchanged.

## 11. MIDI switching

**Connect the controller.** Plug in the MIDI Captain by USB, start the app, and tick it under **MIDI Inputs** in the device panel on the left. The choice is remembered. The monitor at the bottom right shows every message it receives, and what the message did.

**The map.** Every scene has its own MIDI bindings, saved in its scene file: a default address for every node and setting in the scene's graph (added automatically as nodes are added, and never renumbered), plus anything you learn. A readable copy to print and program the controller from is written to `Documents\RigBench\scenes\<scene>.midi-map.txt`. **Everything uses one MIDI channel**, set by `"channel"` in `Documents\RigBench\midi-map.json` (1 by default), so other devices can keep their own channels. Functions are told apart by CC number:

| CC numbers | What they do |
| --- | --- |
| 20 to 39 | Node n on (64 or more) or off. n counts from 0 in signal order, so the noise gate is CC 20. |
| 40 to 59 | Toggle node n. Any value of 64 or more toggles it, and a release (0) is ignored, so a single press toggles. The noise gate is CC 40. |
| 60 to 99 | Numeric settings. 0 to 127 spans the setting's range. |
| 100 to 109 | Other on/off settings, such as Match loudness and the stage output's routing. |
| 110 to 119 | The same settings as 100 to 109, as toggles. |

If a block fills up, extra settings take the next free number between 20 and 119. CC 0, 32 and 120 and above are never used.

**Programming the FCB1010 (stock firmware).** Set the MIDI channel of CC1, CC2, the two expression pedals and the program changes to the app's channel. For each switch, set CC1 (and CC2 if you want a second action) to the toggle number of the node, for example 42 for the overdrive, with a value of 127. One press then toggles that node. Point an expression pedal at a numeric setting's CC number to move it across its range.

Messages from one switch press are applied in order. Program Changes show in the monitor, but scenes are not built yet, so they do nothing.

**Editing.** Change a number in `midi-map.json` to move an address. Delete an entry to unbind it. Several entries can share one address, so one message can drive several settings. Notes can be bound by setting `"type": "note"`. New settings added in later builds are appended without renumbering anything. If the file is not valid JSON, the app tells you in the monitor, and deleting the file makes it write a fresh one.

## 12. MIDI learn and scene files

**MIDI learn.** Every control has a **MIDI** button on its right. Click it, then press a switch or move a pedal on the controller. The first controller or note message received is bound to that control, the button shows the address (for example `Ch1 CC2 tgl`), and the monitor confirms it. While learning, other messages are ignored. Click the button again to cancel, or the small **x** beside it to clear the binding.

- On/off controls learn as a **toggle** by default: any press with a value of 64 or more flips the setting, and a release is ignored. Hold **Shift** while clicking to bind it as on/off by value instead (64 or more = on, below 64 = off).
- Numeric controls bind so that 0 to 127 spans the control's range, which is what an expression pedal needs.
- Learning is one to one. The new message replaces anything already on its address, and any earlier binding of that control.

**Bindings belong to the scene.** Learned bindings are saved in the scene file, `Documents\RigBench\scenes\default.scene.json`, not in the global map. Each scene has its own bindings, so the same switch can do different things in different scenes.

## 13. Scenes, scene presets and node presets

**Files.** Everything is one JSON file per item, under `Documents\RigBench\`:

| Path | What it is |
| --- | --- |
| `presets\<node type>\<id>.preset.json` | A node preset: the settings of one kind of node (for example a chorus), with a slot number. It does not include whether the node is on. |
| `scenes\<id>.scene.json` | A scene: its name, its Program Change number, its graph (nodes, the capture or IR each uses, and connections) and its MIDI bindings. |
| `scenes\<id>\presets\<id>.preset.json` | A scene preset: for each node, which node preset it uses and whether the node is on, plus the on/off and level of each edge. It holds no parameter values of its own. |

**Using them.**
- **Node presets.** Every node has a Preset row. Choose a saved preset to load its settings, or **Save...** to save the node's current settings under a name. Saving over a name replaces it. The slot number shown is what a MIDI CC value selects.
- **Scene presets.** In the scene bar at the top of the right-hand column, **Save preset...** captures the whole rig. For each node it links to a saved node preset with identical settings, or creates a new one named after the scene preset and the node (for example "Verse - Chorus"). Choosing a scene preset applies it behind a short fade, so nothing thumps. Any node it does not list returns to its defaults and is switched off.
- **Scenes.** **New scene...** asks for a name and a Program Change number, and starts from a copy of the current scene's MIDI bindings. A Program Change on the app's channel selects the scene with that number, loads its bindings, and applies its first scene preset. Each scene has its own scene presets. Until the graph engine exists, every scene uses the same graph.
- **MIDI.** The scene preset selector and each node's preset selector are bindable like any other control (the MIDI button beside them), and the default map gives them addresses too. A CC value picks the preset with that slot number. Messages that arrive while a scene is switching are held and applied afterwards in order, so a press that sends a Program Change and then a CC lands the CC on the new scene.

**The graph belongs to the scene.** A scene file holds its own graph: which nodes it has, what each uses (a NAM capture or a cab IR is a property of its amp or IR node), and how they connect. Selecting a scene changes the live graph to match, adding and removing only what differs, so nodes the two scenes share keep their loaded models and nothing reloads. A scene file from an earlier version has no graph, so it gets the starting rig (the same nodes and connections as before), with the model and IR it used. A new scene starts as a copy of the current one. Paths are stored as they are on disk, so moving a file breaks the link and a "file not found" warning shows in the stats box. A scene preset changes settings, not the model, so two captures that you want to switch between in a song live in the same scene as parallel nodes (see section 15).

**Program numbers.** The first scene is called "Default scene" and has program number 0. Scene files from before this version may have no program number. Edit the file and add `"pc": <number>`, or use New scene.

## 14. Known limits of this version

- All nodes are mono. The graph's buffers can carry more channels, but stereo effects need per-channel state and arrive with the first stereo node.
- There is no delay compensation yet. Connections switch on and off abruptly (a short fade is next), so a switch may click.
- Edge levels are not smoothed, so changing one during play could click. Nothing in the GUI changes them yet.
- Turning an effect off cuts its tail instantly.
- The overdrive is not oversampled, so very high drive can sound slightly harsh.

## 15. Building a scene's graph

The right-hand panel is the editor. Every node is a group of controls with a **Remove** button, and the **Connections** section lists every connection with its on/off switch, a level, and an **x** to disconnect it. At the bottom, **Add node...** asks for a kind and a name, and **Connect...** asks for two nodes.

- **A new node starts switched off,** so adding one changes nothing you hear. Switch it on from its tick box. A new connection starts on.
- **Amps and IRs** have a **Capture** or **IR file** row with a **Load...** button. The file belongs to that node in this scene, so a scene can hold any number of amps and IRs, each with its own file.
- **Inputs and outputs** have an **Interface channel** setting, so the SY-200 is just a second input node on channel 2, and any output can feed any interface output.
- **Parallel paths.** Connect one node to several, and connect several into one. Signals into a node are summed, each with its own level. For example, a clean amp and a distorted amp can sit side by side, both fed by the guitar, both feeding the cab IR. To switch between them, switch one connection off and the other on, or save a scene preset for each: a scene preset stores every node's on/off and every connection's on/off and level.
- **Muting a path.** An inactive node passes its input through, so deactivating an amp node alone would let the dry guitar into the mix. Mute a path by switching off its connection. Leave its node active, so it keeps processing and switching back never drops out. Every active amp costs CPU all the time, so the number of amps a scene can hold depends on your machine.
- **Loops** are refused with a message.
- **Removing a node** removes its connections. Scene presets that mention it keep working and ignore it.
- Adding, removing and connecting happen behind the same short fade as a model load, so they do not click or thump.

## 16. Upgrading from the previous version

On first start the app gives each existing scene the starting rig as its graph (same node and connection names as before, so your saved presets still work), with the model and IR the scene used. Your scene bindings stay. Default MIDI addresses are now written into each scene, in the same blocks as before. The global map file now only holds the channel, so its old entries are dropped.


## 17. Tabs and the playing view

The window is now split into four tabs so the playing screen is not cluttered:

- **Play**: scene buttons along the top and one big tile per processing node (inputs, outputs and IRs are shown too if they are nodes other than input and output). A bright tile is on, a dim tile is bypassed. Touch a tile to toggle it. The output meters sit along the bottom. Nothing else can be changed here.
- **Edit**: the scene and scene preset bar, the full meters, and the node panel (add, remove, connect, files, parameters, MIDI learn). The app opens on this tab for now.
- **Setup**: the audio device selector and the MIDI monitor.
- **Diagnostics**: stats, reset, and the loopback latency test.

Tile colours come from the node type for now (PlayView::colourForType). A per-node colour and label property is the next step.

## 18. Fades, per-input interface level and the two-amp test

**Click-free switching.** Every connection's gain now moves to its target over about 10 ms, whether you switch it on or off or change its level. Switching a node on or off crossfades over 10 ms between the processed signal and its bypass (silence for inputs and outputs), and the processed signal keeps running during the fade so tails die away naturally. A new connection or a newly loaded scene starts at its target level (no fade-in), because scene changes already fade the outputs.

**Interface level per input.** The single rig-wide "Interface level at 0 dBFS" is gone. Each input node now has its own `Interface level at 0 dBFS` setting (default +12 dBu), saved with the scene. An amp calibrates against the nearest input that feeds it (following connections upstream, whether or not they are switched on). Set the guitar input to what your Duo gives at 0 dBFS with its gain knob where you have it, and the SY-200 input to the SY-200's line level. If an amp has no input upstream, calibration is skipped.

**Two-amp stress test (do this on the mini PC).**
1. On the Edit tab, add a second Amp node. Load a large capture on each amp.
2. Connect Input 1 to both amps, and both amps to the IR(s) and outputs as you would for a clean/dirty pair.
3. Switch one amp's connection off, then on, and listen for clicks.
4. Open Diagnostics, press Reset stats, and play for five minutes with both amps feeding the outputs. Then try with the small submodel size on both.
5. Report mean and peak CPU, overruns and driver xruns, at 64 samples.

Both amps always run, whichever one you can hear, so this is the real cost of parallel paths. The earlier single-amp figure was about 27% mean and 54% peak.

## 19. Fixes: silent capture nodes, and stale controls on a re-used node ID

- **A switched-off NAM capture node is silent.** It no longer passes the dry signal through. Before, with a clean and a dirty capture in parallel, the capture that was off still passed the guitar through, so you heard the clean guitar whenever the other capture was on. Every other node type still passes through when off. Inputs and outputs are silent when off, as before. (This is a rule for capture nodes only; an IR node, for example, still bypasses.)
- **Stale controls.** If you removed a node and then added a different type of node whose name gave the same ID (a capture node named "Drive" after removing the Overdrive, which also has the ID `drive`), the new node showed the old node's controls, which did nothing. The parameter registry never deletes parameters, so the old ones were still listed under that ID. They are now hidden when a node with that ID is created, so only the new node's own controls appear. Existing MIDI bindings to those old controls stay in the scene file but do nothing.

## 20. Recalling a particular scene preset from a switch

The existing **Scene preset** MIDI button is a *selector*: one CC address where the CC value is the preset's slot number. That suits a pedal that can send different values on one CC, but a stock FCB1010 switch sends one fixed message, so it could only ever reach one preset.

The Edit tab now has a second row, **Recall preset**. Pick a preset in the box above it, click the MIDI button on that row, then press the switch you want. That switch now recalls that preset, whatever value it sends (a value of 0 is ignored, so a release does nothing). Do the same for each preset ("Drive", "Clean", ...). The binding is saved in the scene file against the preset's name, so each preset keeps its own switch. The x beside it clears the binding for the selected preset. Both kinds can be used together.

Note that selecting a preset in the box also loads it, as before.

## 21. The graph view (Edit tab)

The Edit tab now opens with a picture of the scene's signal path above the settings:

- Nodes are boxes coloured by type, arranged in columns from the inputs (left) to the outputs (right). The layout is automatic and is worked out again whenever the graph changes, so nothing about it is saved. (Saved, draggable positions can come later.)
- Wires are the connections. A bright wire is switched on, and its brightness follows its level. A grey dashed wire is switched off. An arrow shows the direction.
- A dim box is a node that is switched off. The round button on a box switches it on or off.
- **Click a node or a wire** to select it. The panel underneath then shows only that item: for a node, its preset, file, settings and the connections that touch it (with a Remove button and the MIDI buttons); for a wire, its on/off, level and a button to delete it.
- **Drag from the dot on a node's right edge** onto another node to connect them. Loops are refused with a message.
- **Add node...** and **Connect...** are on the toolbar above the picture (Connect... is a list-based fallback to dragging).

Other changes: the level meters moved to the Diagnostics tab (the Play tab still shows the outputs), and node types that are not in the scene are not shown anywhere except in the Add node list.

## 22. Pedal captures (pass through when off) and three EQ nodes

**Pass audio through when off.** Every NAM capture node now has a `Pass audio through when off` setting, off by default. With it off, a switched-off capture is silent (the rule from section 19, right for amps sitting in parallel). With it on, a switched-off capture passes its input through unchanged, which is what you want for a capture of a pedal in a chain. It is an ordinary setting, so it is saved in node presets, shows in the node's settings, and has its own MIDI address. Switching still crossfades over 10 ms. While a capture is switched off its model is not run, so it costs no CPU.

**EQ nodes** (Add node... lists them):
- `EQ: bass, mid, treble`: bass is a low shelf at 100 Hz, mid a broad peak at 700 Hz, treble a high shelf at 3 kHz. Each is plus or minus 15 dB.
- `EQ: 10-band graphic`: octave bands at 31 Hz, 62, 125, 250, 500, 1 kHz, 2, 4, 8 and 16 kHz, each plus or minus 12 dB.
- `EQ: high and low shelf`: a low shelf (40 to 400 Hz) and a high shelf (1 to 12 kHz), each with its own gain of plus or minus 15 dB.

Bands set to 0 dB are skipped and cost nothing. Like the other effects they pass the signal through when off. Changing a setting while playing can give a very faint zipper; smoothing the coefficients is a small later job if you hear it.

## 23. Setlists, dragging connection ends, grouped Add node, and fade times

**Setlists.** A group is just a scene preset, so a setlist is a saved, ordered list of scene and scene preset pairs. The new **Setlist** tab lets you:
- create (New...) and delete setlists, and choose the current one;
- add an entry by picking a scene and one of its presets (or "(first preset)"), or press **Add current** to add the scene and preset you are in now;
- move entries up and down, remove them, and double-click one (or press **Go to entry**) to jump to it;
- bind two switches with the **Next entry** and **Previous entry** MIDI buttons. These two bindings are global, kept in `midi-map.json`, because they have to work whichever scene is playing. They respond to any value above 0, so a release does nothing, and they stop at the ends of the list rather than wrapping round.

On the Play tab, the buttons along the top become the current setlist's entries (scene on the first line, preset on the second), with the playing one highlighted. With no setlist chosen they are the scenes, as before. The current setlist is remembered in `Documents\RigBench\settings.json`. Setlists are files in `Documents\RigBench\setlists\`. If the scene is changed some other way (a Program Change, say), no entry is highlighted, and Next continues from the first entry that uses the new scene.

**Dragging connection ends.** In the graph view, drag from the arrow end of a wire (the dot on a node's left edge) and let go on another node to move the wire there, or in empty space to break it. The line turns red when letting go will break it. A moved wire keeps its level and on/off state. If a node has several incoming wires, the selected one is picked up, else the first. **Shift-drag from a node's right dot** does the same with the source end of a wire (the selected one, or the node's only outgoing one). A plain drag from the right dot still starts a new connection.

**Add node menu** is grouped under headings: Inputs and outputs, Amps and cabs, Dynamics, Drive, EQ, Modulation and time. The amp entry is now called "Amp or pedal (NAM capture)".

**Switching times.** The **Setup** tab has two global settings, in milliseconds (0 switches at once, up to 250): **Capture on/off fade** (a NAM capture node being switched on or off) and **Connection on/off fade** (a connection being switched on or off, or its level changing). Both default to 10 ms and are saved in `settings.json`. They are not part of any scene or preset.

## 24. Library files, saved positions, node labels and colours, the status strip

**No mouse wheel on settings.** The wheel no longer changes any slider or drop-down, including the ones inside the audio device selector. It only scrolls.

**Setlist entry names.** Each setlist entry has a name (a song title, say). A new entry starts with its preset's name, or its scene's. Press **Rename...** on the Setlist tab to change it. The name is the button's label on the Play tab. Entries from before have no name and keep showing the scene and preset.

**Library files.** Scenes now refer to a capture or IR inside the library folder by a path relative to it (`lib:NAM/amp.nam`), so you can move or copy `Documents\RigBench` to another machine and the scenes still find their files. A file outside the library keeps its full path. If a file is not where the scene says, RigBench looks for the same file name anywhere in the library's `NAM` (or `IR`) folder. Scenes you already have are converted the first time they are saved. If a file cannot be found, the node keeps working without it and the status strip shows a warning.

**File browser.** **Load...** on a capture or IR node opens a list of the files in the library's `NAM` or `IR` folder, with a filter box. For captures it shows gear type, the tone tag (which can be wrong), loudness, input level and size. Double-click or press **Use this one**. **Browse elsewhere...** opens an ordinary file chooser for a file outside the library.

**Saved positions.** Drag a box in the graph view by its body to put it where you like. Its place is saved in the scene. Boxes you have not moved stay in the automatic layout. **Reset layout** puts them all back.

**Node name and colour.** Each node's header has **Name...** and **Colour...** buttons (Edit tab, select the node). The label and colour are saved in the scene, so the same node can look different in different scenes. They show in the graph view and on the Play tab. **Colour...** offers ten colours or Default (by kind of node).

**Status strip.** A strip along the bottom of every tab shows the scene (and setlist position), CPU mean and peak, sample rate, buffer size and xruns, and then the latest warning or, for five seconds, the latest MIDI message. It is green when all is well, amber for a warning or CPU over 60%, and red for an overrun, an xrun or no audio device.

## 25. Delay compensation

When one path through the graph is slower than another (a node that reports latency, such as a future VST plugin or a look-ahead processor), the paths no longer line up when they meet, and parallel paths would comb-filter. RigBench now fixes this in the graph:
- Each node can report its latency in samples (`latencySamples()`; none of the current nodes has any, so nothing changes yet).
- When the graph is committed, every path into a node is delayed to match the slowest path into it, and all outputs are aligned to the slowest output, so Output 1 and Output 2 stay in time with each other.
- Delay lines run all the time, even for connections that are switched off, so switching a connection on never changes timing and never has to fill a delay line first.
- Compensation is whole samples, on every channel of the source. Connections that are switched off still count towards the longest path.
- The graph exposes `compensatedLatency()`: the extra time, in samples, that the outputs wait for the slowest path.

Tested with a stand-alone harness: two parallel paths with 100 and 0 samples of latency, plus a second output fed by the fast path only. All three arrive on the same sample (100), and the two paths sum to 2.0.

Not yet built: stereo (N-channel) edges. No node needs them yet, so they wait until one does.

## 26. VST3 plugins

**Add node → Plugins → VST3 effect plugin** adds a node that hosts one VST3 effect. Select it on the Edit tab and press **Load...** to choose a `.vst3` file (the file chooser opens in `C:\Program Files\Common Files\VST3` if it exists; a plugin that is a folder works too). The plugin is the node's file property: it is saved in the scene like a capture or an IR (`lib:` path if it sits inside `Documents\RigBench`, so a plugin copied to a `VST3` folder there travels with the library).

- **Plugin window.** Press **Window** on the node to open the plugin's own editor. Moving a control there moves the matching setting in RigBench, and the other way round.
- **Settings.** The plugin's first 64 automatable parameters appear as settings of the node, from 0 to 1 (the plugin's window shows the real values). They save in node presets and scene presets, and can be bound to MIDI like any other setting.
- **Signal.** The graph is mono at the moment: the signal is fed to both inputs of a stereo plugin and the plugin's outputs are averaged back to one channel.
- **On and off.** A switched-off plugin is not run. Like other effects it passes audio through when off; untick **Pass audio through when off** to make it silent instead (for a send to a reverb, say). It is reset when switched on, so no old tail comes back. The on/off uses the global capture fade time.
- **Latency.** The plugin's reported latency is used by delay compensation (section 25), so parallel paths stay in time. The graph is recommitted after a plugin loads.
- **Loading.** The plugin is created on the message thread (plugins expect that), so the window may pause for the second or so a plugin takes to load. The outputs are faded during the swap.
- **Limits for now.** The plugin runs inside RigBench, so a plugin that crashes takes the app with it (the engine/GUI split on the roadmap addresses that). State that is not a parameter (a sample, a loaded preset file inside the plugin) is not saved. There is no MIDI input yet (instruments are on the roadmap). Plugins are not scanned in a separate process.

Build: the project defines `JUCE_PLUGINHOST_VST3=1` and links `juce_audio_processors`. Needs no extra SDK.

## 27. VST3 instruments (notes in)

**Add node → Plugins → VST3 instrument (notes in)** adds a node that hosts a VST3 instrument (a synth, a sampler). Load it like an effect plugin (**Load...**, then **Window** for its editor). An effect loaded into an instrument node, or an instrument into an effect node, is refused with a message saying which node to use.

- **Notes.** Notes that arrive on the global MIDI channel are played by every instrument node that is switched on. Also passed on: pitch bend, aftertouch, the mod wheel (CC 1), the sustain pedal (CC 64), all sound off (CC 120) and all notes off (CC 123). They reach the plugin as channel 1 whatever the global channel is. The Setup tab's MIDI monitor shows notes with "-> instruments".
- **Bindings win.** A note or one of those CCs that is bound to a setting (including by default addresses or MIDI learn) controls the setting and does not play. Other CCs (such as an FCB1010 expression pedal) never reach instruments.
- **Signal.** An instrument makes sound from nothing, so it normally starts a path: connect it to an amp, effect or output like any other node. It has a **Level** control (dB). The signal is mono for now (the plugin's channels are averaged).
- **On and off.** Switched off, an instrument is silent (**Pass audio through when off** is off by default for it) and is not run. Notes that arrive while it is off are not remembered, and when it is switched on the plugin is reset, so no stuck notes come back.
- **Timing.** Notes are delivered at the start of the next audio block (one buffer, about 1.5 ms at 64 samples).
- Everything else is as for effect plugins (section 26): settings, presets, MIDI learn, latency, the same limits.

## 28. Files in presets, deleting, naming entries

**The model or IR is part of a preset.** Saving a node preset (and so a scene preset) also records the capture, cab IR or plugin the node has loaded. Recalling the preset loads that file, but only if it is not the one already loaded, so moving between presets that share a model never reloads it. When a preset does need a different file, the new model is built while the old sound keeps playing, and only the swap itself is faded. A scene preset loads all its files first, then fades the outputs briefly while the settings change. The file is stored as `lib:NAM/...` when it lives in the library, so presets travel with it. Presets saved before this have no file and leave whatever is loaded alone: load the model you want and save the preset again to add it. When a scene is selected, its own file for a node is skipped if the preset about to be applied supplies one, so a model is never loaded twice.

**Deleting.** **Delete** beside a node's preset box deletes that preset. **Delete preset** and **Delete scene** on the Edit tab's scene bar delete the selected scene preset, and the scene together with its scene presets. Each asks first and says how many scene presets or setlist entries use the item (they are left, and show as missing). Node presets, captures and IRs are never deleted along with a scene. The last remaining scene cannot be deleted. Deleting the playing scene moves to another one first.

**Naming entries.** Adding an entry to a setlist (**Add** or **Add current**) now asks for its name first, with the preset or scene name filled in. Use it for a song title. **Rename...** changes it later.

## 29. Songs and footswitch buttons, and the Stage tab

**The idea.** A setlist entry is a song: one scene, a start scene preset, and what each footswitch *button* does in that song. The pedal only ever sends "button 1, button 2 ...", always the same switches, and the song decides which scene preset each one recalls. You do not need to remember which songs use which page of the pedal.

**Setting up the buttons (Setlist tab, lower part).**
1. **Footswitch buttons** sets how many buttons your pedal has (1 to 20; the MIDI Captain 8, the FCB1010 10). It is saved in `settings.json`.
2. Click **1: MIDI**, press the first switch on the pedal, then the same for each of the others. These are global (kept in `midi-map.json` with Next and Previous) and work in every scene. Learning replaces whatever was on that address.
3. Select a setlist entry in the list above and choose, for each button, the scene preset it recalls (or nothing). Each entry has its own assignments. Buttons recall their scene preset when pressed with a value above 0, so releases are ignored.

**Playing.** Going to an entry (Next and Previous, double-click, or the Setlist tab) selects its scene, recalls its start preset and makes its assignments live. With no song playing, button *n* recalls the current scene's scene preset in slot number *n - 1*, so a scene still works from the pedal on its own. A binding of the same address in a scene (for example "recall this scene preset") is replaced when you learn it as a button, and the other way round.

**Stage tab (new, second tab).** An alternative playing view for A/B testing against Play: the previous, current and next song across the top (touch the sides to step), a large button for each footswitch slot showing the scene preset it recalls in this song (the playing one is highlighted; touch to recall), and a strip of node chips along the bottom (touch to switch a node). Play is unchanged.

Setlist files gain a `slots` list per entry; older setlists load with no assignments.

## 30. MIDI output node

**Add node → MIDI → MIDI output (sends messages).** A node with no audio that sends MIDI out of a port when a scene preset (or the node's own preset) is recalled, so one press can change the amp *and* the SY-200's patch.

- **Port and channel.** Choose the MIDI output on the node (the port is saved in the scene) and set the MIDI channel. A port that is not plugged in stays in the list and is reported when you try to send.
- **Messages.** The list starts empty. **Add message** adds a row: choose **Program Change** (a patch number, 0 to 127) or **Control Change** (a controller number and a value, 0 to 127), and set the numbers. **Delete** removes a row and the rows below move up. Up to 16 messages are sent in the order shown (so a Bank Select CC 0 / CC 32 row can come before a Program Change). Numbers are exactly what goes on the wire, so if your device counts patches from 1, subtract one.
- **Presets.** The list, like the other settings, is saved in the node's presets, so each scene preset can recall its own set of messages (for example "Sk8er Boi verse": Program Change 12). A scene preset sends them when it is applied, after the settings are in place; recalling a node preset on its own sends them too. A node preset with no messages sends nothing.
- **Off.** A switched-off MIDI output node sends nothing, which disables it for a song.
- **Send now** sends the list straight away (even with the node off), and shows what went wrong if the port is missing. Use it to check your device answers.
- **Nothing is sent** at startup or when a scene is merely loaded without a preset being applied, and messages never go through the audio path, so they cannot click.
- It can be on the Play tab like any node, and has no MIDI-learn addresses of its own.
