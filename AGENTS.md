# RigBench: instructions for agents

Read this first, then `docs/architecture.md` (how the code is organised and the file formats) and `docs/status-and-roadmap.md` (what is done, unverified and next). Treat all three as the working memory of the project; keep them current.

## Repository (since 2026-11-12)
- This repository, `shaun-ric/no-more-pedals-board`, is the source of truth for the code. The root is the CMake project (`CMakeLists.txt`, `Source/`); `tools/` holds the scripts; `docs/` holds the project notes.
- NeuralAmpModelerCore is a git submodule at `third_party/NeuralAmpModelerCore`, pinned to a commit the code was checked against. Change the pin deliberately and say so.
- Work on a branch per change and open a pull request; the owner merges and pulls in GitHub Desktop on Windows. This replaces sending `RigBench.zip`.
- The ASIO SDK, build output and the owner's `Documents\RigBench` library (captures, IRs, presets) are not in this repository.

## What this is
RigBench is a custom live guitar rig host for one player's stage rig: C++20, JUCE 8.0.6, NeuralAmpModelerCore (NAM). It replaces a pedalboard. It runs on a GMKtec mini PC (Intel N97, AVX2) on Windows 11 with an M-Audio Duo (ASIO). The owner is not a C++ specialist; they build on Windows, test on the real rig, and report back.

## Environment you work in
- Code in `Source/`, with `Source/Core`, `Source/Nodes`. `README.md` has numbered sections (1-30) per feature; add a new numbered section for every user-visible change.
- You cannot run the app, hear audio or use Windows. Verification you CAN do: `g++ -std=c++20 -fsyntax-only` against a JUCE 8.0.6 checkout (`-I<juce>/modules -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1`), and the standalone harnesses in `tools/` that link `Source/Core/Graph.cpp` (add `-DJUCE_USE_CURL=0` when building `juce_unity.cpp`). `NamProcessor.cpp` also needs NAM's include paths and Eigen (NAM fetches Eigen from GitLab; a GitHub mirror works if GitLab is unreachable). Python is available for generating and validating library JSON.
- The owner downloads `PopPunkSet.zip` (library files for `Documents\RigBench`). Always say which files changed and whether they overwrite things the owner may have edited.

## Tools
Scripts live under `tools/` (see `tools/README.md`): `gen_poppunk_library.py` generates the song library, `validate_library.py` must be run after every generation, `syntax_check.sh` checks C++ against JUCE, and `test.cpp`/`t2.cpp`/`t3.cpp` are graph harnesses.

## Working rules
1. **Be honest about what is verified.** Say "syntax-checked, not run" or "tested by the owner". Never claim audio behaviour you cannot hear (tone, clicks, latency).
2. **Real-time safety first.** Nothing on the audio thread may allocate, lock a blocking mutex, do file I/O or throw. Slow work (model/IR/plugin loads) happens on the Rig control thread, then swaps in under a fade. Outputs are faded around swaps; every output has a limiter and hard clip. Keep it that way.
3. **Parameters have stable IDs** (`nodeId/name`) and are never removed (use `retireGroup`). Presets and MIDI bindings depend on them. Never rename a param short name without a migration.
4. **Fix the cause.** When a bug report arrives, find the code path and explain why it happened (e.g. the scene-preset recall did not load the file stored in a node preset). Do not guess-patch.
5. **Keep changes small and testable.** One feature or fix per pull request, syntax-check every touched file, and list what the owner should test and what a failure would look like.
6. **No silent behaviour changes.** Defaults that change (pass-through default, output level default) must be stated in the pull request and in the README.
7. **Do not invent gear data.** Amp/IR/capture choices are guesses unless the owner has auditioned them; say so.
8. **Stage hardening matters more than features.** A rig that fails mid-gig is the worst outcome. Prefer predictable, boring, recoverable behaviour.
9. Keep replies short: what changed, what to test, what is still uncertain. Ask a question only when a wrong guess is expensive.

## Owner's preferences (from the conversation)
- A song is one scene; parts of a song are scene presets. Switching amp models inside a song is NOT wanted; load every model a song needs into its own node and mute/unmute with edges and `active`.
- Scenes should be consolidated where songs share a signal chain, but contain only the nodes they need (no VSTi or SY-200 nodes where unused).
- Footswitch preference: the bottom row of buttons is assigned first (MIDI Captain 2x4 = slots 5-8). Setlist entries carry their own slot assignments; the owner does not want to remember pages per song.
- Output level and the model/IR file are part of presets; pedal/amp nodes pass audio through when off by default.
- Setlist entry names are the songs; `[HB]` / `[P90]` tags mark which guitar (Les Paul humbucker / Tele P-90) and may be edited by the owner.
- The owner wants an agentic workflow from here on: take a request, do the work, verify what can be verified, deliver files, and report concisely.

## Definition of done for a change
`tools/ci_check.sh` passes (the pull request's Validate check is green); README section added; pull request opened against `main`; any library JSON validated by script (all node ids, edge ids and file references resolve); the pull request states test steps and known risks.
