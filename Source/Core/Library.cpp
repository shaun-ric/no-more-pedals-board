#include "Library.h"

#include <algorithm>

namespace
{
    constexpr const char* presetSuffix = ".preset.json";
    constexpr const char* sceneSuffix = ".scene.json";

    bool readNodePresetFile (const juce::File& f, NodePreset& out)
    {
        const juce::var root = juce::JSON::parse (f);
        if (! root.isObject())
            return false;

        out.id = root.getProperty ("id", juce::var()).toString();
        if (out.id.isEmpty())
            out.id = f.getFileName().upToLastOccurrenceOf (presetSuffix, false, false);

        out.name = root.getProperty ("name", juce::var (out.id)).toString();
        out.nodeType = root.getProperty ("nodeType", juce::var()).toString().toStdString();
        out.slot = (int) root.getProperty ("slot", juce::var (0));
        out.file = root.getProperty ("file", juce::var()).toString();
        out.values.clear();

        if (auto* obj = root.getProperty ("values", juce::var()).getDynamicObject())
            for (const auto& nv : obj->getProperties())
                out.values.emplace_back (nv.name.toString().toStdString(), (float) (double) nv.value);

        return true;
    }

    bool readScenePresetFile (const juce::File& f, ScenePreset& out)
    {
        const juce::var root = juce::JSON::parse (f);
        if (! root.isObject())
            return false;

        out.id = root.getProperty ("id", juce::var()).toString();
        if (out.id.isEmpty())
            out.id = f.getFileName().upToLastOccurrenceOf (presetSuffix, false, false);

        out.name = root.getProperty ("name", juce::var (out.id)).toString();
        out.slot = (int) root.getProperty ("slot", juce::var (0));
        out.nodes.clear();
        out.edges.clear();

        if (const auto* nodes = root.getProperty ("nodes", juce::var()).getArray())
        {
            for (const auto& n : *nodes)
            {
                ScenePresetNode e;
                e.node = n.getProperty ("node", juce::var()).toString().toStdString();
                e.presetId = n.getProperty ("preset", juce::var()).toString();
                e.active = (bool) n.getProperty ("active", juce::var (false));
                out.nodes.push_back (e);
            }
        }

        if (const auto* edges = root.getProperty ("edges", juce::var()).getArray())
        {
            for (const auto& n : *edges)
            {
                ScenePresetEdge e;
                e.edge = n.getProperty ("edge", juce::var()).toString().toStdString();
                e.enabled = (bool) n.getProperty ("enabled", juce::var (true));
                e.levelDb = (float) (double) n.getProperty ("level", juce::var (0.0));
                out.edges.push_back (e);
            }
        }

        return true;
    }
}

juce::String Library::slug (const juce::String& name)
{
    juce::String out;
    bool lastWasDash = true;

    for (const auto c : name.toLowerCase())
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            out += juce::String::charToString (c);
            lastWasDash = false;
        }
        else if (! lastWasDash)
        {
            out += "-";
            lastWasDash = true;
        }
    }

    out = out.trimCharactersAtEnd ("-");
    return out.isEmpty() ? juce::String ("preset") : out;
}

bool Library::updateJsonFile (const juce::File& file, const std::function<void (juce::DynamicObject&)>& edit)
{
    juce::var root = file.existsAsFile() ? juce::JSON::parse (file) : juce::var();

    if (root.getDynamicObject() == nullptr)
        root = juce::var (new juce::DynamicObject());

    edit (*root.getDynamicObject());

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (root));
}

juce::File Library::nodePresetFolder (const std::string& nodeType) const
{
    return root.getChildFile ("presets").getChildFile (juce::String (nodeType));
}

juce::File Library::scenePresetFolder (const juce::String& sceneId) const
{
    return root.getChildFile ("scenes").getChildFile (sceneId).getChildFile ("presets");
}

// ---- Node presets ---------------------------------------------------------------------------

std::vector<NodePreset> Library::listNodePresets (const std::string& nodeType) const
{
    std::vector<NodePreset> list;

    for (const auto& f : nodePresetFolder (nodeType).findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetSuffix))
    {
        NodePreset p;
        if (readNodePresetFile (f, p))
            list.push_back (std::move (p));
    }

    std::sort (list.begin(), list.end(), [] (const NodePreset& a, const NodePreset& b) { return a.slot < b.slot; });
    return list;
}

bool Library::readNodePreset (const std::string& nodeType, const juce::String& id, NodePreset& out) const
{
    const auto f = nodePresetFolder (nodeType).getChildFile (id + presetSuffix);
    return f.existsAsFile() && readNodePresetFile (f, out);
}

bool Library::readNodePresetBySlot (const std::string& nodeType, int slot, NodePreset& out) const
{
    for (auto& p : listNodePresets (nodeType))
        if (p.slot == slot)
        {
            out = std::move (p);
            return true;
        }

    return false;
}

