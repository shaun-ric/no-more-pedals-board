#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "../Core/Node.h"
#include "../Core/PeakMeter.h"
#include "../NamProcessor.h"

// A NAM amp model. Models are built off the audio thread and swapped in under a try-lock.
class NamAmpNode : public Node
{
public:
    NamAmpNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false);

    bool hasFile() const override { return true; }
    juce::String filePath() const override;
    juce::String fileName() const override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void setUpstreamInterface (Param* p) override { interfaceLevel.store (p); }

    // Control thread. Build first, then swap (the caller fades the outputs around the swap).
    std::unique_ptr<NamProcessor> prepareModel (const std::filesystem::path& file, const NamMetadata& meta, std::string& error);
    void swapModel (std::unique_ptr<NamProcessor> fresh, const juce::File& file);

    struct Info
    {
        juce::String fileName, filePath;
        juce::String modelName, gearType, toneType;
        double loudness = std::numeric_limits<double>::quiet_NaN();
        double inputLevelDbu = std::numeric_limits<double>::quiet_NaN();
        double sampleRate = -1.0;
        bool slimmable = false;
        std::vector<double> breakpoints;
    };
    Info getInfo() const;

    // False until a capture has been loaded. A switched-on node without one passes its input through
    // unprocessed, which the app reports as a warning.
    bool hasModel() const noexcept { return modelLoaded.load(); }

    PeakMeter meterOut;   // the level leaving the amp, after loudness matching
    double getAppliedTrimDb() const noexcept { return appliedTrimDb.load(); }   // loudness matching, in dB
    double getCalibrationDb() const noexcept { return calibrationDb.load(); }   // input calibration from the file, in dB
    double getAppliedInputDb() const noexcept { return appliedInputDb.load(); } // total drive into the model, in dB

protected:
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    // A switched-off capture passes its input through by default ("passthrough" on), which is what a
    // capture of a pedal in a chain needs. With "passthrough" off it is silent instead. Captures that sit
    // in parallel are kept apart by switching their connections off, which scene presets do; a connection
    // out of a node that a scene preset does not know about is switched off by the rig if it would add
    // the dry signal to a path that already has one (see Rig::applyScenePreset).
    void processInactive (juce::AudioBuffer<float>& buffer, int numSamples) override
    {
        juce::ignoreUnused (numSamples);
        if (passthrough == nullptr || ! passthrough->on())
            buffer.clear();
    }

private:
    Param* size = nullptr;     // 0..1, picks the submodel in container models
    Param* match = nullptr;    // match the model's loudness to the target
    Param* target = nullptr;   // target loudness, in dB
    Param* input = nullptr;      // manual drive into the model, in dB
    Param* calibrate = nullptr;  // calibrate the drive from the model's own input level
    Param* passthrough = nullptr;   // when off: pass the input through (a pedal capture) instead of silence
    std::atomic<Param*> interfaceLevel { nullptr };   // the rig-wide interface level at 0 dBFS, in dBu
    std::atomic<double> calibrationDb { 0.0 };
    std::atomic<double> appliedInputDb { 0.0 };
    float lastInputGain = 1.0f;
    std::atomic<double> appliedTrimDb { 0.0 };
    float lastTrim = 1.0f;

    juce::SpinLock modelLock;
    std::unique_ptr<NamProcessor> nam;
    std::atomic<double> sampleRate_ { 0.0 };
    std::atomic<int> maxBlock_ { 0 };
    std::atomic<bool> modelLoaded { false };

    mutable juce::CriticalSection infoLock;
    Info info;
};

// A cab impulse response, using JUCE's zero-latency convolution.
class IrNode : public Node
{
public:
    IrNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = false);

    bool hasFile() const override { return true; }
    juce::String filePath() const override;
    juce::String fileName() const override { return getFileName(); }

    void prepare (double sampleRate, int maxBlockSize) override;

    // Control thread. Hands the file to JUCE's convolution, which builds the new response on its own
    // thread and crossfades to it over 50 ms when it is ready, so there is nothing to wait for.
    void loadFile (const juce::File& file);
    juce::String getFileName() const;

    PeakMeter meterOut;   // the level leaving the IR, after its level control

protected:
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void onActivate() override
    {
        convolution.reset();
        lastGain = juce::Decibels::decibelsToGain (level->get());
    }

private:
    Param* level = nullptr;   // makes up the level the normalised IR loses
    float lastGain = 1.0f;
    static constexpr int maxChunk = 128;   // the convolution's block size: see prepare()
    int chunk = maxChunk;
    juce::dsp::Convolution convolution;
    std::atomic<bool> loaded { false };

    mutable juce::CriticalSection nameLock;
    juce::String fileNameStr, filePathStr;
};
