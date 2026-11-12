#include "MidiRouter.h"

#include <algorithm>
#include <set>
#include <utility>

namespace
{
    constexpr int firstCc = 1;     // CC 0 and 32 are bank select, kept free for scenes
    constexpr int lastCc = 119;    // 120 and above are channel mode messages

    // Blocks of CC numbers, so the functions stay apart on a single channel.
    constexpr int nodeSetBase = 20, nodeToggleBase = 40, blockSize = 20;
    constexpr int numericFirst = 60, numericLast = 99;
    constexpr int otherSetFirst = 100, otherToggleFirst = 110, otherBlockSize = 10;

    constexpr int mapVersion = 3;
}

bool MidiRouter::isSlotTarget (const std::string& id)
{
    return id.rfind ("slot:", 0) == 0 && id.size() > 5;
}

bool MidiRouter::isGlobalTarget (const std::string& id)
{
    return id.rfind ("setlist:", 0) == 0 || isSlotTarget (id);
}

bool MidiRouter::isKnownTarget (const std::string& target) const
{
    if (target == "scenepreset" || target.rfind ("scenepreset:", 0) == 0 || target == "setlist:next" || target == "setlist:prev")
        return true;

    if (isSlotTarget (target))
        return true;

    if (target.rfind ("preset:", 0) == 0)
        return rig.graph().findNode (target.substr (7)) != nullptr;

    return rig.params().find (target) != nullptr;
}

juce::String MidiRouter::describe (const Binding& b) const
{
    if (b.param == "scenepreset")
        return "Scene preset  (CC value = preset slot number)";

    if (b.param == "setlist:next")
        return "Setlist: next entry  (any value above 0)";

    if (b.param == "setlist:prev")
        return "Setlist: previous entry  (any value above 0)";

    if (isSlotTarget (b.param))
        return "Footswitch button " + juce::String (b.param.substr (5)) + ": recalls this song's scene preset  (any value above 0)";

    if (b.param.rfind ("scenepreset:", 0) == 0)
        return "Recall scene preset \"" + juce::String (b.param.substr (12)) + "\"  (any value above 0)";

    if (b.param.rfind ("preset:", 0) == 0)
    {
        const auto n = rig.graph().findNode (b.param.substr (7));
        return (n != nullptr ? n->displayName() : juce::String (b.param.substr (7))) + ": preset  (CC value = preset slot number)";
    }

    const Param* p = rig.params().find (b.param);
    if (p == nullptr)
        return juce::String (b.param);

    const auto node = rig.graph().findNode (p->nodeId);
    const juce::String owner = node != nullptr ? node->displayName()
                                               : (p->nodeId == "rig" ? juce::String ("Rig") : juce::String());

    const bool isActive = node != nullptr && node->activeParam() == p;
    juce::String line = owner.isNotEmpty() ? owner + ": " + (isActive ? juce::String ("on/off") : p->name)
                                           : p->name;

    if (b.mode == Mode::Toggle)
        line += "  (toggles on any value of 64 or more)";
    else if (p->kind == ParamKind::Toggle)
        line += "  (64 or more = on, below 64 = off)";
    else
        line += "  (0 to 127 = " + juce::String (p->min, 1) + " to " + juce::String (p->max, 1) + p->unit + ")";

    return line;
}

bool MidiRouter::loadOrCreate (const juce::File& mapFile, juce::String& notice)
{
    int channel = 1;

    if (mapFile.existsAsFile())
    {
        const juce::var root = juce::JSON::parse (mapFile);

        if (! root.isObject())
        {
            notice = "midi-map.json is not valid JSON. Fix it or delete it, and a new one is written.";
            return false;
        }

        channel = juce::jlimit (1, 16, (int) root.getProperty ("channel", juce::var (1)));

        globalBindings.clear();
        if (const auto* entries = root.getProperty ("global", juce::var()).getArray())
            for (const auto& e : *entries)
            {
                Binding b;
                b.param = e.getProperty ("param", juce::var()).toString().toStdString();
                b.isNote = e.getProperty ("type", juce::var ("cc")).toString() == "note";
                b.channel = (int) e.getProperty ("channel", juce::var (1));
                b.number = (int) e.getProperty ("number", juce::var (0));
                b.mode = Mode::Set;
                b.text = describe (b);

                if (isGlobalTarget (b.param))
                    globalBindings.push_back (b);
            }
    }

    globalChannel = channel;
    this->mapFile = mapFile;

    saveGlobal();
    rebuildTable();
    return true;
}

