#!/usr/bin/env bash
# The validation run before the Windows build (GitHub Actions runs it on Linux; it also runs locally).
# Fails on the first category with a problem, after reporting everything in that category.
#
# Usage: tools/ci_check.sh <JUCE 8.0.6 modules folder> [Eigen folder]
#   The Eigen folder defaults to NAM core's own submodule copy.
set -euo pipefail

JUCE="${1:?usage: tools/ci_check.sh <JUCE modules folder> [Eigen folder]}"
NAM=third_party/NeuralAmpModelerCore
EIGEN="${2:-$NAM/Dependencies/eigen}"

COMMON=(-std=c++20 -w -ISource -I"$JUCE" -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_PLUGINHOST_VST3=1)
NAMFLAGS=(-I"$NAM" -I"$NAM/Dependencies" -I"$EIGEN" -I"$NAM/Dependencies/nlohmann"
          -DNAM_SAMPLE_FLOAT -DRIGBENCH_NAM_FAST_TANH=1)

# ---- 1. Every source file compiles (syntax only: no linking, no Windows) ---------------------------
echo "== 1. Syntax check against JUCE"
fail=0
for f in Source/*.cpp Source/Core/*.cpp Source/Nodes/*.cpp; do
    extra=()
    [[ "$f" == Source/NamProcessor.cpp ]] && extra=("${NAMFLAGS[@]}")
    if g++ "${COMMON[@]}" "${extra[@]}" -fsyntax-only "$f"; then
        echo "ok    $f"
    else
        echo "FAIL  $f"
        fail=1
    fi
done
[[ $fail -eq 0 ]] || { echo "Syntax check failed."; exit 1; }

# ---- 2. Graph harnesses: built, run, and their output compared with the known-good results ---------
echo "== 2. Graph harnesses"
work="$(mktemp -d)"
HFLAGS=("${COMMON[@]}" -O1 -DJUCE_STANDALONE_APPLICATION=0)
g++ "${HFLAGS[@]}" -c tools/juce_unity.cpp -o "$work/juce.o"
g++ "${HFLAGS[@]}" -c Source/Core/Graph.cpp -o "$work/graph.o"
g++ "${HFLAGS[@]}" -c tools/stub.cpp -o "$work/stub.o"

expect() {   # expect <harness> <text that must appear in its output>...
    local name="$1"; shift
    g++ "${HFLAGS[@]}" "tools/$name.cpp" "$work/graph.o" "$work/juce.o" "$work/stub.o" -o "$work/$name" -lpthread -ldl
    local out; out="$("$work/$name")"
    for want in "$@"; do
        if ! grep -qF -- "$want" <<< "$out"; then
            echo "FAIL  $name: expected \"$want\" in:"; echo "$out"; exit 1
        fi
    done
    echo "ok    $name"
}

# Delay compensation: a 100-sample path and a 0-sample path line up, and sum to 2.0 (README section 25).
expect test "compensated latency 100" "out  nonzero at 100 = 2.000000" "out2 nonzero at 100 = 1.000000"
# Note path: notes and pitch bend reach an instrument once; CC 7 is not a note-path message.
expect t2 "pushed on=1 cc7=0 pb=1" "events 3 offs 1" "events after 2nd block 3"
# A node with no connections does not stop the graph from committing or running.
expect t3 "commit 1 ''" "ok"

# ---- 3. The generated song library is consistent ------------------------------------------------------
echo "== 3. Library generator and validator"
python3 -I tools/gen_poppunk_library.py
python3 -I tools/validate_library.py /tmp/popunk/RigBench   # captures and IRs are not in the repo, so they list as missing

echo "All checks passed."
