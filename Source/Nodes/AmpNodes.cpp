#include "AmpNodes.h"

#include <cmath>

// ---- NamAmpNode ------------------------------------------------------------

NamAmpNode::NamAmpNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive)
    : Node (id, name, "amp", reg, startActive)
{
    size = addNumber ("size", "Model size", 0.0f, 1.0f, 1.0f, 0.01f);
    match = addToggle ("match", "Match loudness", true);
    target = addNumber ("target", "Target loudness", -30.0f, -6.0f, -18.0f, 0.5f, " dB");
    input = addNumber ("input", "Input level", -60.0f, 12.0f, 0.0f, 0.5f, " dB");
    calibrate = addToggle ("calibrate", "Calibrate input", true);
    passthrough = addToggle ("passthrough", "Pass audio through when off", true);
    setFadeParam (reg.find ("rig/captureFadeMs"));   // the global capture on/off fade time
}

void NamAmpNode::prepare (double sr, int maxBlock)
{
    sampleRate_ = sr;
    maxBlock_ = maxBlock;

    const juce::SpinLock::ScopedLockType lock (modelLock);
    if (nam != nullptr)
        nam->prepare (sr, maxBlock);
}

std::unique_ptr<NamProcessor> NamAmpNode::prepareModel (const std::filesystem::path& file, const NamMetadata& meta, std::string& error)
{
    auto fresh = std::make_unique<NamProcessor>();

    if (! fresh->load (file, error))
        return nullptr;

    fresh->setMetadata (meta);

    const double sr = sampleRate_.load();
    if (sr > 0.0)
        fresh->prepare (sr, maxBlock_.load());

    return fresh;
}

void NamAmpNode::swapModel (std::unique_ptr<NamProcessor> fresh, const juce::File& file)
{
    {
        const juce::ScopedLock sl (infoLock);
        info.fileName = file.getFileName();
        info.filePath = file.getFullPathName();
        info.modelName = fresh->metadata().name;
        info.gearType = fresh->metadata().gearType;
        info.toneType = fresh->metadata().toneType;
        info.loudness = fresh->metadata().loudness;
        info.inputLevelDbu = fresh->metadata().inputLevelDbu;
        info.sampleRate = fresh->expectedSampleRate();
        info.slimmable = fresh->isSlimmable();
        info.breakpoints = fresh->slimmableBreakpoints();
    }

    {
        const juce::SpinLock::ScopedLockType lock (modelLock);
        std::swap (nam, fresh);
    }
    // `fresh` now holds the old model and is destroyed here, off the lock.
}

NamAmpNode::Info NamAmpNode::getInfo() const
{
    const juce::ScopedLock sl (infoLock);
    return info;
}

juce::String NamAmpNode::filePath() const
{
    const juce::ScopedLock sl (infoLock);
    return info.filePath;
}

juce::String NamAmpNode::fileName() const
{
    const juce::ScopedLock sl (infoLock);
    return info.fileName;
}

void NamAmpNode::process (juce::AudioBuffer<float>& b, int n)
{
    float* data = b.getWritePointer (0);

    const juce::SpinLock::ScopedTryLockType lock (modelLock);
    if (lock.isLocked() && nam != nullptr && nam->isLoaded())
    {
        // Drive into the model: the manual input level, plus calibration from the model's own
        // input level. A model captured with 0 dBFS at 22.9 dBu expects a much quieter signal than
        // an interface that reaches 0 dBFS at +12 dBu, so the difference is taken off automatically.
        double calDb = 0.0;
        const double modelInDbu = nam->metadata().inputLevelDbu;
        Param* iface = interfaceLevel.load();
        if (calibrate->on() && iface != nullptr && std::isfinite (modelInDbu))
            calDb = juce::jlimit (-48.0, 24.0, (double) iface->get() - modelInDbu);

        const double totalDb = (double) input->get() + calDb;
        calibrationDb.store (calDb);
        appliedInputDb.store (totalDb);

        const float inputGain = juce::Decibels::decibelsToGain ((float) totalDb);
        b.applyGainRamp (0, 0, n, lastInputGain, inputGain);
        lastInputGain = inputGain;

        nam->setSlimmableSize (size->get());
        nam->process (data, data, n);

        // Loudness matching: bring this model's output to the target loudness, so switching
        // models does not jump in volume. Limited to +/-12 dB. No loudness in the file: no change.
        double trimDb = 0.0;
        if (match->on())
        {
            const double modelLoudness = nam->loudnessFor (size->get());
            if (std::isfinite (modelLoudness))
                trimDb = juce::jlimit (-12.0, 12.0, (double) target->get() - modelLoudness);
        }

        appliedTrimDb.store (trimDb);
        const float trimGain = juce::Decibels::decibelsToGain ((float) trimDb);
        b.applyGainRamp (0, 0, n, lastTrim, trimGain);
        lastTrim = trimGain;

        meterOut.push (data, n);
    }
    // No model, or a swap in progress: the signal passes through unchanged.
}

// ---- IrNode ----------------------------------------------------------------

IrNode::IrNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive)
    : Node (id, name, "ir", reg, startActive)
{
    // JUCE normalises each IR to a fixed energy, which costs about 10 dB with typical cab IRs.
    // The default makes most of that back up, and the meters show the result.
    level = addNumber ("level", "Level", -24.0f, 24.0f, 10.0f, 0.5f, " dB");
}

void IrNode::prepare (double sr, int maxBlock)
{
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 1 });
    convolution.reset();
}

void IrNode::loadFile (const juce::File& file)
{
    const auto oldSize = convolution.getCurrentIRSize();

    convolution.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     0,
                                     juce::dsp::Convolution::Normalise::yes);

    // JUCE builds the new convolution engine on a background thread. Wait (briefly) until it is in.
    const auto start = juce::Time::getMillisecondCounter();
    while (convolution.getCurrentIRSize() == oldSize && juce::Time::getMillisecondCounter() - start < 300)
        juce::Thread::sleep (10);

    juce::Thread::sleep (20);

    {
        const juce::ScopedLock sl (nameLock);
        fileNameStr = file.getFileName();
        filePathStr = file.getFullPathName();
    }
    loaded = true;
}

juce::String IrNode::getFileName() const
{
    const juce::ScopedLock sl (nameLock);
    return fileNameStr;
}

juce::String IrNode::filePath() const
{
    const juce::ScopedLock sl (nameLock);
    return filePathStr;
}

void IrNode::process (juce::AudioBuffer<float>& b, int n)
{
    if (! loaded.load())
        return;   // no IR yet: pass through

    float* ch[1] = { b.getWritePointer (0) };
    juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

    const float gain = juce::Decibels::decibelsToGain (level->get());
    b.applyGainRamp (0, 0, n, lastGain, gain);
    lastGain = gain;

    meterOut.push (ch[0], n);
}