void MidiRouter::saveGlobal()
{
    if (mapFile == juce::File())
        return;

    // Every scene has its own bindings, so this file holds the one channel they all use, and the few
    // bindings that belong to no scene (setlist next and previous).
    juce::Array<juce::var> entries;
    for (const auto& b : globalBindings)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("param", juce::String (b.param));
        e->setProperty ("type", b.isNote ? "note" : "cc");
        e->setProperty ("channel", b.channel);
        e->setProperty ("number", b.number);
        entries.add (juce::var (e));
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("version", mapVersion);
    o->setProperty ("channel", globalChannel.load());
    o->setProperty ("note", "Every scene has its own MIDI bindings. This file sets the channel they all use, and holds the bindings that belong to no scene: setlist next and previous, and the footswitch buttons (slots).");
    o->setProperty ("global", entries);
    mapFile.replaceWithText (juce::JSON::toString (juce::var (o)));
}

void MidiRouter::ensureDefaults (const juce::File& printableFile)
{
    const int channel = globalChannel.load();
    auto& list = sceneBindings;
    const auto sizeBefore = list.size();

    auto hasBinding = [&list] (const std::string& id, Mode mode)
    {
        for (const auto& b : list)
            if (b.param == id && b.mode == mode)
                return true;
        return false;
    };

    auto isUsed = [this, &list, channel] (int cc)
    {
        for (const auto& b : list)
            if (! b.isNote && b.channel == channel && b.number == cc)
                return true;
        for (const auto& b : globalBindings)
            if (! b.isNote && b.channel == channel && b.number == cc)
                return true;
        return false;
    };

    // The preferred number if it is free and in range, else the first free number in the block,
    // else the first free number anywhere in the allowed range. -1 if everything is taken.
    auto allocate = [&isUsed] (int first, int last, int preferred) -> int
    {
        if (preferred >= first && preferred <= last && ! isUsed (preferred))
            return preferred;

        for (int cc = first; cc <= last; ++cc)
            if (! isUsed (cc))
                return cc;

        for (int cc = firstCc; cc <= lastCc; ++cc)
            if (cc != 32 && ! isUsed (cc))
                return cc;

        return -1;
    };

    auto addBinding = [&list, channel] (const std::string& id, Mode mode, int cc)
    {
        if (cc < 0)
            return;

        Binding b;
        b.param = id;
        b.mode = mode;
        b.channel = channel;
        b.number = cc;
        list.push_back (b);
    };

    const auto nodes = rig.graph().nodes();
    std::set<std::string> liveGroups;
    for (const auto& n : nodes)
        liveGroups.insert (n->id());
    for (const auto& e : rig.graph().edges())
        liveGroups.insert ("edge:" + e.from + ">" + e.to);
    liveGroups.insert ("rig");

    std::set<const Param*> activeParams;
    int nodeIndex = 0;

    for (const auto& node : nodes)
    {
        Param* active = node->activeParam();
        activeParams.insert (active);

        if (! hasBinding (active->id, Mode::Set))
            addBinding (active->id, Mode::Set, allocate (nodeSetBase, nodeSetBase + blockSize - 1, nodeSetBase + nodeIndex));

        if (! hasBinding (active->id, Mode::Toggle))
            addBinding (active->id, Mode::Toggle, allocate (nodeToggleBase, nodeToggleBase + blockSize - 1, nodeToggleBase + nodeIndex));

        ++nodeIndex;
    }

    for (auto* p : rig.params().all())
    {
        if (p->hidden || p->customUi || p->structural || activeParams.count (p) > 0 || liveGroups.count (p->nodeId) == 0)
            continue;

        const bool isEdgeLevel = p->nodeId.rfind ("edge:", 0) == 0 && p->kind == ParamKind::Number;
        if (isEdgeLevel)
            continue;   // connection levels can be learned when wanted

        if (p->kind == ParamKind::Number)
        {
            if (! hasBinding (p->id, Mode::Set))
                addBinding (p->id, Mode::Set, allocate (numericFirst, numericLast, -1));
        }
        else
        {
            int setCc = -1;
            if (! hasBinding (p->id, Mode::Set))
            {
                setCc = allocate (otherSetFirst, otherSetFirst + otherBlockSize - 1, -1);
                addBinding (p->id, Mode::Set, setCc);
            }

            if (! hasBinding (p->id, Mode::Toggle))
            {
                const int preferred = setCc >= 0 ? otherToggleFirst + (setCc - otherSetFirst) : -1;
                addBinding (p->id, Mode::Toggle, allocate (otherToggleFirst, otherToggleFirst + otherBlockSize - 1, preferred));
            }
        }
    }

    // Preset selectors: one for each node that has settings, and one for the scene preset.
    for (const auto& node : nodes)
    {
        bool hasSettings = false;
        for (auto* p : rig.params().all())
            if (p->nodeId == node->id() && p != node->activeParam() && ! p->hidden && ! p->structural)
            {
                hasSettings = true;
                break;
            }

        const std::string target = "preset:" + node->id();
        if (hasSettings && ! hasBinding (target, Mode::Set))
            addBinding (target, Mode::Set, allocate (numericFirst, numericLast, -1));
    }

    if (! hasBinding ("scenepreset", Mode::Set))
        addBinding ("scenepreset", Mode::Set, allocate (numericFirst, numericLast, -1));

    for (auto& b : list)
        if (b.text.isEmpty())
            b.text = describe (b);

    if (list.size() != sizeBefore)
    {
        rebuildTable();
        saveScene();
    }

    // A readable copy to print and program the controller from.
    std::vector<Binding> sorted = list;
    std::sort (sorted.begin(), sorted.end(), [] (const Binding& a, const Binding& b)
    {
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.number < b.number;
    });

    juce::String text;
    text << "RigBench MIDI map for the scene \"" << sceneName << "\"\n"
         << "Everything uses MIDI channel " << channel << ". Change \"channel\" in midi-map.json to move it all.\n"
         << "Edit an entry's number in the scene file to move it, or delete an entry to unbind it.\n"
         << "Settings added later are given new numbers at the end. Existing numbers never change.\n\n";

    for (const auto& b : sorted)
        text << (b.channel != channel ? "Ch " + juce::String (b.channel) + "  " : juce::String())
             << (b.isNote ? "Note " : "CC   ") << juce::String (b.number).paddedLeft (' ', 3) << "   " << b.text << "\n";

    printableFile.getParentDirectory().createDirectory();
    printableFile.replaceWithText (text);
}

