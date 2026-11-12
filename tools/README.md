# tools/

- `gen_poppunk_library.py`: generates the Pop-Punk Set library (scenes, node presets, scene presets, setlist) from the song/part definitions inside it. Edit the `AMPS`, `IRS`, `EQ`, etc. tables and the `S(...)` song blocks, run it, then zip `/tmp/popunk/RigBench` as `PopPunkSet.zip`. It also applies the setlist conventions (bottom-row slots, `[HB]`/`[P90]` tags). Warning: output overwrites hand-edited presets and setlist names when installed.
- `validate_library.py`: checks a generated library (`python3 validate_library.py /tmp/popunk/RigBench /tmp/n2 /tmp/i2`). Run it after every generation.
- `syntax_check.sh`: g++ syntax check against JUCE sources.
- `test.cpp`, `t2.cpp`, `t3.cpp`, `juce_unity.cpp`, `stub.cpp`: standalone harnesses that link `Source/Core/Graph.cpp` (delay compensation and graph tests). `juce_unity.cpp` pulls in the JUCE modules needed; `stub.cpp` stubs `juce_compilationDate/Time`.

All of these were used during development; none of them runs the audio engine or the GUI.
