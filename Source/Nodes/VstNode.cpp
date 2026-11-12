#include "VstNode.h"

namespace
{
    constexpr int maxExposedParams = 64;

    // Moves a plugin's own parameter changes (its window, its automation) into the node's setting.
    struct Bridge : juce::AudioProcessorParameter::Listener
    {
        Param* param = nullptr;
        std::atomic<float>* last = nullptr;

        void parameterValueChanged (int, float newValue) override
        {
            last->store (newValue);
            param->set (newValue);
        }

        void parameterGestureChanged (int, bool) override {}
    };
}

struct VstNode::Loaded
{
    std::shared_ptr<juce::AudioPluginInstance> plugin;

    std::vector<Param*> params;                                 // the node's settings
    std::vector<juce::AudioProcessorParameter*> pluginParams;   // the matching plugin parameters
    std::unique_ptr<std::atomic<float>[]> last;                 // what the plugin was last told, or told us
    std::vector<std::unique_ptr<Bridge>> bridges;

    int numIn = 0, numOut = 0;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer midi;

    juce::String name;
    juce::String path;

    ~Loaded()
    {
        for (size_t i = 0; i < bridges.size(); ++i)
            pluginParams[i]->removeListener (bridges[i].get());
    }
};

VstNode::VstNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive,
                  bool isInstrument)
    : Node (id, name, isInstrument ? "vsti" : "vst3", reg, startActive), instrument (isInstrument)
{
    // An effect lets the signal by when it is off. An instrument has nothing to pass, so it is just silent.
    passthrough = addToggle ("passthrough", "Pass audio through when off", ! isInstrument);

    if (instrument)
        level = addNumber ("level", "Level", -48.0f, 12.0f, 0.0f, 0.1f, " dB");
}

VstNode::~VstNode() = default;

juce::String VstNode::filePath() const
{
    const juce::ScopedLock sl (infoLock);
    return pluginPath;
}

juce::String VstNode::fileName() const
{
    const juce::ScopedLock sl (infoLock);
    return pluginName;
}

std::shared_ptr<juce::AudioPluginInstance> VstNode::instance() const
{
    const juce::SpinLock::ScopedLockType lock (const_cast<juce::SpinLock&> (swapLock));
    return current != nullptr ? current->plugin : nullptr;
}

void VstNode::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;

    const juce::SpinLock::ScopedLockType lock (swapLock);
    if (current == nullptr)
        return;

    current->plugin->setPlayConfigDetails (current->numIn, current->numOut, sampleRate, maxBlockSize);
    current->plugin->prepareToPlay (sampleRate, maxBlockSize);
    current->work.setSize (juce::jmax (1, current->numIn, current->numOut), juce::jmax (1, maxBlockSize));
    current->work.clear();
    latency = current->plugin->getLatencySamples();
}

std::shared_ptr<VstNode::Loaded> VstNode::prepareLoad (const juce::File& file, juce::String& error)
{
    const double sr = sampleRate_.load();
    const int block = maxBlock_.load();

    // Plugins expect to be created on the message thread, so the work is handed over and waited for.
    struct Result
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::String error, name;
        bool isInstrument = false;
        juce::WaitableEvent done;
    };

    auto result = std::make_shared<Result>();
    const juce::String path = file.getFullPathName();

    juce::MessageManager::callAsync ([result, path, sr, block]
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, path);

        if (descriptions.isEmpty())
        {
            result->error = "no VST3 plugin found in that file";
        }
        else
        {
            result->name = descriptions[0]->name;
            result->isInstrument = descriptions[0]->isInstrument;
            result->plugin = format.createInstanceFromDescription (*descriptions[0], sr, block, result->error);
        }

        result->done.signal();
    });

    bool finished = false;
    for (int i = 0; i < 300 && ! finished; ++i)   // up to 30 s, but gives up if the control thread is told to stop
    {
        finished = result->done.wait (100);

        if (auto* t = juce::Thread::getCurrentThread())
            if (t->threadShouldExit())
                break;
    }

    if (! finished)
    {
        error = "the plugin took too long to load";
        return nullptr;
    }

    if (result->plugin == nullptr)
    {
        error = result->error.isNotEmpty() ? result->error : juce::String ("the plugin could not be created");
        return nullptr;
    }

    if (result->isInstrument != instrument)
    {
        error = result->isInstrument ? "that plugin is an instrument. Add it as a \"VST3 instrument\" node"
                                     : "that plugin is an effect. Add it as a \"VST3 effect plugin\" node";
        return nullptr;
    }

    auto fresh = std::make_shared<Loaded>();
    fresh->plugin = std::shared_ptr<juce::AudioPluginInstance> (std::move (result->plugin));
    fresh->name = result->name;
    fresh->path = path;

    auto& plugin = *fresh->plugin;

    // Stereo in and out if the plugin allows it (most reverbs and delays are stereo), else its own layout.
    {
        auto layout = plugin.getBusesLayout();

        if (plugin.getBusCount (true) > 0)
            layout.getChannelSet (true, 0) = juce::AudioChannelSet::stereo();
        if (plugin.getBusCount (false) > 0)
            layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();

        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);
    }

    fresh->numIn = plugin.getTotalNumInputChannels();
    fresh->numOut = plugin.getTotalNumOutputChannels();
    plugin.setPlayConfigDetails (fresh->numIn, fresh->numOut, sr, block);
    plugin.prepareToPlay (sr, block);
    fresh->work.setSize (juce::jmax (1, fresh->numIn, fresh->numOut), juce::jmax (1, block));
    fresh->work.clear();

    // Settings: hide the ones from a previous plugin in this node, then register this plugin's.
    for (auto* p : params().all())
        if (p->nodeId == id() && p != activeParam() && p != passthrough)
            p->hidden = true;

    const auto& list = plugin.getParameters();
    int exposed = 0;

    for (int i = 0; i < list.size() && exposed < maxExposedParams; ++i)
    {
        auto* pp = list[i];
        if (! pp->isAutomatable())
            continue;

        juce::String label = pp->getName (40);
        if (label.isEmpty())
            label = "Parameter " + juce::String (i + 1);

        auto* setting = params().add (id(), "p" + std::to_string (i), label, ParamKind::Number,
                                      0.0f, 1.0f, pp->getDefaultValue(), 0.001f, {}, false);
        fresh->params.push_back (setting);
        fresh->pluginParams.push_back (pp);
        ++exposed;
    }

    fresh->last.reset (new std::atomic<float>[fresh->params.size() > 0 ? fresh->params.size() : 1]);
    for (size_t i = 0; i < fresh->params.size(); ++i)
    {
        const float v = fresh->pluginParams[i]->getValue();
        fresh->last[i].store (v);
        fresh->params[i]->set (v);

        auto bridge = std::make_unique<Bridge>();
        bridge->param = fresh->params[i];
        bridge->last = &fresh->last[i];
        fresh->pluginParams[i]->addListener (bridge.get());
        fresh->bridges.push_back (std::move (bridge));
    }

    return fresh;
}