bool Library::writeNodePreset (const NodePreset& preset) const
{
    auto* values = new juce::DynamicObject();
    for (const auto& kv : preset.values)
        values->setProperty (juce::Identifier (juce::String (kv.first)), (double) kv.second);

    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("id", preset.id);
    o->setProperty ("name", preset.name);
    o->setProperty ("nodeType", juce::String (preset.nodeType));
    o->setProperty ("slot", preset.slot);
    o->setProperty ("values", juce::var (values));
    if (preset.file.isNotEmpty())
        o->setProperty ("file", preset.file);

    const auto folder = nodePresetFolder (preset.nodeType);
    folder.createDirectory();
    const bool ok = folder.getChildFile (preset.id + presetSuffix).replaceWithText (juce::JSON::toString (juce::var (o)));

    ++revision;
    return ok;
}

bool Library::deleteNodePreset (const std::string& nodeType, const juce::String& id) const
{
    const bool ok = nodePresetFolder (nodeType).getChildFile (id + presetSuffix).deleteFile();
    ++revision;
    return ok;
}

int Library::countNodePresetUsers (const std::string& nodeType, const juce::String& id) const
{
    int count = 0;
    for (const auto& s : listScenes())
        for (const auto& sp : listScenePresets (s.id))
            for (const auto& n : sp.nodes)
                if (n.presetId == id)
                {
                    // The preset belongs to one node type, so check the node's type in the scene's graph.
                    GraphDef def;
                    if (readGraphDef (sceneFile (s.id), def))
                    {
                        const auto* nd = def.findNode (n.node);
                        if (nd != nullptr && nd->type == nodeType)
                            ++count;
                    }
                }
    return count;
}

int Library::nextFreeNodeSlot (const std::string& nodeType) const
{
    std::vector<bool> used (128, false);
    for (const auto& p : listNodePresets (nodeType))
        if (p.slot >= 0 && p.slot < 128)
            used[(size_t) p.slot] = true;

    for (int i = 0; i < 128; ++i)
        if (! used[(size_t) i])
            return i;

    return 128;   // every slot is taken
}

// ---- Scenes ---------------------------------------------------------------------------------

juce::File Library::sceneFile (const juce::String& sceneId) const
{
    return root.getChildFile ("scenes").getChildFile (sceneId + sceneSuffix);
}

std::vector<SceneInfo> Library::listScenes() const
{
    std::vector<SceneInfo> list;

    for (const auto& f : root.getChildFile ("scenes").findChildFiles (juce::File::findFiles, false, juce::String ("*") + sceneSuffix))
    {
        const juce::var v = juce::JSON::parse (f);
        if (! v.isObject())
            continue;

        SceneInfo s;
        s.id = f.getFileName().upToLastOccurrenceOf (sceneSuffix, false, false);
        s.name = v.getProperty ("name", juce::var (s.id)).toString();
        s.pc = (int) v.getProperty ("pc", juce::var (-1));
        list.push_back (s);
    }

    std::sort (list.begin(), list.end(), [] (const SceneInfo& a, const SceneInfo& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return list;
}

bool Library::readGraphDef (const juce::File& file, GraphDef& out) const
{
    const juce::var root = juce::JSON::parse (file);
    const juce::var graph = root.getProperty ("graph", juce::var());

    if (! graph.isObject())
        return false;

    out.nodes.clear();
    out.edges.clear();

    if (const auto* nodes = graph.getProperty ("nodes", juce::var()).getArray())
    {
        for (const auto& n : *nodes)
        {
            NodeDef d;
            d.id = n.getProperty ("id", juce::var()).toString().toStdString();
            d.type = n.getProperty ("type", juce::var()).toString().toStdString();
            d.name = n.getProperty ("name", juce::var (juce::String (d.id))).toString();
            d.startActive = (bool) n.getProperty ("active", juce::var (false));
            d.file = n.getProperty ("file", juce::var()).toString();
            if (d.file.isNotEmpty())
                d.file = resolveAsset (d.file, d.type).getFullPathName();

            d.text = n.getProperty ("text", juce::var()).toString();

            d.posX = (float) (double) n.getProperty ("x", juce::var (-1.0));
            d.posY = (float) (double) n.getProperty ("y", juce::var (-1.0));

            const juce::String colourText = n.getProperty ("colour", juce::var()).toString();
            d.colour = colourText.isNotEmpty() ? (juce::uint32) colourText.getHexValue64() : 0;

            if (auto* props = n.getProperty ("props", juce::var()).getDynamicObject())
                for (const auto& nv : props->getProperties())
                    d.props.emplace_back (nv.name.toString().toStdString(), (float) (double) nv.value);

            if (! d.id.empty() && ! d.type.empty())
                out.nodes.push_back (std::move (d));
        }
    }

    if (const auto* edges = graph.getProperty ("edges", juce::var()).getArray())
    {
        for (const auto& e : *edges)
        {
            EdgeDef d;
            d.from = e.getProperty ("from", juce::var()).toString().toStdString();
            d.to = e.getProperty ("to", juce::var()).toString().toStdString();
            d.enabled = (bool) e.getProperty ("enabled", juce::var (true));

            if (! d.from.empty() && ! d.to.empty())
                out.edges.push_back (std::move (d));
        }
    }

    return true;
}

bool Library::writeGraphDef (const juce::File& file, const GraphDef& def) const
{
    juce::Array<juce::var> nodes;
    for (const auto& n : def.nodes)
    {
        auto* props = new juce::DynamicObject();
        for (const auto& kv : n.props)
            props->setProperty (juce::Identifier (juce::String (kv.first)), (double) kv.second);

        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (n.id));
        o->setProperty ("type", juce::String (n.type));
        o->setProperty ("name", n.name);
        o->setProperty ("active", n.startActive);
        o->setProperty ("props", juce::var (props));

        if (n.file.isNotEmpty())
            o->setProperty ("file", storedPathFor (juce::File (n.file)));

        if (n.text.isNotEmpty())
            o->setProperty ("text", n.text);

        if (n.posX >= 0.0f && n.posY >= 0.0f)
        {
            o->setProperty ("x", (double) n.posX);
            o->setProperty ("y", (double) n.posY);
        }

        if (n.colour != 0)
            o->setProperty ("colour", juce::String::toHexString ((juce::int64) n.colour));

        nodes.add (juce::var (o));
    }

    juce::Array<juce::var> edges;
    for (const auto& e : def.edges)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("from", juce::String (e.from));
        o->setProperty ("to", juce::String (e.to));
        o->setProperty ("enabled", e.enabled);
        edges.add (juce::var (o));
    }

    auto* graph = new juce::DynamicObject();
    graph->setProperty ("nodes", nodes);
    graph->setProperty ("edges", edges);
    const juce::var graphVar (graph);

    return updateJsonFile (file, [&graphVar] (juce::DynamicObject& root) { root.setProperty ("graph", graphVar); });
}