void MidiRouter::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isActiveSense() || m.isMidiClock())
        return;

    // While a scene is being selected, later messages wait so they land on the new scene.
    if (sceneSwitchPending.load())
    {
        const juce::ScopedLock sl (deferLock);
        deferred.push_back (m);
        return;
    }

    processMessage (m);
}

void MidiRouter::processMessage (const juce::MidiMessage& m)
{
    if (learning.load())
    {
        // While learning, the first controller or note message is taken as the binding, and nothing else acts.
        if (m.isController() || m.isNoteOn())
        {
            const bool isNote = m.isNoteOn();
            const int channel = m.getChannel();
            const int number = isNote ? m.getNoteNumber() : m.getControllerNumber();

            learning = false;

            juce::WeakReference<MidiRouter> self (this);
            juce::MessageManager::callAsync ([self, isNote, channel, number]
            {
                if (self != nullptr)
                    self->finishLearn (isNote, channel, number);
            });
        }
        return;
    }

    // Notes and the few controllers an instrument uses go to the VST3 instruments, unless the address is
    // bound to a setting (a binding always wins). Notes are shown in the monitor like everything else.
    if (m.getChannel() == globalChannel.load() && NoteBus::wanted (m))
    {
        const bool isNote = m.isNoteOnOrOff();
        const bool isCc = m.isController();
        const int number = isNote ? m.getNoteNumber() : isCc ? m.getControllerNumber() : 0;
        const bool bound = (isNote || isCc) && hasBinding (isNote, m.getChannel(), number);

        if (! bound)
        {
            rig.notes().push (m);

            if (isNote)
                addLog ("Ch " + juce::String (m.getChannel()) + (m.isNoteOn() ? " Note on " : " Note off ")
                        + juce::String (m.getNoteNumber()) + "  ->  instruments");
            return;
        }
    }

    if (m.isController())
    {
        dispatch (false, m.getChannel(), m.getControllerNumber(), m.getControllerValue(),
                  "Ch " + juce::String (m.getChannel()) + " CC " + juce::String (m.getControllerNumber())
                      + " = " + juce::String (m.getControllerValue()));
    }
    else if (m.isNoteOn())
    {
        dispatch (true, m.getChannel(), m.getNoteNumber(), m.getVelocity(),
                  "Ch " + juce::String (m.getChannel()) + " Note on " + juce::String (m.getNoteNumber())
                      + " velocity " + juce::String (m.getVelocity()));
    }
    else if (m.isNoteOff())
    {
        dispatch (true, m.getChannel(), m.getNoteNumber(), 0,
                  "Ch " + juce::String (m.getChannel()) + " Note off " + juce::String (m.getNoteNumber()));
    }
    else if (m.isProgramChange())
    {
        const int pc = m.getProgramChangeNumber();
        const int channel = m.getChannel();
        const juce::String where = "Ch " + juce::String (channel) + " PC " + juce::String (pc);

        juce::String sceneId;
        if (channel == globalChannel.load())
        {
            const juce::ScopedLock sl (programLock);
            for (const auto& entry : scenePrograms)
                if (entry.first == pc)
                    sceneId = entry.second;
        }

        if (sceneId.isNotEmpty())
        {
            addLog (where + "  ->  scene \"" + sceneId + "\"");

            sceneSwitchPending = true;
            switchScheduled = true;

            juce::WeakReference<MidiRouter> self (this);
            juce::MessageManager::callAsync ([self, sceneId]
            {
                if (self != nullptr)
                    self->runSceneSwitch (sceneId);
            });
        }
        else if (channel != globalChannel.load())
        {
            addLog (where + "  (the app listens on channel " + juce::String (globalChannel.load()) + ")");
        }
        else
        {
            addLog (where + "  (no scene has this program number)");
        }
    }
}

