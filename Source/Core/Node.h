#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <string>
#include <utility>

#include "Param.h"

// Every processing block in the rig (input, amp, IR, effect, output, and later
// plugins and utilities) implements this one interface.
//
// A node processes its buffer in place. The graph sums the incoming edges into the
// buffer before calling run(), and downstream nodes read the buffer afterwards.
// Every node has an "active" parameter. An inactive node passes audio through
// unchanged, except inputs and outputs, which go silent.
class Node
{
public:
    Node (std::string nodeId, juce::String displayName, std::string typeName,
          ParamRegistry& reg, bool startActive, int numChannels = 1)
        : id_ (std::move (nodeId)), name_ (std::move (displayName)), type_ (std::move (typeName)),
          registry (reg), channels_ (numChannels)
    {
        active_ = registry.add (id_, "active", "Active", ParamKind::Toggle, 0.0f, 1.0f,
                                startActive ? 1.0f : 0.0f, 1.0f);
    }

    virtual ~Node() = default;

    const std::string& id() const noexcept { return id_; }
    juce::String displayName() const
    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        return name_;
    }

    // The label and colour can be changed while the rig runs (never read by the audio thread).
    void setDisplayName (const juce::String& newName)
    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        name_ = newName;
    }

    juce::uint32 colour() const noexcept { return colourArgb.load(); }           // 0 = the node type's colour
    void setColour (juce::uint32 argb) noexcept { colourArgb.store (argb); }
    const std::string& type() const noexcept { return type_; }
    int numChannels() const noexcept { return channels_; }
    Param* activeParam() const noexcept { return active_; }

    virtual void prepare (double /*sampleRate*/, int /*maxBlockSize*/) {}
    // A text setting some nodes have (the MIDI output node's device). Saved with the scene's graph.
    virtual bool hasText() const { return false; }
    virtual juce::String textLabel() const { return {}; }
    virtual juce::String textValue() const { return {}; }
    virtual juce::StringArray textOptions() const { return {}; }
    virtual void setTextValue (const juce::String&) {}

    // Called (not on the audio thread) after a preset's settings were applied to this node, for nodes
    // that do something at that moment (the MIDI output node sends its messages).
    virtual void presetApplied() {}

    // Nodes that take notes (VST3 instruments). The graph hands them the block's MIDI before run().
    virtual bool acceptsMidi() const { return false; }
    void setMidiIn (const juce::MidiBuffer* m) noexcept { midiIn = m; }

    virtual int latencySamples() const { return 0; }          // the graph delays parallel paths to match (read when the graph is committed)
    virtual bool isInput() const { return false; }
    virtual bool isOutput() const { return false; }
    virtual int deviceInputChannel() const { return -1; }     // for input nodes: the interface channel, from 0
    virtual int deviceOutputChannel() const { return -1; }    // for output nodes: the interface channel, from 0

    // A file property, such as a NAM capture or a cab IR. Empty and false for nodes without one.
    virtual bool hasFile() const { return false; }
    virtual juce::String filePath() const { return {}; }
    virtual juce::String fileName() const { return {}; }

    // Called by the graph (control thread, before the node is used): sizes the crossfade used when
    // the node is switched on or off, so a bypass change never clicks.
    void prepareFade (double sampleRate, int maxBlockSize)
    {
        fadeSampleRate = sampleRate;
        fadeLen = juce::jmax (0, (int) (0.010 * sampleRate));   // 10 ms unless fadeMsParam says otherwise
        dry.setSize (channels_, juce::jmax (1, maxBlockSize));
        dry.clear();
    }

    // Lets a node take its on/off fade time, in milliseconds, from a global setting (0 = switch at once).
    void setFadeParam (Param* p) noexcept { fadeMsParam = p; }

    // For nodes that calibrate against the interface level of the input feeding them. The graph calls
    // this on every commit with the nearest upstream input's interface-level parameter (or null).
    virtual void setUpstreamInterface (Param*) {}

    // Audio thread.
    void run (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const bool isActive = active_->on();

        if (! initialised)
        {
            initialised = true;
            wasActive = isActive;
            if (isActive)
                onActivate();
        }
        else if (isActive != wasActive)
        {
            if (isActive)
                onActivate();

            wasActive = isActive;

            if (fadeMsParam != nullptr)
                fadeLen = juce::jmax (0, (int) (0.001 * (double) fadeMsParam->get() * fadeSampleRate));

            if (fadeLen > 0 && dry.getNumSamples() >= numSamples)
            {
                fading = true;
                fadePos = 0;
            }
        }

        if (! fading)
        {
            if (isActive)
                process (buffer, numSamples);
            else
                processInactive (buffer, numSamples);
            return;
        }

        if (dry.getNumSamples() < numSamples)
        {
            fading = false;
            return;
        }

        // Crossfade between the processed signal and what the node does while inactive. Both are
        // computed for the ten milliseconds it takes, so tails fade out instead of being cut.
        for (int c = 0; c < buffer.getNumChannels() && c < dry.getNumChannels(); ++c)
            dry.copyFrom (c, 0, buffer, c, 0, numSamples);

        processInactive (dry, numSamples);
        process (buffer, numSamples);

        auto wetGain = [this] (int pos)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (float) pos / (float) fadeLen);
            return wasActive ? t : 1.0f - t;
        };

        const float g0 = wetGain (fadePos), g1 = wetGain (fadePos + numSamples);

        for (int c = 0; c < buffer.getNumChannels() && c < dry.getNumChannels(); ++c)
        {
            buffer.applyGainRamp (c, 0, numSamples, g0, g1);
            buffer.addFromWithRamp (c, 0, dry.getReadPointer (c), numSamples, 1.0f - g0, 1.0f - g1);
        }

        fadePos += numSamples;
        if (fadePos >= fadeLen)
            fading = false;
    }

protected:
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples) = 0;
    virtual void processInactive (juce::AudioBuffer<float>&, int) {}   // default: pass through
    virtual void onActivate() {}                                       // reset tails and state

    ParamRegistry& params() noexcept { return registry; }
    const juce::MidiBuffer* midiIn = nullptr;   // audio thread: this block's notes, if acceptsMidi()

    Param* addNumber (const char* shortName, const juce::String& display, float min, float max,
                      float def, float step, const juce::String& unit = {}, bool persist = false)
    {
        return registry.add (id_, shortName, display, ParamKind::Number, min, max, def, step, unit, persist);
    }

    Param* addToggle (const char* shortName, const juce::String& display, bool def, bool persist = false)
    {
        return registry.add (id_, shortName, display, ParamKind::Toggle, 0.0f, 1.0f, def ? 1.0f : 0.0f, 1.0f, {}, persist);
    }

private:
    std::string id_;
    mutable juce::SpinLock nameLock;
    std::atomic<juce::uint32> colourArgb { 0 };
    juce::String name_;
    std::string type_;
    ParamRegistry& registry;
    int channels_;
    Param* active_ = nullptr;
    bool wasActive = false;
    bool initialised = false;
    bool fading = false;
    int fadePos = 0;
    int fadeLen = 0;
    double fadeSampleRate = 44100.0;
    Param* fadeMsParam = nullptr;
    juce::AudioBuffer<float> dry;
};
