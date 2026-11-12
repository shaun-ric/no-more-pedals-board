#include "Rig.h"

#include <cmath>
#include <filesystem>

#include "../NamMetadata.h"
#include "../Nodes/AmpNodes.h"
#include "../Nodes/NodeFactory.h"
#include "../Nodes/VstNode.h"

namespace
{
    std::filesystem::path toPath (const juce::File& f)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (f.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::path (f.getFullPathName().toStdString());
       #endif
    }

    std::string shortName (const Param& p) { return p.id.substr (p.nodeId.size() + 1); }

    bool sameValues (const std::vector<std::pair<std::string, float>>& a, const std::vector<std::pair<std::string, float>>& b)
    {
        if (a.size() != b.size())
            return false;

        for (const auto& x : a)
        {
            bool found = false;
            for (const auto& y : b)
                if (x.first == y.first)
                {
                    if (std::abs (x.second - y.second) > 1.0e-3f + 1.0e-4f * std::abs (x.second))
                        return false;
                    found = true;
                    break;
                }

            if (! found)
                return false;
        }
        return true;
    }
}

Rig::Rig() : juce::Thread ("Rig control")
{
    graphObj.setNoteBus (&noteBus);

    // Global switching times, in milliseconds. They are not part of any scene or preset (the app saves
    // them in settings.json) and are hidden from the generic panel and from MIDI defaults.
    registry.add ("rig", "captureFadeMs", "Capture on/off fade", ParamKind::Number, 0.0f, 250.0f, 10.0f, 1.0f, " ms")->hidden = true;
    registry.add ("rig", "connectionFadeMs", "Connection on/off fade", ParamKind::Number, 0.0f, 250.0f, 10.0f, 1.0f, " ms")->hidden = true;
}

Rig::~Rig()
{
    stopThread (3000);
}

void Rig::start()
{
    startThread();
}

void Rig::submit (Command command)
{
    {
        const juce::ScopedLock sl (queueLock);
        queue.push_back (std::move (command));
    }
    notify();
}

void Rig::run()
{
    while (! threadShouldExit())
    {
        for (;;)
        {
            Command c;
            {
                const juce::ScopedLock sl (queueLock);
                if (queue.empty())
                    break;

                c = std::move (queue.front());
                queue.pop_front();
            }
            apply (c);
        }

        wait (200);
    }
}

void Rig::apply (const Command& c)
{
    switch (c.type)
    {
        case Command::Type::SetParam:
            if (auto* p = registry.find (c.target))
                p->set (c.value);
            break;

        case Command::Type::ToggleParam:
            if (auto* p = registry.find (c.target))
                p->set (p->on() ? 0.0f : 1.0f);
            break;

        case Command::Type::LoadFile:
            loadNodeFile (c.target, c.file, c.value > 0.5f);
            break;

        case Command::Type::LoadNodePreset:
            loadNodePreset (c.target, c.text, c.slot);
            break;

        case Command::Type::LoadScenePreset:
            loadScenePreset (c.text, c.slot);
            break;

        case Command::Type::ResetState:
            fade (true);
            resetToDefaults();
            fade (false);
            {
                const juce::ScopedLock sl (sceneLock);
                currentScenePreset = {};
                nodePresetIds.clear();
            }
            break;

        case Command::Type::ApplyGraph:
            if (c.graph != nullptr)
                applyGraph (*c.graph);
            break;
    }
}

void Rig::fade (bool down)
{
    // Calls nest (a preset change fades the outputs and may swap a model inside that), and the outputs only
    // come back when the outermost fade ends.
    if (down)
    {
        if (fadeDepth++ > 0)
            return;

        fadeTarget = 0.0f;
        juce::Thread::sleep (40);   // longer than the 20 ms fade, so the outputs are silent before anything changes
    }
    else
    {
        if (fadeDepth > 0 && --fadeDepth > 0)
            return;

        fadeTarget = 1.0f;
    }
}

