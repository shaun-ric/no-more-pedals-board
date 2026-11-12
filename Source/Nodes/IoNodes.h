#pragma once

#include <juce_dsp/juce_dsp.h>

#include "../Core/Node.h"
#include "../Core/PeakMeter.h"

// An interface input channel. The channel is a property of the node (saved with the scene's graph).
// "Active" off mutes it.
class InputNode : public Node
{
public:
    InputNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false)
        : Node (id, name, "input", reg, startActive)
    {
        channel = addNumber ("channel", "Interface channel", 1.0f, 16.0f, 1.0f, 1.0f);
        channel->structural = true;
        gain = addNumber ("gain", "Input gain", -48.0f, 24.0f, 0.0f, 0.5f, " dB");

        // What this input's hardware delivers at 0 dBFS. The guitar input and a pedal's line output
        // usually differ. Amps downstream use it to calibrate against their model's input level.
        interfaceLvl = addNumber ("interface", "Interface level at 0 dBFS", -20.0f, 30.0f, 12.0f, 0.5f, " dBu");
        interfaceLvl->structural = true;
    }

    bool isInput() const override { return true; }
    int deviceInputChannel() const override { return (int) channel->get() - 1; }

    PeakMeter meterBeforeGain;   // what the interface delivers
    PeakMeter meterAfterGain;    // what the rest of the rig receives

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        meterBeforeGain.push (b.getReadPointer (0), n);

        const float target = juce::Decibels::decibelsToGain (gain->get());
        b.applyGainRamp (0, 0, n, lastGain, target);
        lastGain = target;

        meterAfterGain.push (b.getReadPointer (0), n);
    }

    void processInactive (juce::AudioBuffer<float>& b, int n) override { b.clear (0, n); }

private:
    Param* channel = nullptr;
    Param* gain = nullptr;
    Param* interfaceLvl = nullptr;
    float lastGain = 1.0f;
};

// An interface output channel. Every output has a limiter and a hard clip behind its level,
// so nothing unlimited ever leaves the rig. "Active" off silences it. A new output starts at -24 dB.
class OutputNode : public Node
{
public:
    OutputNode (const std::string& id, const juce::String& name, ParamRegistry& reg,
                bool startActive = false, float defaultLevelDb = -10.5f)
        : Node (id, name, "output", reg, startActive)
    {
        channel = addNumber ("channel", "Interface channel", 1.0f, 16.0f, 1.0f, 1.0f);
        channel->structural = true;
        level = addNumber ("level", "Level", -60.0f, 12.0f, defaultLevelDb, 0.5f, " dB");
    }

    bool isOutput() const override { return true; }
    int deviceOutputChannel() const override { return (int) channel->get() - 1; }

    PeakMeter meterBeforeLimiter;   // after the level control
    PeakMeter meterAfterLimiter;    // what actually leaves this output

    void prepare (double sr, int maxBlock) override
    {
        limiter.prepare ({ sr, (juce::uint32) maxBlock, 1 });
        limiter.setThreshold (-1.0f);
        limiter.setRelease (60.0f);
        limiter.reset();
    }

protected:
    void process (juce::AudioBuffer<float>& b, int n) override
    {
        const float target = juce::Decibels::decibelsToGain (level->get());
        b.applyGainRamp (0, 0, n, lastGain, target);
        lastGain = target;

        float* ch[1] = { b.getWritePointer (0) };
        meterBeforeLimiter.push (ch[0], n);

        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
        limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
        juce::FloatVectorOperations::clip (ch[0], ch[0], -0.98f, 0.98f, n);

        meterAfterLimiter.push (ch[0], n);
    }

    void processInactive (juce::AudioBuffer<float>& b, int n) override { b.clear (0, n); }

    void onActivate() override
    {
        limiter.reset();
        lastGain = juce::Decibels::decibelsToGain (level->get());
    }

private:
    Param* channel = nullptr;
    Param* level = nullptr;
    float lastGain = 1.0f;
    juce::dsp::Limiter<float> limiter;
};