void MidiRouter::runSceneSwitch (const juce::String& sceneId)
{
    switchScheduled = false;

    if (onSelectScene)
        onSelectScene (sceneId);

    finishSceneSwitch();
}

void MidiRouter::finishSceneSwitch()
{
    for (;;)
    {
        std::deque<juce::MidiMessage> batch;
        {
            const juce::ScopedLock sl (deferLock);
            if (deferred.empty())
            {
                sceneSwitchPending = false;
                return;
            }
            batch.swap (deferred);
        }

        while (! batch.empty())
        {
            const juce::MidiMessage m = batch.front();
            batch.pop_front();
            processMessage (m);

            if (switchScheduled.load())
            {
                // A Program Change in this burst started another scene switch. Hold the rest until it is done.
                const juce::ScopedLock sl (deferLock);
                for (auto it = batch.rbegin(); it != batch.rend(); ++it)
                    deferred.push_front (*it);
                return;
            }
        }
    }
}

void MidiRouter::setScenePrograms (std::vector<std::pair<int, juce::String>> programs)
{
    const juce::ScopedLock sl (programLock);
    scenePrograms = std::move (programs);
}

bool MidiRouter::hasBinding (bool isNote, int channel, int number) const
{
    std::shared_ptr<const Table> t;
    {
        const juce::SpinLock::ScopedLockType lock (tableLock);
        t = table;
    }

    return t != nullptr && t->lookup.find (keyFor (isNote, channel, number)) != t->lookup.end();
}