void VstNode::swapIn (std::shared_ptr<Loaded> fresh, const juce::File& file)
{
    std::shared_ptr<Loaded> old;

    {
        const juce::ScopedLock sl (infoLock);
        pluginName = fresh->name;
        pluginPath = file.getFullPathName();
    }

    latency = fresh->plugin->getLatencySamples();
    needsReset = false;

    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        old = std::move (current);
        current = std::move (fresh);
    }

    // Plugins are taken down on the message thread too.
    if (old != nullptr)
        juce::MessageManager::callAsync ([old]() mutable { old.reset(); });
}

void VstNode::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    const juce::SpinLock::ScopedTryLockType lock (swapLock);
    if (! lock.isLocked() || current == nullptr)
        return;   // nothing loaded: the signal passes through

    auto& L = *current;
    if (L.work.getNumSamples() < numSamples)
        return;

    if (needsReset.exchange (false))
    {
        L.plugin->reset();   // also drops notes that were left sounding when the node was switched off
        if (level != nullptr)
            lastGain = juce::Decibels::decibelsToGain (level->get(), -48.0f);
    }

    // Settings to the plugin, only when they changed.
    for (size_t i = 0; i < L.params.size(); ++i)
    {
        const float v = L.params[i]->get();
        if (v != L.last[i].load())
        {
            L.last[i].store (v);
            L.pluginParams[i]->setValue (v);
        }
    }

    const int channels = L.work.getNumChannels();
    for (int c = 0; c < channels; ++c)
    {
        if (c < L.numIn)
            L.work.copyFrom (c, 0, buffer, 0, 0, numSamples);
        else
            L.work.clear (c, 0, numSamples);
    }

    juce::AudioBuffer<float> view (L.work.getArrayOfWritePointers(), channels, numSamples);
    L.midi.clear();
    if (instrument && midiIn != nullptr)
        L.midi.addEvents (*midiIn, 0, numSamples, 0);
    L.plugin->processBlock (view, L.midi);

    if (L.numOut <= 0)
        return;

    // The plugin's outputs, averaged to the node's one channel.
    float* out = buffer.getWritePointer (0);
    juce::FloatVectorOperations::copy (out, view.getReadPointer (0), numSamples);
    for (int c = 1; c < L.numOut; ++c)
        juce::FloatVectorOperations::add (out, view.getReadPointer (c), numSamples);
    if (L.numOut > 1)
        juce::FloatVectorOperations::multiply (out, 1.0f / (float) L.numOut, numSamples);

    if (level != nullptr)
    {
        const float gain = juce::Decibels::decibelsToGain (level->get(), -48.0f);
        buffer.applyGainRamp (0, 0, numSamples, lastGain, gain);
        lastGain = gain;
    }

    latency.store (L.plugin->getLatencySamples(), std::memory_order_relaxed);
}
