#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <vector>

// Kinds grow as needed (choice, trigger). Number and Toggle cover everything so far.
enum class ParamKind { Number, Toggle };

// A controllable value. Every node registers its parameters in the ParamRegistry,
// and the GUI, MIDI, scenes and presets all address them by `id`.
struct Param
{
    std::string id;        // stable, "node-id/name", never reused for something else
    std::string nodeId;
    juce::String name;     // display name
    juce::String unit;     // for example " dB"
    ParamKind kind = ParamKind::Number;
    float min = 0.0f, max = 1.0f, def = 0.0f, step = 0.01f;
    bool persist = false;     // keeps its value across scene presets
    bool hidden = false;      // not shown by the generic GUI
    bool customUi = false;    // saved in presets like any setting, but the node's panel draws it itself (and it gets no MIDI address)
    bool structural = false;  // part of the graph's layout (such as an interface channel), saved with the
                              // scene's graph rather than in presets
    std::atomic<float> value { 0.0f };

    float get() const noexcept { return value.load (std::memory_order_relaxed); }
    bool on() const noexcept { return get() >= 0.5f; }

    void set (float v) noexcept
    {
        v = (kind == ParamKind::Toggle) ? (v >= 0.5f ? 1.0f : 0.0f) : juce::jlimit (min, max, v);
        value.store (v, std::memory_order_relaxed);
    }

    void reset() noexcept { value.store (def, std::memory_order_relaxed); }
};

// Parameters are never removed, so pointers to them stay valid even after their node is gone. Adding a
// parameter whose ID already exists reuses it (and resets its settings), which is what happens when a
// node is removed and a new one with the same ID is added. Values are atomics, so the audio thread
// reads them directly. The lists are guarded, because nodes are added while other threads look.
class ParamRegistry
{
public:
    Param* add (const std::string& nodeId, const std::string& shortName, const juce::String& displayName,
                ParamKind kind, float min, float max, float def, float step = 0.01f,
                const juce::String& unit = {}, bool persist = false)
    {
        const juce::ScopedLock sl (lock);

        const std::string id = nodeId + "/" + shortName;
        Param* p = nullptr;

        const auto it = byId.find (id);
        if (it != byId.end())
        {
            p = it->second;
        }
        else
        {
            storage.emplace_back();
            p = &storage.back();
            byId[id] = p;
            ordered.push_back (p);
        }

        p->id = id;
        p->nodeId = nodeId;
        p->name = displayName;
        p->unit = unit;
        p->kind = kind;
        p->min = min;
        p->max = max;
        p->def = def;
        p->step = step;
        p->persist = persist;
        p->hidden = false;
        p->customUi = false;
        p->structural = false;
        p->value.store (def);
        return p;
    }

    // Called before a node registers its parameters. Anything left over from an earlier node with the
    // same ID (a different type, say) is hidden, so its controls never appear on the new node. The
    // parameters the new node registers again are made visible by add().
    void retireGroup (const std::string& nodeId)
    {
        const juce::ScopedLock sl (lock);
        for (auto* p : ordered)
            if (p->nodeId == nodeId)
            {
                p->hidden = true;
                p->structural = false;
            }
    }

    Param* find (const std::string& id) const
    {
        const juce::ScopedLock sl (lock);
        const auto it = byId.find (id);
        return it != byId.end() ? it->second : nullptr;
    }

    // A copy, so the caller can loop while other threads add parameters.
    std::vector<Param*> all() const
    {
        const juce::ScopedLock sl (lock);
        return ordered;
    }

private:
    mutable juce::CriticalSection lock;
    std::deque<Param> storage;   // a deque never relocates its elements
    std::map<std::string, Param*> byId;
    std::vector<Param*> ordered;
};