void MidiRouter::dispatch (bool isNote, int channel, int number, int value, const juce::String& monitorText)
{
    std::shared_ptr<const Table> t;
    {
        const juce::SpinLock::ScopedLockType lock (tableLock);
        t = table;
    }

    if (t == nullptr)
        return;

    const auto it = t->lookup.find (keyFor (isNote, channel, number));

    if (it == t->lookup.end())
    {
        addLog (monitorText + "  (no binding)");
        return;
    }

    bool acted = false;

    for (const size_t index : it->second)
    {
        const Binding& b = t->bindings[index];

        // Setlist navigation (global bindings). A value of 0, such as a release, is ignored.
        if (b.param == "setlist:next" || b.param == "setlist:prev")
        {
            if (value <= 0)
                continue;

            const int delta = b.param == "setlist:next" ? 1 : -1;
            juce::WeakReference<MidiRouter> self (this);
            juce::MessageManager::callAsync ([self, delta]
            {
                if (self != nullptr && self->onSetlistStep)
                    self->onSetlistStep (delta);
            });

            addLog (monitorText + "  ->  " + b.text);
            acted = true;
            continue;
        }

        // A footswitch button of the current song (global bindings). A value of 0 is ignored.
        if (isSlotTarget (b.param))
        {
            if (value <= 0)
                continue;

            const int slot = juce::String (b.param.substr (5)).getIntValue();
            juce::WeakReference<MidiRouter> self (this);
            juce::MessageManager::callAsync ([self, slot]
            {
                if (self != nullptr && self->onSlot)
                    self->onSlot (slot);
            });

            addLog (monitorText + "  ->  " + b.text);
            acted = true;
            continue;
        }

        // Recall one particular scene preset. The value does not matter (a stock FCB1010 sends a fixed
        // value per switch), except that 0 is ignored, as a release or note off would be.
        if (b.param.rfind ("scenepreset:", 0) == 0)
        {
            if (value <= 0)
                continue;

            Command pc;
            pc.type = Command::Type::LoadScenePreset;
            pc.text = juce::String (b.param.substr (12));
            rig.submit (std::move (pc));
            addLog (monitorText + "  ->  " + b.text);
            acted = true;
            continue;
        }

        if (b.param == "scenepreset" || b.param.rfind ("preset:", 0) == 0)
        {
            if (b.mode == Mode::Toggle)
                continue;

            Command pc;
            pc.slot = value;   // the CC value is the preset's slot number

            if (b.param == "scenepreset")
            {
                pc.type = Command::Type::LoadScenePreset;
            }
            else
            {
                pc.type = Command::Type::LoadNodePreset;
                pc.target = b.param.substr (7);
            }

            rig.submit (std::move (pc));
            addLog (monitorText + "  ->  " + b.text);
            acted = true;
            continue;
        }

        const Param* p = rig.params().find (b.param);
        if (p == nullptr)
            continue;

        Command c;
        c.target = b.param;

        if (b.mode == Mode::Toggle)
        {
            if (value < 64)
                continue;   // the release of a press

            c.type = Command::Type::ToggleParam;
        }
        else
        {
            c.type = Command::Type::SetParam;
            c.value = p->kind == ParamKind::Toggle ? (value >= 64 ? 1.0f : 0.0f)
                                                   : p->min + (p->max - p->min) * ((float) value / 127.0f);
        }

        rig.submit (std::move (c));
        addLog (monitorText + "  ->  " + b.text);
        acted = true;
    }

    if (! acted)
        addLog (monitorText + "  (ignored)");
}

