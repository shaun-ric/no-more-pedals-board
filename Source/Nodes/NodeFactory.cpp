#include "NodeFactory.h"

#include "AmpNodes.h"
#include "EffectNodes.h"
#include "IoNodes.h"
#include "VstNode.h"
#include "MidiOutNode.h"

const std::vector<NodeTypeInfo>& nodeTypes()
{
    static const std::vector<NodeTypeInfo> types
    {
        { "input",      "Input",                    "Inputs and outputs" },
        { "output",     "Output",                   "Inputs and outputs" },
        { "amp",        "Amp or pedal (NAM capture)", "Amps and cabs" },
        { "ir",         "Cab IR",                   "Amps and cabs" },
        { "gate",       "Noise gate",               "Dynamics" },
        { "compressor", "Compressor",               "Dynamics" },
        { "overdrive",  "Overdrive",                "Drive" },
        { "eq3",        "Bass, mid, treble",        "EQ" },
        { "eq10",       "10-band graphic",          "EQ" },
        { "eqshelf",    "High and low shelf",       "EQ" },
        { "chorus",     "Chorus",                   "Modulation and time" },
        { "delay",      "Delay",                    "Modulation and time" },
        { "reverb",     "Reverb",                   "Modulation and time" },
        { "midiout",    "MIDI output (sends messages)", "MIDI" },
        { "vst3",       "VST3 effect plugin",       "Plugins" },
        { "vsti",       "VST3 instrument (notes in)", "Plugins" },
    };

    return types;
}

std::shared_ptr<Node> createNode (const std::string& type, const std::string& id, const juce::String& name,
                                  ParamRegistry& reg, bool startActive)
{
    reg.retireGroup (id);   // stale parameters from a removed node with this ID must not show up

    if (type == "input")      return std::make_shared<InputNode> (id, name, reg, startActive);
    if (type == "output")     return std::make_shared<OutputNode> (id, name, reg, startActive);
    if (type == "amp")        return std::make_shared<NamAmpNode> (id, name, reg, startActive);
    if (type == "ir")         return std::make_shared<IrNode> (id, name, reg, startActive);
    if (type == "gate")       return std::make_shared<GateNode> (id, name, reg, startActive);
    if (type == "compressor") return std::make_shared<CompressorNode> (id, name, reg, startActive);
    if (type == "overdrive")  return std::make_shared<OverdriveNode> (id, name, reg, startActive);
    if (type == "chorus")     return std::make_shared<ChorusNode> (id, name, reg, startActive);
    if (type == "delay")      return std::make_shared<DelayNode> (id, name, reg, startActive);
    if (type == "reverb")     return std::make_shared<ReverbNode> (id, name, reg, startActive);
    if (type == "eq3")        return std::make_shared<ToneEqNode> (id, name, reg, startActive);
    if (type == "eq10")       return std::make_shared<GraphicEqNode> (id, name, reg, startActive);
    if (type == "midiout")    return std::make_shared<MidiOutNode> (id, name, reg, startActive);
    if (type == "vst3")       return std::make_shared<VstNode> (id, name, reg, startActive, false);
    if (type == "vsti")       return std::make_shared<VstNode> (id, name, reg, startActive, true);
    if (type == "eqshelf")    return std::make_shared<ShelfEqNode> (id, name, reg, startActive);

    return nullptr;
}