void Rig::loadPresetFile (const std::string& nodeId, const NodePreset& preset)
{
    if (preset.file.isEmpty() || library == nullptr)
        return;

    const auto node = graphObj.findNode (nodeId);
    if (node == nullptr || ! node->hasFile())
        return;

    // The same file as the one already loaded costs nothing, so switching between presets that share a
    // model never reloads it.
    loadNodeFile (nodeId, library->resolveAsset (preset.file, node->type()), false);
}

void Rig::setStatus (const juce::String& text)
{
    const juce::ScopedLock sl (statusLock);
    status = text;
}

juce::String Rig::getStatusMessage() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

// ===== The graph ======================================================================================

void Rig::applyGraph (const GraphDef& def)
{
    juce::String problems;
    bool changed = false;

    auto defHasNode = [&def] (const std::string& id, const std::string& type)
    {
        for (const auto& n : def.nodes)
            if (n.id == id && n.type == type)
                return true;
        return false;
    };

    auto defHasEdge = [&def] (const std::string& from, const std::string& to)
    {
        for (const auto& e : def.edges)
            if (e.from == from && e.to == to)
                return true;
        return false;
    };

    // Work out whether anything about the layout differs, so an unchanged graph is left alone.
    for (const auto& node : graphObj.nodes())
        if (! defHasNode (node->id(), node->type()))
            changed = true;

    for (const auto& d : def.nodes)
        if (graphObj.findNode (d.id) == nullptr)
            changed = true;

    for (const auto& e : graphObj.edges())
        if (! defHasEdge (e.from, e.to))
            changed = true;

    for (const auto& e : def.edges)
        if (! graphObj.hasEdge (e.from, e.to))
            changed = true;

    if (changed)
    {
        fade (true);   // silent while the layout changes

        for (const auto& node : graphObj.nodes())
            if (! defHasNode (node->id(), node->type()))
            {
                graphObj.removeNode (node->id());

                const juce::ScopedLock sl (sceneLock);
                nodePresetIds.erase (node->id());
            }

        for (const auto& d : def.nodes)
        {
            if (graphObj.findNode (d.id) != nullptr)
                continue;

            auto node = createNode (d.type, d.id, d.name, registry, d.startActive);
            if (node == nullptr)
            {
                problems << "Unknown node type " << juce::String (d.type) << "  ";
                continue;
            }

            for (const auto& kv : d.props)
                if (auto* p = registry.find (d.id + "/" + kv.first))
                {
                    p->def = kv.second;   // an initial value is also the node's default
                    p->set (kv.second);
                }

            node->setColour (d.colour);
            if (node->hasText())
                node->setTextValue (d.text);
            graphObj.addNode (node);
        }

        for (const auto& e : graphObj.edges())
            if (! defHasEdge (e.from, e.to))
                graphObj.disconnect (e.from, e.to);

        for (const auto& e : def.edges)
            if (! graphObj.hasEdge (e.from, e.to))
                graphObj.connect (e.from, e.to, e.enabled);

        juce::String error;
        if (! graphObj.commit (error))
            problems << "Graph error: " << error << "  ";

        fade (false);

        juce::Thread::sleep (60);   // the audio thread has long since left the old plan
        graphObj.releaseRetired();
    }

    // Structural settings (such as the interface channel) follow the definition.
    for (const auto& d : def.nodes)
        for (const auto& kv : d.props)
            if (auto* p = registry.find (d.id + "/" + kv.first))
                if (p->structural)
                    p->set (kv.second);

    // Names and colours follow the definition too: a node ID can be shared by scenes that label it differently.
    bool labelsChanged = false;
    for (const auto& d : def.nodes)
        if (const auto node = graphObj.findNode (d.id))
            if (node->displayName() != d.name || node->colour() != d.colour)
            {
                node->setDisplayName (d.name);
                node->setColour (d.colour);
                labelsChanged = true;
            }

    for (const auto& d : def.nodes)
        if (const auto node = graphObj.findNode (d.id))
            if (node->hasText() && node->textValue() != d.text)
                node->setTextValue (d.text);

    if (labelsChanged)
        graphObj.touch();

    setStatus (problems);

    // The capture or IR each node should hold. One that is already loaded is left alone.
    for (const auto& d : def.nodes)
        if (d.file.isNotEmpty())
            loadNodeFile (d.id, juce::File (d.file), false);
}

