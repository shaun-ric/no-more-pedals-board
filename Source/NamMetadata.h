#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <vector>

// What a .nam file says about itself. Read straight from the file's JSON, so it does not
// depend on what the NAM core library exposes. These values are hints made by whoever
// published the model, and can be wrong (a clean-channel capture can be tagged high-gain).
struct NamMetadata
{
    struct Entry
    {
        double maxValue = 1.0;   // upper edge of this submodel's slimming range
        double loudness = std::numeric_limits<double>::quiet_NaN();   // NaN when unknown
    };

    juce::String name, gearType, toneType;
    double sampleRate = -1.0;
    double loudness = std::numeric_limits<double>::quiet_NaN();   // top-level loudness, in dB
    double inputLevelDbu = std::numeric_limits<double>::quiet_NaN();    // analog level at 0 dBFS when captured
    double outputLevelDbu = std::numeric_limits<double>::quiet_NaN();
    std::vector<Entry> submodels;   // empty unless the file is a slimmable container

    // Loudness of the submodel that a given slimming value selects. NaN if unknown.
    double loudnessFor (double size) const noexcept
    {
        if (submodels.empty())
            return loudness;

        const Entry* chosen = &submodels.back();
        for (const auto& e : submodels)
            if (size < e.maxValue) { chosen = &e; break; }

        return std::isnan (chosen->loudness) ? loudness : chosen->loudness;
    }
};

// Returns false if the file cannot be read as JSON. A file with little metadata still returns true.
bool readNamMetadata (const juce::File& file, NamMetadata& out);
