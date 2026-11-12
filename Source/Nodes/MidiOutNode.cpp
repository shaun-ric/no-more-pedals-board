#include "MidiOutNode.h"

MidiOutNode::MidiOutNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive)
    : Node (id, name, "midiout", reg, startActive)
{
    channel = addNumber ("channel", "MIDI channel", 1.0f, 16.0f, 1.0f, 1.0f);

    rows = addNumber ("rows", "Messages", 0.0f, (float) maxRows, 0.0f, 1.0f);
    rows->customUi = true;

    for (int k = 0; k < maxRows; ++k)
    {
        const std::string n = std::to_string (k + 1);
        auto* t = addToggle (("t" + n).c_str(), ("Message " + n + " is a Control Change").c_str(), false);
        auto* a = addNumber (("a" + n).c_str(), ("Message " + n + " number").c_str(), 0.0f, 127.0f, 0.0f, 1.0f);
        auto* v = addNumber (("v" + n).c_str(), ("Message " + n + " value").c_str(), 0.0f, 127.0f, 0.0f, 1.0f);
        t->customUi = a->customUi = v->customUi = true;
        types.push_back (t);
        numbers.push_back (a);
        values.push_back (v);
    }
}

juce::String MidiOutNode::textValue() const
{
    const juce::ScopedLock sl (lock);
    return device;
}

juce::StringArray MidiOutNode::textOptions() const
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

void MidiOutNode::setTextValue (const juce::String& deviceName)
{
    const juce::ScopedLock sl (lock);
    if (deviceName == device)
        return;

    device = deviceName;
    output.reset();   // reopened on the next send
    openedFor = {};
}

bool MidiOutNode::openIfNeeded (juce::String& problem)
{
    // Called with the lock held.
    if (device.isEmpty())
    {
        problem = "no MIDI output chosen";
        return false;
    }

    if (output != nullptr && openedFor == device)
        return true;

    output.reset();

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == device)
        {
            output = juce::MidiOutput::openDevice (d.identifier);
            openedFor = device;
            break;
        }

    if (output == nullptr)
    {
        problem = "MIDI output \"" + device + "\" is not available";
        return false;
    }

    return true;
}

bool MidiOutNode::sendNow (juce::String& problem)
{
    const juce::ScopedLock sl (lock);

    if (! openIfNeeded (problem))
        return false;

    const int ch = juce::jlimit (1, 16, (int) channel->get());
    const int count = juce::jlimit (0, maxRows, (int) rows->get());

    for (int k = 0; k < count; ++k)
    {
        const int number = juce::jlimit (0, 127, (int) numbers[(size_t) k]->get());

        if (types[(size_t) k]->on())
            output->sendMessageNow (juce::MidiMessage::controllerEvent (ch, number, juce::jlimit (0, 127, (int) values[(size_t) k]->get())));
        else
            output->sendMessageNow (juce::MidiMessage::programChange (ch, number));
    }

    return true;
}

void MidiOutNode::presetApplied()
{
    if (activeParam() == nullptr || ! activeParam()->on() || (int) rows->get() <= 0)
        return;

    juce::String problem;
    sendNow (problem);   // a problem here (no port chosen, say) is shown when "Send now" is pressed
}