// ---- Scene presets --------------------------------------------------------------------------

std::vector<ScenePreset> Library::listScenePresets (const juce::String& sceneId) const
{
    std::vector<ScenePreset> list;

    for (const auto& f : scenePresetFolder (sceneId).findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetSuffix))
    {
        ScenePreset p;
        if (readScenePresetFile (f, p))
            list.push_back (std::move (p));
    }

    std::sort (list.begin(), list.end(), [] (const ScenePreset& a, const ScenePreset& b) { return a.slot < b.slot; });
    return list;
}

bool Library::readScenePreset (const juce::String& sceneId, const juce::String& id, ScenePreset& out) const
{
    const auto f = scenePresetFolder (sceneId).getChildFile (id + presetSuffix);
    return f.existsAsFile() && readScenePresetFile (f, out);
}

bool Library::readScenePresetBySlot (const juce::String& sceneId, int slot, ScenePreset& out) const
{
    for (auto& p : listScenePresets (sceneId))
        if (p.slot == slot)
        {
            out = std::move (p);
            return true;
        }

    return false;
}

bool Library::writeScenePreset (const juce::String& sceneId, const ScenePreset& preset) const
{
    juce::Array<juce::var> nodes;
    for (const auto& n : preset.nodes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("node", juce::String (n.node));
        o->setProperty ("preset", n.presetId);
        o->setProperty ("active", n.active);
        nodes.add (juce::var (o));
    }

    juce::Array<juce::var> edges;
    for (const auto& e : preset.edges)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("edge", juce::String (e.edge));
        o->setProperty ("enabled", e.enabled);
        o->setProperty ("level", (double) e.levelDb);
        edges.add (juce::var (o));
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("version", 1);
    obj->setProperty ("id", preset.id);
    obj->setProperty ("name", preset.name);
    obj->setProperty ("slot", preset.slot);
    obj->setProperty ("nodes", nodes);
    obj->setProperty ("edges", edges);

    const auto folder = scenePresetFolder (sceneId);
    folder.createDirectory();
    const bool ok = folder.getChildFile (preset.id + presetSuffix).replaceWithText (juce::JSON::toString (juce::var (obj)));

    ++revision;
    return ok;
}

bool Library::deleteScenePreset (const juce::String& sceneId, const juce::String& id) const
{
    const bool ok = scenePresetFolder (sceneId).getChildFile (id + presetSuffix).deleteFile();
    ++revision;
    return ok;
}

bool Library::deleteScene (const juce::String& sceneId) const
{
    const auto folder = root.getChildFile ("scenes").getChildFile (sceneId);
    if (folder.isDirectory())
        folder.deleteRecursively();

    const bool ok = sceneFile (sceneId).deleteFile();
    ++revision;
    return ok;
}

