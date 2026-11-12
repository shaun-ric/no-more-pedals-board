#include "SceneManager.h"

#include <algorithm>
#include <set>

namespace
{
    // The rig this app started with, used to give a scene that has no graph yet something to start from.
    GraphDef defaultGraphDef()
    {
        GraphDef g;

        auto node = [&g] (const char* id, const char* type, const char* name, bool active,
                          std::vector<std::pair<std::string, float>> props = {})
        {
            NodeDef d;
            d.id = id;
            d.type = type;
            d.name = name;
            d.startActive = active;
            d.props = std::move (props);
            g.nodes.push_back (std::move (d));
        };

        node ("guitar", "input", "Guitar input", true, { { "channel", 1.0f } });
        node ("gate", "gate", "Noise gate", false);
        node ("comp", "compressor", "Compressor", false);
        node ("drive", "overdrive", "Overdrive", false);
        node ("amp", "amp", "Amp (NAM)", true);
        node ("chorus", "chorus", "Chorus", false);
        node ("delay", "delay", "Delay", false);
        node ("reverb", "reverb", "Reverb", false);
        node ("ir", "ir", "Cab IR", true);
        node ("out1", "output", "Output 1 (FOH)", true, { { "channel", 1.0f }, { "level", -12.0f } });
        node ("out2", "output", "Output 2 (stage cab)", false, { { "channel", 2.0f }, { "level", -24.0f } });

        auto edge = [&g] (const char* from, const char* to, bool enabled = true)
        {
            EdgeDef e;
            e.from = from;
            e.to = to;
            e.enabled = enabled;
            g.edges.push_back (std::move (e));
        };

        edge ("guitar", "gate");
        edge ("gate", "comp");
        edge ("comp", "drive");
        edge ("drive", "amp");
        edge ("amp", "chorus");
        edge ("chorus", "delay");
        edge ("delay", "reverb");
        edge ("reverb", "ir");
        edge ("ir", "out1");
        edge ("reverb", "out2");
        edge ("amp", "out2", false);   // the dry amp to the stage cab, off until wanted

        return g;
    }

    std::string shortName (const Param& p) { return p.id.substr (p.nodeId.size() + 1); }
}

void SceneManager::start()
{
    router.onSelectScene = [this] (const juce::String& id) { selectScene (id); };

    auto scenes = library.listScenes();

    if (scenes.empty())
    {
        // First run: write a default scene (loading a missing file creates it).
        router.loadScene (library.sceneFile ("default"));
        router.setScenePc (0);
        ++library.revision;
        scenes = library.listScenes();
    }

    juce::String first = scenes.front().id;
    for (const auto& s : scenes)
        if (s.id == "default")
            first = s.id;

    publishPrograms();
    selectScene (first, true);
}

void SceneManager::selectScene (const juce::String& sceneId, bool atStartup, const juce::String& presetId)
{
    const juce::File file = library.sceneFile (sceneId);
    if (! file.existsAsFile())
        return;

    router.loadScene (file);        // its MIDI bindings
    rig.setCurrentScene (sceneId);
    currentId = sceneId;

    // Its graph. A scene saved by an earlier version has none, so it gets the starting rig, with the
    // model and IR it used before.
    if (! library.readGraphDef (file, currentDef))
    {
        currentDef = defaultGraphDef();

        const juce::var data = juce::JSON::parse (file);
        if (auto* assets = data.getProperty ("assets", juce::var()).getDynamicObject())
            for (const auto& nv : assets->getProperties())
                if (auto* node = currentDef.findNode (nv.name.toString().toStdString()))
                    node->file = nv.value.toString();

        library.writeGraphDef (file, currentDef);
    }

    const auto presets = library.listScenePresets (sceneId);

    // The preset that is about to be applied may carry its own capture, IR or plugin for a node. The scene's
    // own file for that node is then not loaded first, so a model is never loaded twice.
    {
        GraphDef forRig = currentDef;
        const juce::String wanted = presetId.isNotEmpty() ? presetId : (presets.empty() ? juce::String() : presets.front().id);
        ScenePreset sp;

        if (wanted.isNotEmpty() && library.readScenePreset (sceneId, wanted, sp))
            for (const auto& entry : sp.nodes)
            {
                auto* nd = forRig.findNode (entry.node);
                NodePreset np;
                if (nd != nullptr && entry.presetId.isNotEmpty() && library.readNodePreset (nd->type, entry.presetId, np)
                    && np.file.isNotEmpty())
                    nd->file = {};
            }

        applyGraph (&forRig);
    }

    if (presetId.isNotEmpty())
    {
        Command c;
        c.type = Command::Type::LoadScenePreset;
        c.text = presetId;
        rig.submit (std::move (c));
    }
    else if (! presets.empty())
    {
        Command c;
        c.type = Command::Type::LoadScenePreset;
        c.text = presets.front().id;
        rig.submit (std::move (c));
    }
    else if (! atStartup)
    {
        // A scene with no presets yet: return everything to its defaults.
        Command c;
        c.type = Command::Type::ResetState;
        rig.submit (std::move (c));
    }
}