void Rig::loadNodeFile (const std::string& nodeId, const juce::File& file, bool force)
{
    const auto node = graphObj.findNode (nodeId);
    if (node == nullptr || ! node->hasFile())
        return;

    const juce::String path = file.getFullPathName();

    if (! force && node->filePath() == path)
        return;   // a scene that uses what is already loaded costs nothing

    if (! file.exists())   // a VST3 can be a folder (a bundle)
    {
        setStatus (node->displayName() + ": file not found: " + path);
        return;
    }

    if (auto* amp = dynamic_cast<NamAmpNode*> (node.get()))
    {
        // The slow part (reading and building the model) happens while audio keeps playing.
        NamMetadata meta;
        readNamMetadata (file, meta);   // best effort: a file with no metadata still loads

        std::string error;
        auto fresh = amp->prepareModel (toPath (file), meta, error);

        if (fresh == nullptr)
        {
            setStatus (node->displayName() + ": model load failed: " + juce::String (error));
            return;
        }

        fade (true);
        amp->swapModel (std::move (fresh), file);
        fade (false);
        setStatus ({});
    }
    else if (auto* ir = dynamic_cast<IrNode*> (node.get()))
    {
        fade (true);
        ir->loadFile (file);
        fade (false);
        setStatus ({});
    }
    else if (auto* vst = dynamic_cast<VstNode*> (node.get()))
    {
        setStatus ("Loading " + file.getFileName() + "...");

        juce::String error;
        auto fresh = vst->prepareLoad (file, error);   // slow: plugins can take a second or two

        if (fresh == nullptr)
        {
            setStatus (node->displayName() + ": plugin load failed: " + error);
            return;
        }

        fade (true);
        vst->swapIn (std::move (fresh), file);

        // The graph is committed again so the plugin's latency is taken into account.
        juce::String graphError;
        if (! graphObj.commit (graphError))
            setStatus ("Graph error: " + graphError);

        juce::Thread::sleep (60);   // the audio thread has left the old plan
        graphObj.releaseRetired();
        fade (false);

        if (graphError.isEmpty())
            setStatus ({});
    }
}

// ===== Scenes and presets ============================================================================

void Rig::setCurrentScene (const juce::String& sceneId)
{
    const juce::ScopedLock sl (sceneLock);
    currentScene = sceneId;
    currentScenePreset = {};
}

juce::String Rig::getCurrentScene() const
{
    const juce::ScopedLock sl (sceneLock);
    return currentScene;
}

juce::String Rig::getCurrentScenePreset() const
{
    const juce::ScopedLock sl (sceneLock);
    return currentScenePreset;
}

juce::String Rig::getNodePresetId (const std::string& nodeId) const
{
    const juce::ScopedLock sl (sceneLock);
    const auto it = nodePresetIds.find (nodeId);
    return it != nodePresetIds.end() ? it->second : juce::String();
}

std::vector<std::pair<std::string, float>> Rig::captureValues (const Node& node) const
{
    std::vector<std::pair<std::string, float>> values;

    for (auto* p : registry.all())
        if (p->nodeId == node.id() && p != node.activeParam() && ! p->hidden && ! p->structural)
            values.emplace_back (shortName (*p), p->get());

    return values;
}

