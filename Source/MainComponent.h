#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

#include "AudioEngine.h"
#include "Core/Rig.h"
#include "LevelMeter.h"
#include "MidiRouter.h"
#include "SceneManager.h"
#include "NodesPanel.h"
#include "PlayView.h"
#include "GraphView.h"
#include "AssetBrowser.h"
#include "SetlistManager.h"
#include "StatusStrip.h"
#include "SetlistPanel.h"
#include "PluginWindow.h"
#include "StageView.h"

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildGraphViews();
    void rebuildPanel();
    void loadSettings();
    void saveSettings();
    void updatePlaySetlist();
    void updateStage();
    void pressSlot (int slot);   // footswitch button 1, 2, 3...: recalls what the current song assigns to it
    void applySlotCount (int n);
    void updateStatusStrip (const juce::StringArray& freshMidi);
    std::map<std::string, juce::Point<float>> savedPositions() const;
    GraphEditor makeEditor();
    void appendLogLine (const juce::String& line);
    void refreshSceneLists();
    void updateTabVisibility();
    int currentTab() const { return tabs.getCurrentTabIndex(); }

    Library library { juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RigBench") };
    Rig rig;
    AudioEngine engine { rig };
    MidiRouter midiRouter { rig };
    SceneManager sceneManager { library, rig, midiRouter };
    SetlistManager setlists { library, sceneManager };
    SetlistPanel setlistPanel { library, sceneManager, setlists, midiRouter, rig };
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;

    // Tabs: 0 Play, 1 Stage, 2 Edit, 3 Setlist, 4 Setup, 5 Diagnostics
    enum Tab { tabPlay = 0, tabStage, tabEdit, tabSetlist, tabSetup, tabDiagnostics };
    StageView stageView;
    int slotCount = 8;   // footswitch buttons, saved in settings.json
    // Global switching times (Setup tab), saved in settings.json.
    juce::Label captureFadeLabel, connectionFadeLabel;
    juce::Slider captureFadeSlider, connectionFadeSlider;

    // What the Play tab's top row is showing: a setlist's entries, or just the scenes.
    bool playUsesSetlist = false;
    int lastPlayRevision = -1;
    juce::String lastPlaySetlist;
    int lastPlayEntries = -1;
    juce::StringArray playNames, playIds;

    juce::TabbedButtonBar tabs { juce::TabbedButtonBar::TabsAtTop };
    PlayView playView;
    std::unique_ptr<LevelMeter> playMeter;

    // The Edit tab: the graph picture, a toolbar, and the settings of whatever is selected.
    GraphView graphView;
    juce::TextButton addNodeButton { "Add node..." }, connectButton { "Connect..." };
    juce::TextButton resetLayoutButton { "Reset layout" };
    juce::Label editHint;

    StatusStrip statusStrip;
    juce::String lastMidiLine;
    juce::Time lastMidiTime;

    juce::Viewport viewport;
    std::unique_ptr<NodesPanel> nodesPanel;
    std::map<std::string, std::unique_ptr<PluginWindow>> pluginWindows;   // a VST3 node's own window, by node ID
    void openPluginWindow (const std::string& nodeId);
    void prunePluginWindows();
    std::unique_ptr<LevelMeter> meter;

    juce::TextButton resetButton { "Reset stats" };
    juce::ToggleButton loopback { "Loopback latency test (replaces the rig signal)" };
    juce::Label loopInLabel, loopOutLabel;
    juce::ComboBox loopInBox, loopOutBox;

    // Scene bar
    juce::Label sceneLabel, scenePresetLabel;
    juce::ComboBox sceneBox, scenePresetBox;
    juce::TextButton newSceneButton { "New scene..." }, saveScenePresetButton { "Save preset..." };
    juce::TextButton deleteSceneButton { "Delete scene" }, deleteScenePresetButton { "Delete preset" };
    juce::TextButton scenePresetLearn { "MIDI" }, scenePresetClear { "x" };
    juce::Label recallLabel;
    juce::TextButton recallLearn { "MIDI" }, recallClear { "x" };   // recall the preset chosen above
    juce::StringArray sceneIds, scenePresetIds;
    int lastLibraryRevision = -1;
    int lastGraphRevision = -1;
    juce::String lastDefaultsScene;
    juce::String lastShownScene, lastShownPreset;

    juce::TextEditor statsBox;
    juce::TextEditor midiBox;
    juce::StringArray midiLines;
    juce::String midiNotice;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String buildError;

    juce::Time statsStart;
    juce::File logFile;
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
