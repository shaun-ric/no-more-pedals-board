#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../Core/Node.h"

// Hosts one VST3 effect plugin. The plugin file is the node's file property (like a capture or an IR).
//
// The graph is mono for now, so the signal is copied to every input channel of the plugin and the
// plugin's output channels are averaged back to one. The plugin's automatable parameters (the first 64)
// appear as settings of the node, 0 to 1, so presets, scenes and MIDI learn work on them. Moving a control
// in the plugin's own window moves the setting too. The plugin's own latency is reported to the graph, which
// delays parallel paths to match.
//
// A plugin is built off the audio thread (on the message thread, because plugins expect that) and swapped
// in under a try-lock; the caller fades the outputs around the swap. A switched-off node is not run,
// and its plugin is reset when it is switched on again, so no stale tail comes back.
class VstNode : public Node
{
public:
    VstNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false,
             bool isInstrument = false);
    ~VstNode() override;

    bool hasFile() const override { return true; }
    juce::String filePath() const override;
    juce::String fileName() const override;      // the plugin's own name

    void prepare (double sampleRate, int maxBlockSize) override;
    bool acceptsMidi() const override { return instrument; }
    int latencySamples() const override { return latency.load(); }

    struct Loaded;

    // Control thread. Build first (slow), then swap in (the caller fades around it, then recommits the
    // graph so the plugin's latency is picked up).
    std::shared_ptr<Loaded> prepareLoad (const juce::File& file, juce::String& error);
    void swapIn (std::shared_ptr<Loaded> fresh, const juce::File& file);

    // For the plugin's own window (message thread). Null when nothing is loaded.
    std::shared_ptr<juce::AudioPluginInstance> instance() const;

protected:
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void processInactive (juce::AudioBuffer<float>& buffer, int) override
    {
        if (passthrough != nullptr && ! passthrough->on())
            buffer.clear();
    }
    void onActivate() override { needsReset = true; }

private:
    const bool instrument;
    Param* level = nullptr;         // instruments only: output level in dB
    float lastGain = 1.0f;
    Param* passthrough = nullptr;   // when off: pass the input through (default) instead of silence
    std::atomic<bool> needsReset { false };

    juce::SpinLock swapLock;
    std::shared_ptr<Loaded> current;
    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<int> maxBlock_ { 512 };
    std::atomic<int> latency { 0 };

    mutable juce::CriticalSection infoLock;
    juce::String pluginName, pluginPath;
};