void Rig::loadNodePreset (const std::string& nodeId, const juce::String& presetId, int slot)
{
    const auto node = graphObj.findNode (nodeId);
    if (node == nullptr || library == nullptr)
        return;

    NodePreset preset;
    const bool found = presetId.isNotEmpty() ? library->readNodePreset (node->type(), presetId, preset)
                                             : (slot >= 0 && library->readNodePresetBySlot (node->type(), slot, preset));

    if (! found)
    {
        setStatus ("No preset " + (presetId.isNotEmpty() ? presetId : "in slot " + juce::String (slot)) + " for " + node->displayName());
        return;
    }

    loadPresetFile (nodeId, preset);   // first, so the settings land on the right model

    for (const auto& kv : preset.values)
        if (auto* p = registry.find (nodeId + "/" + kv.first))
            if (! p->structural)
                p->set (kv.second);

    {
        const juce::ScopedLock sl (sceneLock);
        nodePresetIds[nodeId] = preset.id;
    }

    if (preset.file.isEmpty())
        setStatus ({});   // otherwise loading the file reports its own problems

    node->presetApplied();
}

void Rig::loadScenePreset (const juce::String& presetId, int slot)
{
    if (library == nullptr)
        return;

    const juce::String scene = getCurrentScene();
    ScenePreset preset;
    const bool found = presetId.isNotEmpty() ? library->readScenePreset (scene, presetId, preset)
                                             : (slot >= 0 && library->readScenePresetBySlot (scene, slot, preset));

    if (! found)
    {
        setStatus ("No scene preset " + (presetId.isNotEmpty() ? presetId : "in slot " + juce::String (slot)));
        return;
    }

    fade (true);          // the outputs are silent while the settings change
    applyScenePreset (preset);
    fade (false);

    const juce::ScopedLock sl (sceneLock);
    currentScenePreset = preset.id;
}

void Rig::applyScenePreset (const ScenePreset& preset)
{
    juce::String problems;
    std::vector<std::shared_ptr<Node>> applied;   // nodes that were given a node preset

    for (const auto& node : graphObj.nodes())
    {
        const ScenePresetNode* entry = nullptr;
        for (const auto& n : preset.nodes)
            if (n.node == node->id())
                entry = &n;

        // A node's own default settings, then the preset's values on top.
        for (auto* p : registry.all())
            if (p->nodeId == node->id() && p != node->activeParam() && ! p->persist && ! p->structural)
                p->reset();

        bool active = false;

        if (entry != nullptr)
        {
            active = entry->active;

            if (entry->presetId.isNotEmpty())
            {
                NodePreset np;
                if (library->readNodePreset (node->type(), entry->presetId, np))
                {
                    if (np.file.isNotEmpty())   // the model or IR first (nothing happens if it is already loaded)
                    {
                        loadPresetFile (node->id(), np);
                        const juce::String fileProblem = getStatusMessage();
                        if (fileProblem.isNotEmpty())
                            problems << fileProblem << "  ";
                    }

                    for (const auto& kv : np.values)
                        if (auto* p = registry.find (node->id() + "/" + kv.first))
                            if (! p->structural)
                                p->set (kv.second);

                    const juce::ScopedLock sl (sceneLock);
                    nodePresetIds[node->id()] = np.id;
                    applied.push_back (node);
                }
                else
                {
                    problems << node->displayName() << ": missing preset " << entry->presetId << "  ";
                }
            }
        }
        else
        {
            // A node the preset does not list returns to its defaults and is switched off.
            const juce::ScopedLock sl (sceneLock);
            nodePresetIds.erase (node->id());
        }

        node->activeParam()->set (active ? 1.0f : 0.0f);   // last, so nothing changes level while settings move
    }

    for (const auto& edge : graphObj.edges())
    {
        const std::string key = edge.from + ">" + edge.to;
        const ScenePresetEdge* entry = nullptr;
        for (const auto& e : preset.edges)
            if (e.edge == key)
                entry = &e;

        if (entry != nullptr)
        {
            edge.enabled->set (entry->enabled ? 1.0f : 0.0f);
            edge.level->set (entry->levelDb);
        }
        else
        {
            edge.enabled->reset();
            edge.level->reset();
        }
    }

    setStatus (problems);

    // Nodes that act when a preset is recalled (MIDI output) do so now that everything is in place.
    for (const auto& node : applied)
        node->presetApplied();
}

