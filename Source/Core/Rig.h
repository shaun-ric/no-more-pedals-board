#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Graph.h"
#include "NoteBus.h"
#include "Library.h"
#include "Param.h"

// Everything that changes the rig is a Command. Any thread can submit one; the control
// thread applies them in order. The GUI, MIDI and scenes all use this path.
struct Command
{
    enum class Type { SetParam, ToggleParam, LoadFile, LoadNodePreset, LoadScenePreset, ResetState, ApplyGraph };

    Type type = Type::SetParam;
    std::string target;    // parameter ID, or node ID for loads
    float value = 0.0f;
    juce::File file;       // for LoadFile; a non-zero `value` forces a reload even if it is already loaded
    juce::String text;     // a preset ID, for the preset commands
    int slot = -1;         // or a preset slot number (what a MIDI CC value selects)
    std::shared_ptr<GraphDef> graph;   // for ApplyGraph
};

// The control layer: owns the parameter registry and the signal graph, applies commands on its
// own thread, and does the slow work (building the graph, loading models and IRs) off the audio thread.
// The graph starts empty. A scene's graph definition is applied with Command::Type::ApplyGraph, which
// changes the live graph to match: nodes that are still wanted stay as they are (so their loaded
// models are kept), and only the differences are added or removed.
class Rig : private juce::Thread
{
public:
    Rig();
    ~Rig() override;

    void start();                       // starts the control thread
    void submit (Command command);      // any thread

    ParamRegistry& params() noexcept { return registry; }
    Graph& graph() noexcept { return graphObj; }
    NoteBus& notes() noexcept { return noteBus; }   // notes for the VST3 instruments
    juce::String getStatusMessage() const;

    // ---- Scenes and presets -------------------------------------------------------------------
    void setLibrary (Library* lib) noexcept { library = lib; }
    Library* getLibrary() const noexcept { return library; }

    void setCurrentScene (const juce::String& sceneId);
    juce::String getCurrentScene() const;
    juce::String getCurrentScenePreset() const;
    juce::String getNodePresetId (const std::string& nodeId) const;   // the preset last loaded or saved for a node

    // Save the current settings (message thread). The scene preset links to saved node presets, and
    // creates a node preset for any node whose settings are not already saved.
    bool saveNodePreset (const std::string& nodeId, const juce::String& name, juce::String& error);
    bool saveScenePreset (const juce::String& name, juce::String& error);

    // 1 = outputs open, 0 = faded out. The audio engine follows this around swaps and loads.
    std::atomic<float> fadeTarget { 1.0f };

private:
    void run() override;
    void apply (const Command& command);
    void applyGraph (const GraphDef& def);
    void loadNodeFile (const std::string& nodeId, const juce::File& file, bool force);
    void fade (bool down);          // calls nest: the outputs come back when the outermost one ends
    void loadPresetFile (const std::string& nodeId, const NodePreset& preset);   // loads a preset's file unless it is already loaded
    int fadeDepth = 0;
    juce::String currentFileRef (const Node& node) const;
    std::string nodeTypeOf (const std::string& nodeId) const;
    void setStatus (const juce::String& text);

    void loadNodePreset (const std::string& nodeId, const juce::String& presetId, int slot);
    void loadScenePreset (const juce::String& presetId, int slot);
    void applyScenePreset (const ScenePreset& preset);
    void resetToDefaults();
    std::vector<std::pair<std::string, float>> captureValues (const Node& node) const;

    ParamRegistry registry;
    NoteBus noteBus;
    Graph graphObj { registry };

    juce::CriticalSection queueLock;
    std::deque<Command> queue;

    mutable juce::CriticalSection statusLock;
    juce::String status;

    Library* library = nullptr;
    mutable juce::CriticalSection sceneLock;
    juce::String currentScene, currentScenePreset;
    std::map<std::string, juce::String> nodePresetIds;
};
