#!/bin/sh
# Syntax-check C++ sources against a JUCE checkout (no linking, no audio).
# Usage: tools/syntax_check.sh /path/to/juce/modules Source/Foo.cpp [more files]
JUCE_MODULES="$1"; shift
for f in "$@"; do
  echo "== $f"
  g++ -std=c++20 -fsyntax-only -ISource -I"$JUCE_MODULES" -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 "$f" 2>&1 | grep -i "error" | head -10
done