void Rig::resetToDefaults()
{
    for (auto* p : registry.all())
        if (p->nodeId != "rig" && ! p->persist && ! p->structural)
            p->reset();
}

bool Rig::saveNodePreset (const std::string& nodeId, const juce::String& name, juce::String& error)
{
    const auto node = graphObj.findNode (nodeId);
    if (library == nullptr || node == nullptr)
    {
        error = "There is no library to save to.";
        return false;
    }

    NodePreset preset;
    preset.nodeType = node->type();
    preset.name = name;
    preset.id = Library::slug (name);
    preset.values = captureValues (*node);

    if (preset.values.empty())
    {
        error = "This node has no settings to save.";
        return false;
    }

    NodePreset existing;
    preset.slot = library->readNodePreset (preset.nodeType, preset.id, existing) ? existing.slot
                                                                                 : library->nextFreeNodeSlot (preset.nodeType);

    if (preset.slot > 127)
    {
        error = "All 128 preset slots for this kind of node are used.";
        return false;
    }

    if (! library->writeNodePreset (preset))
    {
        error = "The preset file could not be written.";
        return false;
    }

    const juce::ScopedLock sl (sceneLock);
    nodePresetIds[nodeId] = preset.id;
    return true;
}

bool Rig::saveScenePreset (const juce::String& name, juce::String& error)
{
    const juce::String scene = getCurrentScene();
    if (library == nullptr || scene.isEmpty())
    {
        error = "There is no scene to save into.";
        return false;
    }

    ScenePreset preset;
    preset.name = name;
    preset.id = Library::slug (name);

    ScenePreset existing;
    preset.slot = library->readScenePreset (scene, preset.id, existing) ? existing.slot
                                                                        : library->nextFreeScenePresetSlot (scene);
    if (preset.slot > 127)
    {
        error = "All 128 scene preset slots for this scene are used.";
        return false;
    }

    for (const auto& node : graphObj.nodes())
    {
        ScenePresetNode entry;
        entry.node = node->id();
        entry.active = node->activeParam()->on();

        const auto values = captureValues (*node);
        if (! values.empty())
        {
            // Reuse a saved node preset with identical settings, or save these as a new one.
            bool matched = false;
            for (const auto& np : library->listNodePresets (node->type()))
                if (sameValues (values, np.values))
                {
                    entry.presetId = np.id;
                    matched = true;
                    break;
                }

            if (! matched)
            {
                NodePreset np;
                np.nodeType = node->type();
                np.name = name + " - " + node->displayName();
                np.id = Library::slug (np.name);
                np.values = values;

                NodePreset old;
                np.slot = library->readNodePreset (np.nodeType, np.id, old) ? old.slot : library->nextFreeNodeSlot (np.nodeType);

                if (np.slot > 127 || ! library->writeNodePreset (np))
                {
                    error = "Could not save a preset for " + node->displayName() + ".";
                    return false;
                }
                entry.presetId = np.id;
            }
        }

        preset.nodes.push_back (entry);
    }

    for (const auto& edge : graphObj.edges())
    {
        ScenePresetEdge e;
        e.edge = edge.from + ">" + edge.to;
        e.enabled = edge.enabled->on();
        e.levelDb = edge.level->get();
        preset.edges.push_back (e);
    }

    if (! library->writeScenePreset (scene, preset))
    {
        error = "The scene preset file could not be written.";
        return false;
    }

    const juce::ScopedLock sl (sceneLock);
    currentScenePreset = preset.id;
    return true;
}
