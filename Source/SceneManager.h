#pragma once

#include <juce_core/juce_core.h>

#include <string>

#include "Core/Library.h"
#include "Core/Rig.h"
#include "MidiRouter.h"

// Selects and creates scenes, and owns the current scene's graph definition (message thread).
// Selecting a scene loads its MIDI bindings into the router, tells the rig which scene is current, has
// the rig reconcile its live graph to the scene's graph, and applies the scene's first scene preset.
// Editing the graph changes the definition, saves it in the scene file, and has the rig apply it.
class SceneManager
{
public:
    SceneManager (Library& lib, Rig& rigToControl, MidiRouter& routerToUse)
        : library (lib), rig (rigToControl), router (routerToUse) {}

    // Makes sure there is a scene, selects one, and starts listening for Program Changes.
    void start();

    // `presetId` picks which scene preset to start in (empty = the scene's first one).
    void selectScene (const juce::String& sceneId, bool atStartup = false, const juce::String& presetId = {});
    bool createScene (const juce::String& name, int pc, juce::String& error);
    bool deleteScene (const juce::String& sceneId, juce::String& error);   // moves to another scene first if it is the current one
    juce::String currentSceneId() const { return currentId; }

    // ---- Editing the current scene's graph -------------------------------------------------------
    bool addNode (const std::string& type, const juce::String& name, juce::String& error);
    bool removeNode (const std::string& nodeId, juce::String& error);
    bool connectNodes (const std::string& from, const std::string& to, juce::String& error);
    bool disconnectNodes (const std::string& from, const std::string& to);
    void setNodeFile (const std::string& nodeId, const juce::File& file);
    void setNodeName (const std::string& nodeId, const juce::String& name);
    void setNodeText (const std::string& nodeId, const juce::String& text);   // a node's text setting (a MIDI output node's port)
    void setNodeColour (const std::string& nodeId, juce::uint32 argb);                       // 0 = by node type
    void setNodePosition (const std::string& nodeId, float x, float y);                      // 0 to 1 across and down
    void clearLayout();                                                                       // back to the automatic layout
    const GraphDef& graphDef() const { return currentDef; }

    // Writes the graph definition to the scene file, bringing structural settings (such as each node's
    // interface channel) up to date first.
    void saveGraph();

    juce::File printableMapFile() const;

private:
    void publishPrograms();
    void applyGraph (const GraphDef* instead = nullptr);   // sends the current graph (or `instead`) to the rig
    bool wouldLoop (const std::string& from, const std::string& to) const;

    Library& library;
    Rig& rig;
    MidiRouter& router;
    juce::String currentId;
    GraphDef currentDef;
};
