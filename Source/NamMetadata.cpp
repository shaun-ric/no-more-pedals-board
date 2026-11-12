#include "NamMetadata.h"

#include <cmath>

bool readNamMetadata (const juce::File& file, NamMetadata& out)
{
    const juce::var root = juce::JSON::parse (file);

    if (! root.isObject())
        return false;

    auto number = [] (const juce::var& v, const char* key, double fallback)
    {
        const juce::Identifier id (key);
        return (v.isObject() && v.hasProperty (id)) ? (double) v.getProperty (id, juce::var()) : fallback;
    };

    const juce::var md = root.getProperty ("metadata", juce::var());

    out.name = md.getProperty ("name", juce::var()).toString();
    out.gearType = md.getProperty ("gear_type", juce::var()).toString();
    out.toneType = md.getProperty ("tone_type", juce::var()).toString();
    out.sampleRate = number (root, "sample_rate", -1.0);
    out.loudness = number (md, "loudness", std::numeric_limits<double>::quiet_NaN());
    out.inputLevelDbu = number (md, "input_level_dbu", std::numeric_limits<double>::quiet_NaN());
    out.outputLevelDbu = number (md, "output_level_dbu", std::numeric_limits<double>::quiet_NaN());
    out.submodels.clear();

    if (root.getProperty ("architecture", juce::var()).toString() == "SlimmableContainer")
    {
        const juce::var subs = root.getProperty ("config", juce::var()).getProperty ("submodels", juce::var());

        if (const auto* array = subs.getArray())
        {
            for (const auto& sm : *array)
            {
                NamMetadata::Entry entry;
                entry.maxValue = number (sm, "max_value", 1.0);

                const juce::var smMeta = sm.getProperty ("model", juce::var()).getProperty ("metadata", juce::var());
                entry.loudness = number (smMeta, "loudness", std::numeric_limits<double>::quiet_NaN());

                out.submodels.push_back (entry);
            }
        }
    }

    return true;
}
