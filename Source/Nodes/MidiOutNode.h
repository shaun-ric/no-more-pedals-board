#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

#include "../Core/Node.h"

// Sends MIDI messages out of a port when a preset is recalled. No audio goes through it. It holds a list of
// messages, added row by row (starting empty), each either a Program Change (a patch number) or a Control
// Change (a controller number and a value). The list is part of the node's presets, so each scene preset can
// send its own: recall "Chorus" and the SY-200 changes patch as well.
//
// The port is the node's text setting. A switched-off node sends nothing.
class MidiOutNode : public Node
{
public:
    static constexpr int maxRows = 16;

    MidiOutNode (const std::string& id, const juce::String& name, ParamRegistry& reg, bool startActive = true);

    // The port.
    bool hasText() const override { return true; }
    juce::String textLabel() const override { return "MIDI output"; }
    juce::String textValue() const override;
    juce::StringArray textOptions() const override;
    void setTextValue (const juce::String& deviceName) override;

    void presetApplied() override;

    // Sends the list now whatever the node's state (the "Send now" button). Returns false and says why if it
    // could not.
    bool sendNow (juce::String& problem);

    Param* channelParam() const noexcept { return channel; }
    Param* rowsParam() const noexcept { return rows; }
    Param* typeParam (int row) const { return types[(size_t) row]; }       // off = Program Change, on = Control Change
    Param* numberParam (int row) const { return numbers[(size_t) row]; }   // patch or controller number, 0 to 127
    Param* valueParam (int row) const { return values[(size_t) row]; }     // controller value, 0 to 127

protected:
    void process (juce::AudioBuffer<float>&, int) override {}
    void processInactive (juce::AudioBuffer<float>&, int) override {}

private:
    bool openIfNeeded (juce::String& problem);

    Param* channel = nullptr;
    Param* rows = nullptr;
    std::vector<Param*> types, numbers, values;

    juce::CriticalSection lock;
    juce::String device;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String openedFor;
};