bool SceneManager::deleteScene (const juce::String& sceneId, juce::String& error)
{
    const auto scenes = library.listScenes();
    if (scenes.size() <= 1)
    {
        error = "This is the only scene. Make another one before deleting it.";
        return false;
    }

    // If it is the one playing, move to another first.
    if (sceneId == currentId)
        for (const auto& s : scenes)
            if (s.id != sceneId)
            {
                selectScene (s.id);
                break;
            }

    if (! library.deleteScene (sceneId))
    {
        error = "The scene file could not be deleted.";
        return false;
    }

    return true;
}

bool SceneManager::createScene (const juce::String& name, int pc, juce::String& error)
{
    if (pc < -1 || pc > 127)
    {
        error = "The Program Change number must be between 0 and 127.";
        return false;
    }

    if (pc >= 0)
        for (const auto& s : library.listScenes())
            if (s.pc == pc)
            {
                error = "Program Change " + juce::String (pc) + " already selects the scene \"" + s.name + "\".";
                return false;
            }

    juce::String id = Library::slug (name);
    for (int n = 2; library.sceneFile (id).existsAsFile(); ++n)
        id = Library::slug (name) + "-" + juce::String (n);

    saveGraph();   // so the copy has the current graph, with its latest settings

    // Start from a copy of the current scene, so its graph and MIDI bindings carry over.
    const juce::var root = juce::JSON::parse (library.sceneFile (currentId));
    auto* obj = root.getDynamicObject();

    if (obj == nullptr)
    {
        error = "The current scene's file could not be read.";
        return false;
    }

    obj->setProperty ("name", name);
    obj->setProperty ("pc", pc);

    const juce::File file = library.sceneFile (id);
    file.getParentDirectory().createDirectory();

    if (! file.replaceWithText (juce::JSON::toString (root)))
    {
        error = "The scene file could not be written.";
        return false;
    }

    ++library.revision;
    publishPrograms();
    selectScene (id);
    return true;
}

void SceneManager::publishPrograms()
{
    std::vector<std::pair<int, juce::String>> programs;

    for (const auto& s : library.listScenes())
        if (s.pc >= 0)
            programs.emplace_back (s.pc, s.id);

    router.setScenePrograms (std::move (programs));
}

void SceneManager::applyGraph (const GraphDef* instead)
{
    Command c;
    c.type = Command::Type::ApplyGraph;
    c.graph = std::make_shared<GraphDef> (instead != nullptr ? *instead : currentDef);
    rig.submit (std::move (c));
}

juce::File SceneManager::printableMapFile() const
{
    return library.rootFolder().getChildFile ("scenes").getChildFile (currentId + ".midi-map.txt");
}

// ---- Editing the graph ------------------------------------------------------------------------------

void SceneManager::saveGraph()
{
    if (currentId.isEmpty())
        return;

    for (auto& nd : currentDef.nodes)
        for (auto* p : rig.params().all())
            if (p->nodeId == nd.id && p->structural)
            {
                const std::string name = shortName (*p);
                bool found = false;

                for (auto& kv : nd.props)
                    if (kv.first == name)
                    {
                        kv.second = p->get();
                        found = true;
                    }

                if (! found)
                    nd.props.emplace_back (name, p->get());
            }

    library.writeGraphDef (library.sceneFile (currentId), currentDef);
}

bool SceneManager::addNode (const std::string& type, const juce::String& name, juce::String& error)
{
    std::string base = Library::slug (name).toStdString();
    std::string id = base;

    for (int n = 2; currentDef.findNode (id) != nullptr; ++n)
        id = base + "-" + std::to_string (n);

    NodeDef d;
    d.id = id;
    d.type = type;
    d.name = name;
    d.startActive = false;   // a new node starts switched off, so adding one changes nothing you hear

    currentDef.nodes.push_back (d);
    saveGraph();
    applyGraph();
    juce::ignoreUnused (error);
    return true;
}

