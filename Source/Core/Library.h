#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The files on disk. Everything is plain JSON, one file per item:
//
//   presets/<node type>/<id>.preset.json          a node preset: one node type's property values
//   scenes/<id>.scene.json                         a scene: its name, program number and MIDI bindings
//   scenes/<id>/presets/<id>.preset.json           a scene preset: which node presets a scene uses
//   setlists/<id>.setlist.json                     a setlist: scene and scene preset pairs, in order
//
// The library is stateless apart from a counter that tells the GUI when something was written.

struct NodePreset
{
    juce::String id, name;
    std::string nodeType;
    int slot = 0;                                          // the number a CC value selects
    std::vector<std::pair<std::string, float>> values;     // parameter short name -> value
    juce::String file;                                     // the capture, IR or plugin it uses, as stored ("lib:..." or a path); empty = none
};

struct ScenePresetNode
{
    std::string node;
    juce::String presetId;   // empty if the node has no settings to save
    bool active = false;
};

struct ScenePresetEdge
{
    std::string edge;        // "fromNode>toNode"
    bool enabled = true;
    float levelDb = 0.0f;
};

struct ScenePreset
{
    juce::String id, name;
    int slot = 0;
    std::vector<ScenePresetNode> nodes;
    std::vector<ScenePresetEdge> edges;
};

// A scene's graph: which nodes it has, what each one uses, and how they connect. It is saved in the
// scene file, and the rig reconciles its live graph to match it.
struct NodeDef
{
    std::string id, type;
    juce::String name;
    bool startActive = false;                              // whether the node starts switched on
    std::vector<std::pair<std::string, float>> props;      // setting name -> value: structural settings (such as
                                                           // the interface channel), and initial values
    juce::String text;                                     // a text setting, for nodes that have one (the MIDI output node's port)
    juce::String file;                                     // a NAM capture or cab IR path, for nodes that use one
                                                           // (always a full path here; on disk it is stored relative to the library)
    float posX = -1.0f, posY = -1.0f;                      // where the graph view draws it (0 to 1 across and down), or -1 for automatic
    juce::uint32 colour = 0;                               // ARGB, or 0 for the node type's colour
};

struct EdgeDef
{
    std::string from, to;
    bool enabled = true;                                   // whether the connection starts switched on
};

struct GraphDef
{
    std::vector<NodeDef> nodes;
    std::vector<EdgeDef> edges;

    NodeDef* findNode (const std::string& id)
    {
        for (auto& n : nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }
};

// One step in a setlist: a scene, and which of its scene presets to start in (empty = its first one).
struct SetlistEntry
{
    juce::String scene, preset;
    juce::String name;       // what the entry is called (a song title, say); empty = built from the scene and preset
    std::vector<juce::String> slots;   // what footswitch button 1, 2, 3... recalls in this song: a scene preset ID, or empty
};

struct Setlist
{
    juce::String id, name;
    std::vector<SetlistEntry> entries;
};

struct SceneInfo
{
    juce::String id, name;
    int pc = -1;             // the Program Change that selects this scene, or -1
};

class Library
{
public:
    explicit Library (const juce::File& rootFolder) : root (rootFolder) {}

    const juce::File& rootFolder() const noexcept { return root; }

    // A file-name-safe ID made from a display name.
    static juce::String slug (const juce::String& name);

    // Reads a JSON file (or starts an empty object), lets `edit` change it, and writes it back. Anything
    // `edit` does not touch is kept, so different parts of the app can each save their own fields in a
    // shared file, such as a scene.
    static bool updateJsonFile (const juce::File& file, const std::function<void (juce::DynamicObject&)>& edit);

    // Node presets
    std::vector<NodePreset> listNodePresets (const std::string& nodeType) const;   // sorted by slot
    bool readNodePreset (const std::string& nodeType, const juce::String& id, NodePreset& out) const;
    bool readNodePresetBySlot (const std::string& nodeType, int slot, NodePreset& out) const;
    bool writeNodePreset (const NodePreset& preset) const;
    bool deleteNodePreset (const std::string& nodeType, const juce::String& id) const;
    // How many scene presets (in any scene) use this node preset.
    int countNodePresetUsers (const std::string& nodeType, const juce::String& id) const;
    int nextFreeNodeSlot (const std::string& nodeType) const;

    // Scenes
    std::vector<SceneInfo> listScenes() const;
    juce::File sceneFile (const juce::String& sceneId) const;

    // A scene's graph, stored under "graph" in the scene file. Returns false if the file has none.
    bool readGraphDef (const juce::File& sceneFile, GraphDef& out) const;
    bool writeGraphDef (const juce::File& sceneFile, const GraphDef& def) const;

    // Capture and IR files. Scenes refer to files inside the library by a path relative to the library
    // folder ("lib:NAM/amp.nam"), so the whole folder can be moved or copied to another machine. A file
    // that is not where a scene says is looked for by name in the library's NAM or IR folder.
    juce::File assetFolder (bool ir) const { return root.getChildFile (ir ? "IR" : "NAM"); }
    juce::Array<juce::File> listAssets (bool ir) const;   // every .nam (or .wav) under that folder
    juce::File resolveAsset (const juce::String& stored, const std::string& nodeType) const;
    juce::String storedPathFor (const juce::File& file) const;

    // Scene presets
    std::vector<ScenePreset> listScenePresets (const juce::String& sceneId) const;   // sorted by slot
    bool readScenePreset (const juce::String& sceneId, const juce::String& id, ScenePreset& out) const;
    bool readScenePresetBySlot (const juce::String& sceneId, int slot, ScenePreset& out) const;
    bool writeScenePreset (const juce::String& sceneId, const ScenePreset& preset) const;
    int nextFreeScenePresetSlot (const juce::String& sceneId) const;
    bool deleteScenePreset (const juce::String& sceneId, const juce::String& id) const;
    bool deleteScene (const juce::String& sceneId) const;       // the scene file and all its scene presets
    // Setlist entries that point at a scene (and, if given, a scene preset).
    int countSetlistUsers (const juce::String& sceneId, const juce::String& presetId = {}) const;

    // Setlists: setlists/<id>.setlist.json, a named list of scene and scene preset pairs
    std::vector<Setlist> listSetlists() const;    // sorted by name
    bool readSetlist (const juce::String& id, Setlist& out) const;
    bool writeSetlist (const Setlist& setlist) const;
    bool deleteSetlist (const juce::String& id) const;

    // Bumped whenever anything is written, so the GUI knows to refresh its lists.
    mutable std::atomic<int> revision { 0 };

private:
    juce::File nodePresetFolder (const std::string& nodeType) const;
    juce::File scenePresetFolder (const juce::String& sceneId) const;

    juce::File root;
};
