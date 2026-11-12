#pragma once

#include <juce_core/juce_core.h>

#include "Core/Library.h"
#include "SceneManager.h"

// The setlist being played: a list of scene and scene preset pairs. Going to an entry selects its scene
// and starts in its scene preset. Next and previous stop at the ends (a stray press never wraps round
// to the first song). Message thread only.
class SetlistManager
{
public:
    SetlistManager (Library& lib, SceneManager& scenes) : library (lib), sceneManager (scenes) {}

    bool load (const juce::String& id)
    {
        Setlist s;
        if (id.isEmpty() || ! library.readSetlist (id, s))
        {
            current = {};
            lastIndex = -1;
            return false;
        }

        current = s;
        lastIndex = -1;
        return true;
    }

    // Re-reads the current setlist after it was edited.
    void reload()
    {
        Setlist s;
        if (current.id.isNotEmpty() && library.readSetlist (current.id, s))
            current = s;
        else
            current = {};

        if (lastIndex >= (int) current.entries.size())
            lastIndex = -1;
    }

    bool hasSetlist() const { return current.id.isNotEmpty(); }
    const Setlist& setlist() const { return current; }

    // The entry that is playing, or -1 if the scene has been changed some other way since.
    int playingIndex() const
    {
        if (lastIndex >= 0 && lastIndex < (int) current.entries.size()
            && current.entries[(size_t) lastIndex].scene == sceneManager.currentSceneId())
            return lastIndex;

        return -1;
    }

    void go (int index)
    {
        if (index < 0 || index >= (int) current.entries.size())
            return;

        const auto& e = current.entries[(size_t) index];
        lastIndex = index;
        sceneManager.selectScene (e.scene, false, e.preset);
    }

    void step (int delta)
    {
        const int n = (int) current.entries.size();
        if (n == 0)
            return;

        int base = playingIndex();

        if (base < 0)   // not on an entry: continue from the first entry that uses the current scene
            for (int i = 0; i < n; ++i)
                if (current.entries[(size_t) i].scene == sceneManager.currentSceneId())
                {
                    base = i;
                    break;
                }

        int target;
        if (base < 0)
            target = delta > 0 ? 0 : n - 1;
        else
            target = juce::jlimit (0, n - 1, base + delta);

        go (target);
    }

private:
    Library& library;
    SceneManager& sceneManager;
    Setlist current;
    int lastIndex = -1;
};