bool SceneManager::removeNode (const std::string& nodeId, juce::String& error)
{
    auto& nodes = currentDef.nodes;
    const auto before = nodes.size();

    nodes.erase (std::remove_if (nodes.begin(), nodes.end(), [&nodeId] (const NodeDef& n) { return n.id == nodeId; }), nodes.end());

    if (nodes.size() == before)
    {
        error = "There is no such node.";
        return false;
    }

    auto& edges = currentDef.edges;
    edges.erase (std::remove_if (edges.begin(), edges.end(),
                                 [&nodeId] (const EdgeDef& e) { return e.from == nodeId || e.to == nodeId; }),
                 edges.end());

    saveGraph();
    applyGraph();
    return true;
}

bool SceneManager::wouldLoop (const std::string& from, const std::string& to) const
{
    // A connection from -> to makes a loop if `from` can already be reached from `to`.
    std::set<std::string> seen;
    std::vector<std::string> stack { to };

    while (! stack.empty())
    {
        const std::string n = stack.back();
        stack.pop_back();

        if (n == from)
            return true;

        if (! seen.insert (n).second)
            continue;

        for (const auto& e : currentDef.edges)
            if (e.from == n)
                stack.push_back (e.to);
    }

    return false;
}

bool SceneManager::connectNodes (const std::string& from, const std::string& to, juce::String& error)
{
    if (from == to)
    {
        error = "A node cannot connect to itself.";
        return false;
    }

    if (currentDef.findNode (from) == nullptr || currentDef.findNode (to) == nullptr)
    {
        error = "Both nodes must exist.";
        return false;
    }

    for (const auto& e : currentDef.edges)
        if (e.from == from && e.to == to)
        {
            error = "These nodes are already connected.";
            return false;
        }

    if (wouldLoop (from, to))
    {
        error = "That connection would make a loop, which is not supported.";
        return false;
    }

    EdgeDef e;
    e.from = from;
    e.to = to;
    e.enabled = true;
    currentDef.edges.push_back (e);

    saveGraph();
    applyGraph();
    return true;
}

bool SceneManager::disconnectNodes (const std::string& from, const std::string& to)
{
    auto& edges = currentDef.edges;
    const auto before = edges.size();

    edges.erase (std::remove_if (edges.begin(), edges.end(),
                                 [&] (const EdgeDef& e) { return e.from == from && e.to == to; }),
                 edges.end());

    if (edges.size() == before)
        return false;

    saveGraph();
    applyGraph();
    return true;
}

void SceneManager::setNodeFile (const std::string& nodeId, const juce::File& file)
{
    auto* node = currentDef.findNode (nodeId);
    if (node == nullptr)
        return;

    node->file = file.getFullPathName();
    saveGraph();

    Command c;
    c.type = Command::Type::LoadFile;
    c.target = nodeId;
    c.file = file;
    c.value = 1.0f;   // the user asked for this file, so load it even if it looks the same
    rig.submit (std::move (c));
}


void SceneManager::setNodeName (const std::string& nodeId, const juce::String& name)
{
    auto* def = currentDef.findNode (nodeId);
    if (def == nullptr || name.trim().isEmpty())
        return;

    def->name = name.trim();

    if (const auto node = rig.graph().findNode (nodeId))
        node->setDisplayName (def->name);

    saveGraph();
    rig.graph().touch();   // views rebuild with the new label
}

void SceneManager::setNodeText (const std::string& nodeId, const juce::String& text)
{
    auto* def = currentDef.findNode (nodeId);
    if (def == nullptr)
        return;

    def->text = text;

    if (const auto node = rig.graph().findNode (nodeId))
        node->setTextValue (text);

    saveGraph();
}

void SceneManager::setNodeColour (const std::string& nodeId, juce::uint32 argb)
{
    auto* def = currentDef.findNode (nodeId);
    if (def == nullptr)
        return;

    def->colour = argb;

    if (const auto node = rig.graph().findNode (nodeId))
        node->setColour (argb);

    saveGraph();
}

void SceneManager::setNodePosition (const std::string& nodeId, float x, float y)
{
    if (auto* def = currentDef.findNode (nodeId))
    {
        def->posX = x;
        def->posY = y;
        saveGraph();
    }
}

void SceneManager::clearLayout()
{
    for (auto& n : currentDef.nodes)
        n.posX = n.posY = -1.0f;

    saveGraph();
}
