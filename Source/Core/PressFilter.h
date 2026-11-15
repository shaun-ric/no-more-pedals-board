#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <string>

// Footswitch debounce. A worn or bouncing switch can send the same press twice within a few milliseconds,
// which would skip a song, toggle a node on and straight off again, or recall a scene preset twice (two
// output fades). accept() returns false for a repeat of the same press (the same `key`) that arrives
// within `windowMs` of the last accepted one. Thread-safe; the time is passed in so it can be tested.
class PressFilter
{
public:
    static constexpr juce::uint32 defaultWindowMs = 150;

    explicit PressFilter (juce::uint32 windowMs = defaultWindowMs) : window (windowMs) {}

    bool accept (const std::string& key, juce::uint32 nowMs)
    {
        const juce::ScopedLock sl (lock);
        const auto it = lastAccepted.find (key);

        // Unsigned subtraction, so the millisecond counter wrapping round (every 49 days) is harmless.
        if (it != lastAccepted.end() && nowMs - it->second < window)
            return false;

        lastAccepted[key] = nowMs;
        return true;
    }

    juce::uint32 windowMs() const noexcept { return window; }

private:
    const juce::uint32 window;
    juce::CriticalSection lock;
    std::map<std::string, juce::uint32> lastAccepted;
};
