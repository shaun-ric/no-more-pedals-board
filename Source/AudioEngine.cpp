#include "AudioEngine.h"

#include <cmath>

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();
    bufferCapacity = juce::jmax (bs * 2, 2048);
    sampleRate = sr;

    rig.graph().prepare (sr, bufferCapacity);

    safety.reset (sr, 0.02);   // 20 ms fade
    safety.setCurrentAndTargetValue (rig.fadeTarget.load());

    loopCounter = 0;
    sinceFire = -1;
    resetStats();
}

void AudioEngine::resetStats()
{
    blocks = 0;
    overruns = 0;
    sumNs = 0;
    maxNs = 0;
    loopLast = 0;
    loopMin = INT_MAX;
    loopMax = 0;
    loopCount = 0;
    loopSum = 0;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    for (int c = 0; c < numOutputChannels; ++c)
        if (outputChannelData[c] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[c], numSamples);

    const double sr = sampleRate.load();

    if (numSamples <= bufferCapacity && sr > 0.0)
    {
        if (loopbackMode.load())
        {
            const int inCh = juce::jmin (loopbackInput.load(), numInputChannels - 1);
            const float* in = (inCh >= 0 && inputChannelData[inCh] != nullptr) ? inputChannelData[inCh] : nullptr;
            runLoopback (in, outputChannelData, numOutputChannels, numSamples);
        }
        else
        {
            rig.graph().process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
        }

        // Fade applied to every output, used around model and IR swaps.
        safety.setTargetValue (rig.fadeTarget.load());
        if (safety.isSmoothing() || safety.getTargetValue() != 1.0f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float g = safety.getNextValue();
                for (int c = 0; c < numOutputChannels; ++c)
                    if (outputChannelData[c] != nullptr)
                        outputChannelData[c][i] *= g;
            }
        }
    }

    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - startTicks;
    const auto ns = (uint64_t) (juce::Time::highResolutionTicksToSeconds (elapsedTicks) * 1.0e9);

    blocks.fetch_add (1, std::memory_order_relaxed);
    sumNs.fetch_add (ns, std::memory_order_relaxed);

    if (ns > maxNs.load (std::memory_order_relaxed))
        maxNs.store (ns, std::memory_order_relaxed);

    if (sr > 0.0 && (double) ns > 1.0e9 * (double) numSamples / sr)
        overruns.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::runLoopback (const float* in, float* const* out, int numOut, int n)
{
    // Sends a short click once a second on one output and counts samples until
    // it shows up on the chosen input. Cable that output to that input.
    const int outCh = juce::jmin (loopbackOutput.load(), numOut - 1);
    const double sr = sampleRate.load();
    const int interval = (int) sr;
    const int timeout = (int) (sr * 0.5);

    for (int i = 0; i < n; ++i)
    {
        if (loopCounter == 0)
            sinceFire = 0;
        else if (sinceFire >= 0)
            ++sinceFire;

        if (outCh >= 0 && out[outCh] != nullptr)
            out[outCh][i] = (loopCounter < 8) ? 0.25f : 0.0f;

        if (sinceFire > 0 && in != nullptr && std::abs (in[i]) > 0.05f)
        {
            loopLast = sinceFire;
            loopSum += sinceFire;
            loopCount.fetch_add (1);
            if (sinceFire < loopMin.load()) loopMin = sinceFire;
            if (sinceFire > loopMax.load()) loopMax = sinceFire;
            sinceFire = -1;
        }
        else if (sinceFire > timeout)
        {
            sinceFire = -1;
        }

        if (++loopCounter >= interval)
            loopCounter = 0;
    }
}