int Library::countSetlistUsers (const juce::String& sceneId, const juce::String& presetId) const
{
    int count = 0;
    for (const auto& info : listSetlists())
    {
        Setlist s;
        if (readSetlist (info.id, s))
            for (const auto& e : s.entries)
                if (e.scene == sceneId && (presetId.isEmpty() || e.preset == presetId))
                    ++count;
    }
    return count;
}

int Library::nextFreeScenePresetSlot (const juce::String& sceneId) const
{
    std::vector<bool> used (128, false);
    for (const auto& p : listScenePresets (sceneId))
        if (p.slot >= 0 && p.slot < 128)
            used[(size_t) p.slot] = true;

    for (int i = 0; i < 128; ++i)
        if (! used[(size_t) i])
            return i;

    return 128;
}


// ---- Setlists ---------------------------------------------------------------------------------

namespace
{
    constexpr const char* setlistSuffix = ".setlist.json";
}

bool Library::readSetlist (const juce::String& id, Setlist& out) const
{
    const juce::File f = root.getChildFile ("setlists").getChildFile (id + setlistSuffix);
    const juce::var v = juce::JSON::parse (f);

    if (! v.isObject())
        return false;

    out = {};
    out.id = id;
    out.name = v.getProperty ("name", juce::var (id)).toString();

    if (auto* arr = v.getProperty ("entries", juce::var()).getArray())
        for (const auto& item : *arr)
        {
            SetlistEntry e;
            e.scene = item.getProperty ("scene", juce::var()).toString();
            e.preset = item.getProperty ("preset", juce::var()).toString();
            e.name = item.getProperty ("name", juce::var()).toString();
            if (const auto* slots = item.getProperty ("slots", juce::var()).getArray())
                for (const auto& sv : *slots)
                    e.slots.push_back (sv.toString());
            if (e.scene.isNotEmpty())
                out.entries.push_back (e);
        }

    return true;
}

std::vector<Setlist> Library::listSetlists() const
{
    std::vector<Setlist> list;

    for (const auto& f : root.getChildFile ("setlists").findChildFiles (juce::File::findFiles, false, juce::String ("*") + setlistSuffix))
    {
        Setlist s;
        if (readSetlist (f.getFileName().upToLastOccurrenceOf (setlistSuffix, false, false), s))
            list.push_back (std::move (s));
    }

    std::sort (list.begin(), list.end(), [] (const Setlist& a, const Setlist& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return list;
}

bool Library::writeSetlist (const Setlist& setlist) const
{
    juce::Array<juce::var> entries;
    for (const auto& e : setlist.entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("scene", e.scene);
        o->setProperty ("preset", e.preset);
        o->setProperty ("name", e.name);
        juce::Array<juce::var> slots;
        for (const auto& sl : e.slots)
            slots.add (sl);
        o->setProperty ("slots", slots);
        entries.add (juce::var (o));
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("name", setlist.name);
    o->setProperty ("entries", entries);

    const juce::File f = root.getChildFile ("setlists").getChildFile (setlist.id + setlistSuffix);
    f.getParentDirectory().createDirectory();
    const bool ok = f.replaceWithText (juce::JSON::toString (juce::var (o)));
    ++revision;
    return ok;
}

bool Library::deleteSetlist (const juce::String& id) const
{
    const bool ok = root.getChildFile ("setlists").getChildFile (id + setlistSuffix).deleteFile();
    ++revision;
    return ok;
}


// ---- Capture and IR files ------------------------------------------------------------------------

juce::Array<juce::File> Library::listAssets (bool ir) const
{
    juce::Array<juce::File> files;
    assetFolder (ir).findChildFiles (files, juce::File::findFiles, true, ir ? "*.wav" : "*.nam");
    return files;
}

juce::String Library::storedPathFor (const juce::File& file) const
{
    if (file != juce::File() && file.isAChildOf (root))
        return "lib:" + file.getRelativePathFrom (root).replaceCharacter ('\\', '/');

    return file.getFullPathName();
}

juce::File Library::resolveAsset (const juce::String& stored, const std::string& nodeType) const
{
    if (stored.isEmpty())
        return {};

    const juce::File f = stored.startsWith ("lib:") ? root.getChildFile (stored.fromFirstOccurrenceOf ("lib:", false, false))
                                                    : juce::File (stored);
    if (f.exists() || nodeType == "vst3" || nodeType == "vsti")   // a VST3 can be a folder; plugins are not searched for in the library
        return f;

    // Moved? Look for the same file name in the library.
    const auto found = listAssets (nodeType == "ir");
    for (const auto& candidate : found)
        if (candidate.getFileName().equalsIgnoreCase (f.getFileName()))
            return candidate;

    return f;   // still missing: the rig reports it
}