void MidiRouter::rebuildTable()
{
    auto t = std::make_shared<Table>();
    t->bindings = sceneBindings;
    t->bindings.insert (t->bindings.end(), globalBindings.begin(), globalBindings.end());

    for (size_t i = 0; i < t->bindings.size(); ++i)
    {
        const auto& b = t->bindings[i];
        t->lookup[keyFor (b.isNote, b.channel, b.number)].push_back (i);
    }

    const juce::SpinLock::ScopedLockType lock (tableLock);
    table = std::move (t);
}

void MidiRouter::loadScene (const juce::File& file)
{
    sceneFile = file;
    sceneBindings.clear();
    sceneName = "Default scene";
    scenePcValue = -1;

    if (file.existsAsFile())
    {
        const juce::var root = juce::JSON::parse (file);

        if (root.isObject())
        {
            sceneName = root.getProperty ("name", juce::var (sceneName)).toString();
            scenePcValue = (int) root.getProperty ("pc", juce::var (-1));

            const juce::var midi = root.getProperty ("midi", juce::var());
            if (const auto* entries = midi.getProperty ("entries", juce::var()).getArray())
            {
                for (const auto& e : *entries)
                {
                    Binding b;
                    b.param = e.getProperty ("param", juce::var()).toString().toStdString();
                    b.isNote = e.getProperty ("type", juce::var ("cc")).toString() == "note";
                    b.channel = (int) e.getProperty ("channel", juce::var (1));
                    b.number = (int) e.getProperty ("number", juce::var (0));
                    b.mode = e.getProperty ("mode", juce::var ("set")).toString() == "toggle" ? Mode::Toggle : Mode::Set;

                    b.text = e.getProperty ("description", juce::var()).toString();
                    if (b.text.isEmpty())
                        b.text = describe (b);

                    sceneBindings.push_back (b);
                }
            }
        }
    }
    else
    {
        saveScene();
    }

    rebuildTable();
}

void MidiRouter::saveScene()
{
    if (sceneFile == juce::File())
        return;

    juce::Array<juce::var> entries;
    for (const auto& b : sceneBindings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("param", juce::String (b.param));
        o->setProperty ("type", b.isNote ? "note" : "cc");
        o->setProperty ("channel", b.channel);
        o->setProperty ("number", b.number);
        o->setProperty ("mode", b.mode == Mode::Toggle ? "toggle" : "set");
        o->setProperty ("description", b.text);
        entries.add (juce::var (o));
    }

    auto* midi = new juce::DynamicObject();
    midi->setProperty ("entries", entries);
    const juce::var midiVar (midi);

    // Only the fields this class owns are written, so the scene's other fields (such as the model
    // and IR it uses) are kept.
    Library::updateJsonFile (sceneFile, [this, &midiVar] (juce::DynamicObject& root)
    {
        root.setProperty ("version", 1);
        root.setProperty ("name", sceneName);
        root.setProperty ("pc", scenePcValue);
        root.setProperty ("midi", midiVar);
    });
}

void MidiRouter::setScenePc (int pc)
{
    scenePcValue = pc;
    saveScene();
}

void MidiRouter::startLearn (const std::string& target, bool asToggle)
{
    if (! isKnownTarget (target))
        return;

    {
        const juce::ScopedLock sl (learnLock);
        learnTarget = target;
        learnAsToggle = asToggle;
    }
    learning = true;

    Binding preview;
    preview.param = target;
    preview.mode = asToggle ? Mode::Toggle : Mode::Set;
    addLog ("Learning: " + describe (preview) + "\nPress a switch or move a pedal now, or click the button again to cancel.");
}

void MidiRouter::cancelLearn()
{
    if (! learning.exchange (false))
        return;

    {
        const juce::ScopedLock sl (learnLock);
        learnTarget.clear();
    }
    addLog ("Learn cancelled.");
}

