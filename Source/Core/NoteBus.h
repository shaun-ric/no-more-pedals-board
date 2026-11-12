#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Carries the notes (and the few controllers an instrument needs) from the MIDI thread to the audio
// thread. The audio thread drains it once per block, and the graph hands the result to every node that
// takes MIDI (the VST3 instruments). Events are all moved to MIDI channel 1 for the plugins and happen at
// the start of the block.
class NoteBus
{
public:
    // Any thread (the MIDI input threads). Returns false for a message that is not a note path message.
    bool push (const juce::MidiMessage& m)
    {
        if (! wanted (m))
            return false;

        Event e;
        const auto* raw = m.getRawData();
        e.size = (juce::uint8) juce::jmin (3, m.getRawDataSize());
        for (int i = 0; i < e.size; ++i)
            e.data[i] = raw[i];
        e.data[0] = (juce::uint8) ((e.data[0] & 0xf0) | 0);   // channel 1

        const juce::SpinLock::ScopedLockType lock (writeLock);
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0)
        {
            events[(size_t) start1] = e;
            fifo.finishedWrite (1);
        }
        return true;
    }

    // Audio thread. Adds the waiting events to `out` at sample 0.
    void drain (juce::MidiBuffer& out)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        auto add = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& e = events[(size_t) (start + i)];
                out.addEvent (e.data, e.size, 0);
            }
        };

        add (start1, size1);
        add (start2, size2);
        fifo.finishedRead (size1 + size2);
    }

    // What counts as the note path: notes, pitch bend, aftertouch, the mod wheel, the sustain pedal, and
    // all-sound-off / all-notes-off.
    static bool wanted (const juce::MidiMessage& m)
    {
        if (m.isNoteOnOrOff() || m.isPitchWheel() || m.isAftertouch() || m.isChannelPressure())
            return true;

        if (m.isController())
        {
            const int n = m.getControllerNumber();
            return n == 1 || n == 64 || n == 120 || n == 123;
        }

        return false;
    }

private:
    struct Event { juce::uint8 data[3] { 0, 0, 0 }; juce::uint8 size = 0; };

    static constexpr int capacity = 1024;
    juce::AbstractFifo fifo { capacity };
    std::array<Event, (size_t) capacity> events;
    juce::SpinLock writeLock;   // several MIDI inputs can call push at once
};
