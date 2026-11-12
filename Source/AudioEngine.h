#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <climits>

#include "Core/Rig.h"

// The audio device callback. It runs the rig's graph, follows the rig's output fade, and
// keeps timing statistics. A loopback mode (a click out, a timer in) is kept for latency tests.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (Rig& rigToRun) : rig (rigToRun) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void resetStats();

    // Loopback test. While on, the rig is not run.
    std::atomic<bool> loopbackMode { false };
    std::atomic<int> loopbackInput { 0 };
    std::atomic<int> loopbackOutput { 0 };   // Output 1 by default, never the power amp

    // Statistics (written by the audio thread, read by the GUI)
    std::atomic<uint64_t> blocks { 0 }, overruns { 0 }, sumNs { 0 }, maxNs { 0 };
    std::atomic<int> loopLast { 0 }, loopMin { INT_MAX }, loopMax { 0 }, loopCount { 0 };
    std::atomic<int64_t> loopSum { 0 };

private:
    void runLoopback (const float* in, float* const* out, int numOut, int n);

    Rig& rig;
    std::atomic<double> sampleRate { 0.0 };
    int bufferCapacity = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> safety;

    int loopCounter = 0;
    int sinceFire = -1;
};
