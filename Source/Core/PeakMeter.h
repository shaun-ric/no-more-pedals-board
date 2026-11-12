#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>

// Records the highest absolute sample seen since the last take(). The audio thread pushes
// blocks, and the GUI reads and resets it a few times a second.
struct PeakMeter
{
    void push (const float* data, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        const auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
        const float p = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));

        if (p > peak.load (std::memory_order_relaxed))
            peak.store (p, std::memory_order_relaxed);
    }

    float take() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    std::atomic<float> peak { 0.0f };
};