std::string MidiRouter::learningParam() const
{
    if (! learning.load())
        return {};

    const juce::ScopedLock sl (learnLock);
    return learnTarget;
}

void MidiRouter::finishLearn (bool isNote, int channel, int number)
{
    std::string id;
    bool asToggle = true;
    {
        const juce::ScopedLock sl (learnLock);
        id = learnTarget;
        asToggle = learnAsToggle;
        learnTarget.clear();
    }

    if (id.empty())
        return;

    Binding nb;
    nb.param = id;
    nb.isNote = isNote;
    nb.channel = channel;
    nb.number = number;
    nb.mode = asToggle ? Mode::Toggle : Mode::Set;
    nb.text = describe (nb);

    if (isGlobalTarget (id))
    {
        nb.mode = Mode::Set;
        globalBindings.erase (std::remove_if (globalBindings.begin(), globalBindings.end(), [&nb] (const Binding& b)
        {
            return (b.isNote == nb.isNote && b.channel == nb.channel && b.number == nb.number) || b.param == nb.param;
        }), globalBindings.end());

        globalBindings.push_back (nb);
        rebuildTable();
        saveGlobal();
        addLog ("Learned: Ch " + juce::String (channel) + (isNote ? " Note " : " CC ") + juce::String (number) + "  ->  " + nb.text);
        return;
    }

    // One to one: the new message replaces anything already on its address, and any earlier
    // binding of this setting in the same mode.
    sceneBindings.erase (std::remove_if (sceneBindings.begin(), sceneBindings.end(), [&nb] (const Binding& b)
    {
        return (b.isNote == nb.isNote && b.channel == nb.channel && b.number == nb.number)
            || (b.param == nb.param && b.mode == nb.mode);
    }), sceneBindings.end());

    sceneBindings.push_back (nb);
    rebuildTable();
    saveScene();

    addLog ("Learned: Ch " + juce::String (channel) + (isNote ? " Note " : " CC ") + juce::String (number) + "  ->  " + nb.text);
}

void MidiRouter::clearSceneBinding (const std::string& paramId)
{
    if (isGlobalTarget (paramId))
    {
        const auto beforeGlobal = globalBindings.size();
        globalBindings.erase (std::remove_if (globalBindings.begin(), globalBindings.end(),
                                              [&paramId] (const Binding& b) { return b.param == paramId; }),
                              globalBindings.end());

        if (globalBindings.size() != beforeGlobal)
        {
            rebuildTable();
            saveGlobal();
            addLog ("Cleared the MIDI binding for " + juce::String (paramId) + ".");
        }
        return;
    }

    const auto before = sceneBindings.size();

    sceneBindings.erase (std::remove_if (sceneBindings.begin(), sceneBindings.end(),
                                         [&paramId] (const Binding& b) { return b.param == paramId; }),
                         sceneBindings.end());

    if (sceneBindings.size() != before)
    {
        rebuildTable();
        saveScene();
        addLog ("Cleared the MIDI binding for " + juce::String (paramId) + ".");
    }
}

juce::String MidiRouter::sceneBindingText (const std::string& paramId) const
{
    for (const auto& b : globalBindings)
        if (b.param == paramId)
            return "Ch" + juce::String (b.channel) + (b.isNote ? " N" : " CC") + juce::String (b.number);

    for (const auto& b : sceneBindings)
        if (b.param == paramId)
            return "Ch" + juce::String (b.channel) + (b.isNote ? " N" : " CC") + juce::String (b.number)
                   + (b.mode == Mode::Toggle ? " tgl" : "");

    return {};
}

void MidiRouter::addLog (const juce::String& line)
{
    const juce::ScopedLock sl (logLock);
    log.add (line);

    while (log.size() > 200)
        log.remove (0);
}

juce::StringArray MidiRouter::takeLog()
{
    const juce::ScopedLock sl (logLock);
    juce::StringArray out = log;
    log.clear();
    return out;
}
