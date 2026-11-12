#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Rig.h"

// Turns incoming MIDI into rig commands.
//
// Each scene has its own MIDI bindings, saved in the scene file: default addresses for every node and
// setting in the scene's graph (added as nodes are added, and never renumbered), plus anything learned.
//
// Everything shares ONE MIDI channel (the "channel" setting in midi-map.json, default 1), so other devices
// can keep their own channels. The default addresses, told apart by CC number:
//   CC  20 to  39   node n on (64 or more) or off
//   CC  40 to  59   toggle node n (any value of 64 or more, so a release is ignored)
//   CC  60 to  99   numeric settings (0 to 127 spanning the range), and the preset selectors
//   CC 100 to 109   other on/off settings (set), CC 110 to 119 the same settings (toggle)
// A block that fills up spills into the next free number from 1 to 119 (never 0 or 32). A preset
// selector takes the CC value as the preset's slot number.
//
// A Program Change on that channel selects the scene that has that program number. Messages that arrive
// while a scene is being selected wait, and are applied afterwards in order, so they never land on the old scene.
class MidiRouter : public juce::MidiInputCallback
{
public:
    explicit MidiRouter (Rig& rigToControl) : rig (rigToControl) { rebuildTable(); }

    // Reads the shared channel from midi-map.json (writing the file if needed). `notice` is set when there is
    // something to tell the user, and the result is false only on a real error.
    bool loadOrCreate (const juce::File& mapFile, juce::String& notice);

    // Gives every node and setting in the current graph a default address in the current scene, if it has
    // none, and writes a readable copy. Call it whenever the graph changes (message thread).
    void ensureDefaults (const juce::File& printableFile);

    // The scene's own MIDI bindings and program number (message thread). The file is created if it does
    // not exist, and rewritten whenever a binding is learned or cleared.
    void loadScene (const juce::File& sceneFile);
    void setScenePc (int pc);

    // Which Program Change selects which scene, and what to do when one arrives (called on the message thread).
    void setScenePrograms (std::vector<std::pair<int, juce::String>> programs);
    std::function<void (const juce::String&)> onSelectScene;

    // Setlist navigation: "setlist:next" and "setlist:prev" can be learned like any other target. They
    // are global (kept in midi-map.json, not in a scene), because they have to work whatever scene is
    // playing. Called on the message thread with +1 or -1.
    std::function<void (int)> onSetlistStep;

    // Footswitch buttons: "slot:1", "slot:2"... are learned like the setlist ones, are global, and report the
    // button number (from 1) on the message thread. What a button does depends on the song playing.
    std::function<void (int)> onSlot;
    static bool isSlotTarget (const std::string& id);
    static bool isGlobalTarget (const std::string& id);   // setlist:... and slot:...

    // MIDI learn (message thread). The next controller or note message binds to the target, which is a
    // setting's ID, "preset:<node id>" or "scenepreset". A toggle binding flips an on/off setting on any
    // value of 64 or more; otherwise the value is applied directly.
    void startLearn (const std::string& target, bool asToggle);
    void cancelLearn();
    std::string learningParam() const;
    void clearSceneBinding (const std::string& target);
    juce::String sceneBindingText (const std::string& target) const;   // empty if the scene has none

    // MIDI thread.
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    // GUI thread: the monitor lines since the last call, oldest first.
    juce::StringArray takeLog();

private:
    enum class Mode { Set, Toggle };

    struct Binding
    {
        std::string param;   // a setting's ID, "preset:<node id>", or "scenepreset"
        Mode mode = Mode::Set;
        bool isNote = false;
        int channel = 1;
        int number = 0;
        juce::String text;
    };

    // What the MIDI thread reads. It is replaced as a whole, never edited in place.
    struct Table
    {
        std::vector<Binding> bindings;
        std::unordered_map<int, std::vector<size_t>> lookup;   // address -> indices into bindings
    };

    static int keyFor (bool isNote, int channel, int number) noexcept
    {
        return ((isNote ? 1 : 0) << 16) | (channel << 8) | number;
    }

    bool isKnownTarget (const std::string& target) const;
    juce::String describe (const Binding& b) const;
    void rebuildTable();
    void saveScene();
    void processMessage (const juce::MidiMessage& message);
    void finishLearn (bool isNote, int channel, int number);
    bool hasBinding (bool isNote, int channel, int number) const;
    void dispatch (bool isNote, int channel, int number, int value, const juce::String& monitorText);
    void runSceneSwitch (const juce::String& sceneId);
    void finishSceneSwitch();
    void addLog (const juce::String& line);

    Rig& rig;

    std::vector<Binding> sceneBindings;    // message thread only
    std::vector<Binding> globalBindings;   // message thread only: setlist next and previous
    juce::File mapFile;
    void saveGlobal();
    juce::File sceneFile;
    juce::String sceneName { "Default scene" };
    int scenePcValue = -1;

    std::atomic<int> globalChannel { 1 };

    juce::SpinLock tableLock;
    std::shared_ptr<const Table> table;

    mutable juce::CriticalSection learnLock;
    std::string learnTarget;
    bool learnAsToggle = true;
    std::atomic<bool> learning { false };

    juce::CriticalSection programLock;
    std::vector<std::pair<int, juce::String>> scenePrograms;
    std::atomic<bool> sceneSwitchPending { false };
    std::atomic<bool> switchScheduled { false };
    juce::CriticalSection deferLock;
    std::deque<juce::MidiMessage> deferred;

    juce::CriticalSection logLock;
    juce::StringArray log;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MidiRouter)
};
