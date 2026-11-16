#include "MainComponent.h"

#include "Nodes/VstNode.h"

#include <cmath>

#include "Core/PitchDetector.h"
#include "Nodes/AmpNodes.h"
#include "Nodes/IoNodes.h"

namespace
{
    juce::File deviceStateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("RigBench").getChildFile ("device.xml");
    }

    juce::File logFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RigBench");
    }
}

MainComponent::MainComponent()
{
    events.add ("RigBench " + juce::JUCEApplication::getInstance()->getApplicationVersion() + " started");
    events.flush();

    // ---- The rig: its graph starts empty and is built from the current scene ------
    rig.setLibrary (&library);
    rig.start();

    // ---- Audio device: restore last state, otherwise prefer ASIO ----------
    std::unique_ptr<juce::XmlElement> saved;
    if (deviceStateFile().existsAsFile())
        saved = juce::XmlDocument::parse (deviceStateFile());

    deviceManager.initialise (2, 2, saved.get(), true);

    if (saved == nullptr)
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
                deviceManager.setCurrentAudioDeviceType ("ASIO", true);

    // The device to keep connected to. From the saved settings when there are any, because a device that is
    // not plugged in at startup is not open, and it is still the one wanted.
    if (saved != nullptr)
        readIntendedDevice (*saved);
    else
    {
        captureIntendedDevice();
        saveDeviceState();
    }

    deviceManager.addChangeListener (this);   // device changes made on the Setup tab are saved straight away

    if (buildError.isEmpty())
        deviceManager.addAudioCallback (&engine);

    selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, 2, 0, 2,
                                                                     true, false, false, false);
    addAndMakeVisible (*selector);

    // ---- Generic controls and meters: built whenever the graph changes -----------
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // ---- Buttons and diagnostics -------------------------------------------
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { engine.resetStats(); statsStart = juce::Time::getCurrentTime(); };

    addAndMakeVisible (loopback);
    loopback.onClick = [this] { engine.loopbackMode = loopback.getToggleState(); engine.resetStats(); };

    loopInLabel.setText ("Loopback input", juce::dontSendNotification);
    loopOutLabel.setText ("Loopback output", juce::dontSendNotification);
    addAndMakeVisible (loopInLabel);
    addAndMakeVisible (loopOutLabel);

    loopInBox.addItem ("Input 1", 1);
    loopInBox.addItem ("Input 2", 2);
    loopInBox.setSelectedId (1, juce::dontSendNotification);
    loopInBox.onChange = [this] { engine.loopbackInput = loopInBox.getSelectedId() - 1; engine.resetStats(); };
    addAndMakeVisible (loopInBox);

    loopOutBox.addItem ("Output 1 (FOH)", 1);
    loopOutBox.addItem ("Output 2 (stage)", 2);
    loopOutBox.setSelectedId (1, juce::dontSendNotification);
    loopOutBox.onChange = [this] { engine.loopbackOutput = loopOutBox.getSelectedId() - 1; engine.resetStats(); };
    addAndMakeVisible (loopOutBox);

    // ---- Stats --------------------------------------------------------------
    statsBox.setMultiLine (true);
    statsBox.setReadOnly (true);
    statsBox.setCaretVisible (false);
    statsBox.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    addAndMakeVisible (statsBox);

    // ---- Log file -----------------------------------------------------------
    logFolder().createDirectory();
    logFile = logFolder().getChildFile ("bench-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".csv");
    logFile.appendText ("time,sample_rate,buffer,cpu_mean_pct,cpu_peak_pct,overruns,driver_xruns,loop_avg_ms\n");

    // ---- MIDI switching ---------------------------------------------------------
    // The setlist entry that was playing when the app last closed: its scene is selected first, so the
    // rig comes up ready for that song instead of loading the default scene and then the song.
    const StartupEntry startup = readStartupEntry();

    if (buildError.isEmpty())
    {
        if (! midiRouter.loadOrCreate (logFolder().getChildFile ("midi-map.json"), midiNotice) && midiNotice.isNotEmpty())
            midiNotice = "MIDI map: " + midiNotice;

        midiRouter.setEventLog (&events);
        // Changing song or part leaves the tuner, so a forgotten tuner never leaves a song silent.
        midiRouter.onSetlistStep = [this] (int delta) { setTuner (false); setlists.step (delta); };
        midiRouter.onSlot = [this] (int slot) { setTuner (false); pressSlot (slot); };
        midiRouter.onTuner = [this] { setTuner (! engine.tunerOn.load()); };
        sceneManager.start (startup.scene, startup.preset);

        deviceManager.addMidiInputDeviceCallback ({}, &midiRouter);
    }

    // ---- Scene bar --------------------------------------------------------------
    sceneLabel.setText ("Scene", juce::dontSendNotification);
    scenePresetLabel.setText ("Scene preset", juce::dontSendNotification);
    addAndMakeVisible (sceneLabel);
    addAndMakeVisible (scenePresetLabel);
    addAndMakeVisible (sceneBox);
    addAndMakeVisible (scenePresetBox);
    addAndMakeVisible (newSceneButton);
    addAndMakeVisible (saveScenePresetButton);
    addAndMakeVisible (deleteSceneButton);
    addAndMakeVisible (deleteScenePresetButton);
    addAndMakeVisible (scenePresetLearn);
    addChildComponent (scenePresetClear);

    recallLabel.setText ("Recall preset", juce::dontSendNotification);
    addAndMakeVisible (recallLabel);
    addAndMakeVisible (recallLearn);
    addChildComponent (recallClear);

    recallLearn.onClick = [this]
    {
        const int i = scenePresetBox.getSelectedItemIndex();
        if (i < 0 || i >= scenePresetIds.size())
            return;

        const std::string target = "scenepreset:" + scenePresetIds[i].toStdString();
        if (midiRouter.learningParam() == target)
            midiRouter.cancelLearn();
        else
            midiRouter.startLearn (target, false);
    };

    recallClear.onClick = [this]
    {
        const int i = scenePresetBox.getSelectedItemIndex();
        if (i >= 0 && i < scenePresetIds.size())
            midiRouter.clearSceneBinding ("scenepreset:" + scenePresetIds[i].toStdString());
    };

    sceneBox.onChange = [this]
    {
        const int i = sceneBox.getSelectedItemIndex();
        if (i >= 0 && i < sceneIds.size() && sceneIds[i] != sceneManager.currentSceneId())
            sceneManager.selectScene (sceneIds[i]);
    };

    scenePresetBox.onChange = [this]
    {
        const int i = scenePresetBox.getSelectedItemIndex();
        if (i < 0 || i >= scenePresetIds.size())
            return;

        Command c;
        c.type = Command::Type::LoadScenePreset;
        c.text = scenePresetIds[i];
        rig.submit (std::move (c));
    };

    newSceneButton.onClick = [this]
    {
        askScene ([this] (const juce::String& name, int pc)
        {
            juce::String error;
            if (! sceneManager.createScene (name, pc, error))
                showError (error);
        });
    };

    deleteSceneButton.onClick = [this]
    {
        const int i = sceneBox.getSelectedItemIndex();
        if (i < 0 || i >= sceneIds.size())
            return;

        const juce::String id = sceneIds[i];
        const juce::String shown = sceneBox.getText().upToFirstOccurrenceOf ("  (PC", false, false);
        const int presets = (int) library.listScenePresets (id).size();
        const int listed = library.countSetlistUsers (id);

        juce::String message = "Delete the scene \"" + shown + "\"";
        if (presets > 0)
            message << " and its " << presets << (presets == 1 ? " scene preset" : " scene presets");
        message << "? Saved node presets and the capture and IR files are not touched.";
        if (listed > 0)
            message << "\n\nIt is used by " << listed << (listed == 1 ? " setlist entry" : " setlist entries")
                    << ", which will show as missing.";

        askConfirm ("Delete scene", message, [this, id]
        {
            juce::String error;
            if (! sceneManager.deleteScene (id, error))
                showError (error);
        });
    };

    deleteScenePresetButton.onClick = [this]
    {
        const int i = scenePresetBox.getSelectedItemIndex();
        if (i < 0 || i >= scenePresetIds.size())
            return;

        const juce::String scene = sceneManager.currentSceneId();
        const juce::String id = scenePresetIds[i];
        const juce::String shown = scenePresetBox.getText().upToFirstOccurrenceOf ("  (slot", false, false);
        const int listed = library.countSetlistUsers (scene, id);

        juce::String message = "Delete the scene preset \"" + shown + "\"? Saved node presets are not touched.";
        if (listed > 0)
            message << "\n\nIt is used by " << listed << (listed == 1 ? " setlist entry" : " setlist entries")
                    << ", which will show as missing.";

        askConfirm ("Delete scene preset", message, [this, scene, id]
        {
            library.deleteScenePreset (scene, id);
        });
    };

    saveScenePresetButton.onClick = [this]
    {
        askName ("Save scene preset", "Name for this scene preset. Saving over an existing name replaces it.",
                 [this]
                 {
                     // The preset's own name, not its file id (which is the lowercase, hyphenated slug).
                     const juce::String id = rig.getCurrentScenePreset();
                     for (const auto& p : library.listScenePresets (rig.getCurrentScene()))
                         if (p.id == id)
                             return p.name;
                     return id;
                 }(), [this] (const juce::String& name)
                 {
                     juce::String error;
                     if (! rig.saveScenePreset (name, error))
                         showError (error);
                 });
    };

    scenePresetLearn.onClick = [this]
    {
        if (midiRouter.learningParam() == "scenepreset")
            midiRouter.cancelLearn();
        else
            midiRouter.startLearn ("scenepreset", false);
    };
    scenePresetClear.onClick = [this] { midiRouter.clearSceneBinding ("scenepreset"); };

    midiBox.setMultiLine (true);
    midiBox.setReadOnly (true);
    midiBox.setCaretVisible (false);
    midiBox.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    midiBox.setText ((midiNotice.isNotEmpty() ? midiNotice + "\n\n" : juce::String())
                     + "MIDI monitor\nTick your MIDI adapter under MIDI Inputs on the left, then press a switch.\nTo bind a control, click MIDI beside it, then press a switch or move a pedal.\n"
                     + "Each scene has its own addresses, listed in Documents\\RigBench\\scenes\\<scene>.midi-map.txt.", false);
    addAndMakeVisible (midiBox);

    // ---- Tabs and the playing view --------------------------------------------
    for (auto name : { "Play", "Stage", "Edit", "Setlist", "Setup", "Diagnostics" })
        tabs.addTab (name, juce::Colour (0xff23262a), -1);

    tabs.setMinimumTabScaleFactor (1.0);
    tabs.setTabBackgroundColour (0, juce::Colour (0xff2b6a3d));
    addAndMakeVisible (tabs);
    // On stage the app opens on the Stage tab, at the song it was on. Without a setlist it opens on Edit.
    tabs.setCurrentTabIndex (startup.index >= 0 ? tabStage : tabEdit, false);
    tabs.addChangeListener (this);

    playView.onSelectScene = [this] (const juce::String& id)
    {
        if (playUsesSetlist)
            setlists.go (id.getIntValue());   // the buttons are the setlist's entries
        else if (id != sceneManager.currentSceneId())
            sceneManager.selectScene (id);
    };

    playView.onToggleNode = [this] (const std::string& nodeId)
    {
        Command c;
        c.type = Command::Type::ToggleParam;
        c.target = nodeId + "/active";
        rig.submit (std::move (c));
    };

    addChildComponent (playView);

    stageView.onStep = [this] (int delta) { setTuner (false); setlists.step (delta); };
    stageView.onSlot = [this] (int slot) { setTuner (false); pressSlot (slot); };
    stageView.onTuner = [this] { setTuner (! engine.tunerOn.load()); };
    tunerTimer.onTick = [this] { tunerTick(); };
    stageView.onToggleNode = [this] (const std::string& nodeId)
    {
        Command c;
        c.type = Command::Type::ToggleParam;
        c.target = nodeId + "/active";
        rig.submit (std::move (c));
    };
    addChildComponent (stageView);

    // The mouse wheel never changes a setting: it only scrolls.
    for (auto* box : { &loopInBox, &loopOutBox, &sceneBox, &scenePresetBox })
        box->setScrollWheelEnabled (false);

    // ---- Setlist tab ------------------------------------------------------------
    setlistPanel.onSetlistChanged = [this] { saveSettings(); };
    setlistPanel.onSlotCountChanged = [this] (int n) { applySlotCount (n); };
    addChildComponent (setlistPanel);

    addAndMakeVisible (statusStrip);

    // ---- Setup tab: global switching times ---------------------------------------
    captureFadeLabel.setText ("Capture on/off fade", juce::dontSendNotification);
    connectionFadeLabel.setText ("Connection on/off fade", juce::dontSendNotification);

    auto setupFade = [this] (juce::Slider& s, const char* paramId)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setScrollWheelEnabled (false);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
        s.setRange (0.0, 250.0, 1.0);
        s.setTextValueSuffix (" ms");
        s.setValue (10.0, juce::dontSendNotification);

        const std::string id = paramId;
        s.onValueChange = [this, &s, id]
        {
            if (auto* p = rig.params().find (id))
                p->set ((float) s.getValue());

            if (! s.isMouseButtonDown())
                saveSettings();
        };
        s.onDragEnd = [this] { saveSettings(); };
        addChildComponent (s);
    };

    setupFade (captureFadeSlider, "rig/captureFadeMs");
    setupFade (connectionFadeSlider, "rig/connectionFadeMs");
    addChildComponent (captureFadeLabel);
    addChildComponent (connectionFadeLabel);

    // ---- Setup tab: the tuner's footswitch, and the fallback for part changes -----------------------
    tunerLabel.setText ("Tuner footswitch", juce::dontSendNotification);
    tunerLearn.onClick = [this]
    {
        if (midiRouter.learningParam() == "tuner")
            midiRouter.cancelLearn();
        else
            midiRouter.startLearn ("tuner", false);
    };
    tunerClear.onClick = [this] { midiRouter.clearSceneBinding ("tuner"); };

    presetMuteToggle.onClick = [this]
    {
        if (auto* p = rig.params().find ("rig/presetMute"))
            p->set (presetMuteToggle.getToggleState() ? 1.0f : 0.0f);
        saveSettings();
    };

    addChildComponent (tunerLabel);
    addChildComponent (tunerLearn);
    addChildComponent (tunerClear);
    addChildComponent (presetMuteToggle);

    // ---- Edit tab: graph picture and toolbar ------------------------------------
    graphView.onSelect = [this] (const GraphSelection&)
    {
        rebuildPanel();
        resized();
    };

    graphView.onToggleNode = [this] (const std::string& nodeId)
    {
        Command c;
        c.type = Command::Type::ToggleParam;
        c.target = nodeId + "/active";
        rig.submit (std::move (c));
    };

    graphView.onConnect = [this] (const std::string& from, const std::string& to)
    {
        juce::String error;
        if (! sceneManager.connectNodes (from, to, error) && error.isNotEmpty())
            showError (error);
    };

    graphView.onMoveNode = [this] (const std::string& nodeId, float x, float y) { sceneManager.setNodePosition (nodeId, x, y); };

    resetLayoutButton.onClick = [this]
    {
        sceneManager.clearLayout();
        graphView.setGraph (rig.graph(), savedPositions());
    };
    addAndMakeVisible (resetLayoutButton);

    graphView.onDisconnect = [this] (const std::string& from, const std::string& to)
    {
        sceneManager.disconnectNodes (from, to);
    };

    graphView.onReconnect = [this] (const std::string& oldFrom, const std::string& oldTo,
                                    const std::string& newFrom, const std::string& newTo)
    {
        // Remember the old connection's level and on/off state, to carry them to the new one.
        float level = 0.0f;
        bool enabled = true;
        for (const auto& e : rig.graph().edges())
            if (e.from == oldFrom && e.to == oldTo)
            {
                level = e.level != nullptr ? e.level->get() : 0.0f;
                enabled = e.enabled == nullptr || e.enabled->on();
            }

        juce::String error;
        if (! sceneManager.connectNodes (newFrom, newTo, error))
        {
            if (error.isNotEmpty())
                showError (error);
            return;   // the old connection stays
        }

        sceneManager.disconnectNodes (oldFrom, oldTo);

        const std::string group = "edge:" + newFrom + ">" + newTo;

        Command lv;
        lv.type = Command::Type::SetParam;
        lv.target = group + "/level";
        lv.value = level;
        rig.submit (std::move (lv));

        Command en;
        en.type = Command::Type::SetParam;
        en.target = group + "/enabled";
        en.value = enabled ? 1.0f : 0.0f;
        rig.submit (std::move (en));
    };

    addAndMakeVisible (graphView);

    addNodeButton.onClick = [this]
    {
        std::vector<NewNodeChoice> types;
        for (const auto& t : nodeTypes())
            types.push_back ({ t.type, t.label, t.category });

        askNewNode (types, [this] (const std::string& type, const juce::String& name)
        {
            juce::String error;
            if (! sceneManager.addNode (type, name, error))
                showError (error);
        });
    };

    connectButton.onClick = [this]
    {
        std::vector<std::pair<std::string, juce::String>> choices;
        for (const auto& n : rig.graph().nodes())
            choices.emplace_back (n->id(), n->displayName());

        askConnect (choices, [this] (const std::string& from, const std::string& to)
        {
            juce::String error;
            if (! sceneManager.connectNodes (from, to, error) && error.isNotEmpty())
                showError (error);
        });
    };

    addAndMakeVisible (addNodeButton);
    addAndMakeVisible (connectButton);

    editHint.setText ("Click to select.  Drag a box to place it.  Power button: on/off.  Drag from a right dot to connect; drag a wire's arrow end to move it, or into empty space to break it.",
                      juce::dontSendNotification);
    editHint.setColour (juce::Label::textColourId, juce::Colour (0xff8a8f96));
    editHint.setFont (13.0f);
    addAndMakeVisible (editHint);

    loadSettings();   // after the sliders exist: switching times and the current setlist

    if (startup.index >= 0)
        setlists.markPlaying (startup.index);   // its scene was selected above

    statsStart = juce::Time::getCurrentTime();
    updateTabVisibility();
    startTimerHz (4);
    setSize (1400, 1000);
}

MainComponent::~MainComponent()
{
    stopTimer();
    tunerTimer.stopTimer();
    engine.tunerOn = false;
    pluginWindows.clear();
    tabs.removeChangeListener (this);
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &midiRouter);
    deviceManager.removeAudioCallback (&engine);
    midiRouter.setEventLog (nullptr);

    // The device settings are saved whenever they are changed on the Setup tab, not here: after a dropout
    // the open device may be a stand-in, and saving it would make it the one the app reconnects to.

    events.add ("RigBench closed");
    events.flush();
}

void MainComponent::updateTabVisibility()
{
    const int t = currentTab();
    const bool play = t == tabPlay, stage = t == tabStage, edit = t == tabEdit, setlistTab = t == tabSetlist,
               setup = t == tabSetup, diag = t == tabDiagnostics;

    playView.setVisible (play);
    stageView.setVisible (stage);
    if (playMeter != nullptr) playMeter->setVisible (play);

    for (juce::Component* c : { (juce::Component*) &sceneLabel, (juce::Component*) &scenePresetLabel,
                                (juce::Component*) &sceneBox, (juce::Component*) &scenePresetBox,
                                (juce::Component*) &newSceneButton, (juce::Component*) &saveScenePresetButton,
                                (juce::Component*) &deleteSceneButton, (juce::Component*) &deleteScenePresetButton,
                                (juce::Component*) &scenePresetLearn, (juce::Component*) &recallLabel,
                                (juce::Component*) &recallLearn, (juce::Component*) &viewport,
                                (juce::Component*) &graphView, (juce::Component*) &addNodeButton,
                                (juce::Component*) &connectButton, (juce::Component*) &editHint,
                                (juce::Component*) &resetLayoutButton })
        c->setVisible (edit);

    scenePresetClear.setVisible (edit && midiRouter.sceneBindingText ("scenepreset").isNotEmpty());
    if (meter != nullptr) meter->setVisible (diag);

    setlistPanel.setVisible (setlistTab);

    selector->setVisible (setup);
    midiBox.setVisible (setup);
    for (juce::Component* c : { (juce::Component*) &captureFadeLabel, (juce::Component*) &connectionFadeLabel,
                                (juce::Component*) &captureFadeSlider, (juce::Component*) &connectionFadeSlider,
                                (juce::Component*) &tunerLabel, (juce::Component*) &tunerLearn,
                                (juce::Component*) &presetMuteToggle })
        c->setVisible (setup);

    tunerClear.setVisible (setup && midiRouter.sceneBindingText ("tuner").isNotEmpty());

    for (juce::Component* c : { (juce::Component*) &resetButton, (juce::Component*) &loopback,
                                (juce::Component*) &loopInLabel, (juce::Component*) &loopOutLabel,
                                (juce::Component*) &loopInBox, (juce::Component*) &loopOutBox,
                                (juce::Component*) &statsBox })
        c->setVisible (diag);

    resized();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        // Only a change made by hand on the Setup tab picks a new device to keep. Changes made anywhere else
        // are the app's own reconnection attempts, or the driver falling back to another device.
        if (currentTab() == tabSetup)
        {
            captureIntendedDevice();
            saveDeviceState();
        }
        return;
    }

    updateTabVisibility();
}

// ===== Audio device recovery ============================================================================

void MainComponent::captureIntendedDevice()
{
    intendedType = deviceManager.getCurrentAudioDeviceType();
    intendedSetup = deviceManager.getAudioDeviceSetup();
    haveIntendedDevice = deviceManager.getCurrentAudioDevice() != nullptr;   // none chosen: nothing to reconnect to
    deviceLost = false;
}

void MainComponent::readIntendedDevice (const juce::XmlElement& saved)
{
    // The same attributes AudioDeviceManager::initialise reads from its own saved state.
    juce::AudioDeviceManager::AudioDeviceSetup setup = deviceManager.getAudioDeviceSetup();

    if (saved.getStringAttribute ("audioDeviceName").isNotEmpty())
    {
        setup.inputDeviceName = setup.outputDeviceName = saved.getStringAttribute ("audioDeviceName");
    }
    else
    {
        setup.inputDeviceName = saved.getStringAttribute ("audioInputDeviceName");
        setup.outputDeviceName = saved.getStringAttribute ("audioOutputDeviceName");
    }

    setup.bufferSize = saved.getIntAttribute ("audioDeviceBufferSize", setup.bufferSize);
    setup.sampleRate = saved.getDoubleAttribute ("audioDeviceRate", setup.sampleRate);
    setup.inputChannels.parseString (saved.getStringAttribute ("audioDeviceInChans", "11"), 2);
    setup.outputChannels.parseString (saved.getStringAttribute ("audioDeviceOutChans", "11"), 2);
    setup.useDefaultInputChannels = ! saved.hasAttribute ("audioDeviceInChans");
    setup.useDefaultOutputChannels = ! saved.hasAttribute ("audioDeviceOutChans");

    intendedType = saved.getStringAttribute ("deviceType", deviceManager.getCurrentAudioDeviceType());
    intendedSetup = setup;
    haveIntendedDevice = setup.outputDeviceName.isNotEmpty() || setup.inputDeviceName.isNotEmpty();
    deviceLost = false;
}

void MainComponent::saveDeviceState()
{
    if (auto xml = deviceManager.createStateXml())
    {
        deviceStateFile().getParentDirectory().createDirectory();
        xml->writeTo (deviceStateFile());
    }
}

juce::String MainComponent::intendedDeviceName() const
{
    return intendedSetup.outputDeviceName.isNotEmpty() ? intendedSetup.outputDeviceName : intendedSetup.inputDeviceName;
}

juce::String MainComponent::describeDevice() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
        return "none";

    return dev->getName() + " (" + dev->getTypeName() + "), " + juce::String (dev->getCurrentSampleRate(), 0)
           + " Hz, " + juce::String (dev->getCurrentBufferSizeSamples()) + " samples";
}

// Called by the UI timer. Notices that the device has gone (closed, no audio blocks for 3 seconds, or another
// device opened in its place) and reconnects to the intended one every 2 seconds until it is back. Paused while
// the Setup tab is open, so it never fights a change being made there.
void MainComponent::checkAudioDevice()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto blocks = engine.blocks.load();
    const bool advancing = blocks != lastBlockCount;
    lastBlockCount = blocks;

    if (engine.deviceError.exchange (false))
        events.add ("Audio driver reported an error");

    if (! haveIntendedDevice || currentTab() == tabSetup)
    {
        blocksStillSinceMs = 0;
        return;
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    const bool running = dev != nullptr && dev->isOpen() && dev->isPlaying();

    if (running && ! advancing)
    {
        if (blocksStillSinceMs == 0)
            blocksStillSinceMs = now;
    }
    else
    {
        blocksStillSinceMs = 0;
    }

    const bool stalled = blocksStillSinceMs != 0 && now - blocksStillSinceMs > 3000;

    const auto current = deviceManager.getAudioDeviceSetup();
    const bool wrongDevice = running && (deviceManager.getCurrentAudioDeviceType() != intendedType
                                         || current.outputDeviceName != intendedSetup.outputDeviceName
                                         || current.inputDeviceName != intendedSetup.inputDeviceName);

    if (! deviceLost)
    {
        juce::String reason;
        if (! running)          reason = "the device closed";
        else if (stalled)       reason = "no audio for 3 seconds";
        else if (wrongDevice)   reason = "another device opened in its place";

        if (reason.isEmpty())
            return;

        deviceLost = true;
        lastRetryMs = now - 2000;   // the first attempt is straight away
        lastRetryError = {};
        events.add ("Audio device lost (" + reason + "). Reconnecting to " + intendedDeviceName());

        if (running)
            deviceManager.closeAudioDevice();

        return;
    }

    if (running && advancing && ! wrongDevice)
    {
        deviceLost = false;
        events.add ("Audio device back: " + describeDevice());
        return;
    }

    if (now - lastRetryMs < 2000)
        return;

    lastRetryMs = now;

    // Look for it again (a USB device that was unplugged has to be found afresh), then open it as it was.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() == intendedType)
            type->scanForDevices();

    if (deviceManager.getCurrentAudioDeviceType() != intendedType)
        deviceManager.setCurrentAudioDeviceType (intendedType, true);

    const juce::String error = deviceManager.setAudioDeviceSetup (intendedSetup, true);

    if (error.isEmpty())
    {
        // Opened as asked. From now on, compare against the setup exactly as the driver reports it, so a
        // small difference in how names are reported can never cause a reconnection over and over.
        intendedType = deviceManager.getCurrentAudioDeviceType();
        intendedSetup = deviceManager.getAudioDeviceSetup();
    }
    else if (error != lastRetryError)   // each different failure is logged once
    {
        events.add ("Reconnecting failed: " + error);
    }

    lastRetryError = error;
}

void MainComponent::resized()
{
    if (selector == nullptr)
        return;

    auto area = getLocalBounds().reduced (10);
    statusStrip.setBounds (area.removeFromBottom (28));
    area.removeFromBottom (6);
    tabs.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    switch (currentTab())
    {
        case tabStage:
            stageView.setBounds (area);
            break;

        case tabPlay:   // Play
        {
            if (playMeter != nullptr)
            {
                playMeter->setBounds (area.removeFromBottom (playMeter->getRequiredHeight()));
                area.removeFromBottom (6);
            }
            playView.setBounds (area);
            break;
        }

        case tabEdit:   // Edit
        {
            auto row1 = area.removeFromTop (30);
            sceneLabel.setBounds (row1.removeFromLeft (110));
            newSceneButton.setBounds (row1.removeFromRight (120));
            row1.removeFromRight (6);
            deleteSceneButton.setBounds (row1.removeFromRight (120));
            row1.removeFromRight (6);
            sceneBox.setBounds (row1);
            area.removeFromTop (6);

            auto row2 = area.removeFromTop (30);
            scenePresetLabel.setBounds (row2.removeFromLeft (110));
            scenePresetClear.setBounds (row2.removeFromRight (24));
            row2.removeFromRight (4);
            scenePresetLearn.setBounds (row2.removeFromRight (96));
            row2.removeFromRight (6);
            saveScenePresetButton.setBounds (row2.removeFromRight (120));
            row2.removeFromRight (6);
            deleteScenePresetButton.setBounds (row2.removeFromRight (120));
            row2.removeFromRight (6);
            scenePresetBox.setBounds (row2);
            area.removeFromTop (6);

            auto row3 = area.removeFromTop (30);
            recallLabel.setBounds (row3.removeFromLeft (110));
            recallClear.setBounds (row3.removeFromRight (24));
            row3.removeFromRight (4);
            recallLearn.setBounds (row3.removeFromRight (160));
            area.removeFromTop (10);

            {
                auto tools = area.removeFromTop (30);
                addNodeButton.setBounds (tools.removeFromLeft (130));
                tools.removeFromLeft (6);
                connectButton.setBounds (tools.removeFromLeft (130));
                tools.removeFromLeft (6);
                resetLayoutButton.setBounds (tools.removeFromLeft (120));
                tools.removeFromLeft (12);
                editHint.setBounds (tools);
                area.removeFromTop (6);
            }

            // The picture takes about half of what is left; the selected item's settings take the rest.
            graphView.setBounds (area.removeFromTop (juce::jlimit (220, 520, area.getHeight() * 55 / 100)));
            area.removeFromTop (8);

            viewport.setBounds (area);

            if (nodesPanel != nullptr)
            {
                const int w = viewport.getMaximumVisibleWidth();
                nodesPanel->setSize (w, nodesPanel->layout (w, false));
            }
            break;
        }

        case tabSetlist:   // Setlist
        {
            setlistPanel.setBounds (area);
            break;
        }

        case tabSetup:   // Setup
        {
            midiBox.setBounds (area.removeFromRight (460));
            area.removeFromRight (12);

            auto fades = area.removeFromBottom (142);
            area.removeFromBottom (8);
            selector->setBounds (area);

            auto r1 = fades.removeFromTop (30);
            fades.removeFromTop (6);
            captureFadeLabel.setBounds (r1.removeFromLeft (190));
            captureFadeSlider.setBounds (r1);

            auto r2 = fades.removeFromTop (30);
            connectionFadeLabel.setBounds (r2.removeFromLeft (190));
            connectionFadeSlider.setBounds (r2);
            fades.removeFromTop (6);

            auto r3 = fades.removeFromTop (30);
            tunerLabel.setBounds (r3.removeFromLeft (190));
            tunerLearn.setBounds (r3.removeFromLeft (110));
            r3.removeFromLeft (4);
            tunerClear.setBounds (r3.removeFromLeft (24));
            fades.removeFromTop (6);

            presetMuteToggle.setBounds (fades.removeFromTop (30));
            break;
        }

        default:  // Diagnostics
        {
            auto row = [&area] (int h = 28) { auto r = area.removeFromTop (h); area.removeFromTop (6); return r; };

            auto top = row();
            resetButton.setBounds (top.removeFromLeft (160));
            top.removeFromLeft (12);
            loopback.setBounds (top.removeFromLeft (420));

            auto r1 = row();
            loopInLabel.setBounds (r1.removeFromLeft (130));
            loopInBox.setBounds (r1.removeFromLeft (200));
            r1.removeFromLeft (30);
            loopOutLabel.setBounds (r1.removeFromLeft (130));
            loopOutBox.setBounds (r1.removeFromLeft (200));

            area.removeFromTop (6);

            if (meter != nullptr)
            {
                meter->setBounds (area.removeFromTop (meter->getRequiredHeight()));
                area.removeFromTop (8);
            }

            statsBox.setBounds (area);
            break;
        }
    }
}

GraphEditor MainComponent::makeEditor()
{
    GraphEditor editor;

    editor.addNode = [this] (const std::string& type, const juce::String& name)
    {
        juce::String error;
        if (! sceneManager.addNode (type, name, error))
            showError (error);
    };

    editor.removeNode = [this] (const std::string& id)
    {
        juce::String error;
        if (! sceneManager.removeNode (id, error))
            showError (error);
    };

    editor.connect = [this] (const std::string& from, const std::string& to)
    {
        juce::String error;
        if (! sceneManager.connectNodes (from, to, error))
            showError (error);
    };

    editor.disconnect = [this] (const std::string& from, const std::string& to) { sceneManager.disconnectNodes (from, to); };

    editor.loadFile = [this] (const std::string& nodeId, const juce::File& file)
    {
        sceneManager.setNodeFile (nodeId, file);
        engine.resetStats();
        statsStart = juce::Time::getCurrentTime();
    };

    editor.structuralChanged = [this] { sceneManager.saveGraph(); };

    editor.browse = [this] (const std::string& nodeId, bool isIr)
    {
        auto* browser = new AssetBrowser (library, isIr);
        browser->onChosen = [this, nodeId] (const juce::File& file)
        {
            sceneManager.setNodeFile (nodeId, file);
            engine.resetStats();
            statsStart = juce::Time::getCurrentTime();
        };

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (browser);
        options.dialogTitle = isIr ? "Choose a cab IR" : "Choose a NAM capture";
        options.dialogBackgroundColour = juce::Colour (0xff1b1d20);
        options.componentToCentreAround = this;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    };

    editor.openPlugin = [this] (const std::string& nodeId) { openPluginWindow (nodeId); };

    editor.rename = [this] (const std::string& nodeId, const juce::String& name) { sceneManager.setNodeName (nodeId, name); };
    editor.setText = [this] (const std::string& nodeId, const juce::String& text) { sceneManager.setNodeText (nodeId, text); };
    editor.recolour = [this] (const std::string& nodeId, juce::uint32 argb) { sceneManager.setNodeColour (nodeId, argb); };
    return editor;
}

void MainComponent::openPluginWindow (const std::string& nodeId)
{
    const auto node = rig.graph().findNode (nodeId);
    auto* vst = dynamic_cast<VstNode*> (node.get());
    const auto plugin = vst != nullptr ? vst->instance() : nullptr;

    if (plugin == nullptr)
    {
        showError ("No plugin is loaded in this node yet.");
        return;
    }

    const auto existing = pluginWindows.find (nodeId);
    if (existing != pluginWindows.end())
    {
        existing->second->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    pluginWindows[nodeId] = std::make_unique<PluginWindow> (nodeId, plugin, node->displayName(), [safe, nodeId]
    {
        juce::MessageManager::callAsync ([safe, nodeId]
        {
            if (safe != nullptr)
                safe->pluginWindows.erase (nodeId);
        });
    });
}

// Closes windows of nodes that are gone, or whose plugin was replaced.
void MainComponent::prunePluginWindows()
{
    for (auto it = pluginWindows.begin(); it != pluginWindows.end();)
    {
        const auto node = rig.graph().findNode (it->first);
        auto* vst = dynamic_cast<VstNode*> (node.get());
        const auto plugin = vst != nullptr ? vst->instance() : nullptr;

        if (plugin == nullptr || plugin.get() != it->second->instance())
            it = pluginWindows.erase (it);
        else
            ++it;
    }
}

std::map<std::string, juce::Point<float>> MainComponent::savedPositions() const
{
    std::map<std::string, juce::Point<float>> positions;
    for (const auto& n : sceneManager.graphDef().nodes)
        if (n.posX >= 0.0f && n.posY >= 0.0f)
            positions[n.id] = { n.posX, n.posY };

    return positions;
}

void MainComponent::rebuildPanel()
{
    viewport.setViewedComponent (nullptr, false);
    nodesPanel.reset();

    nodesPanel = std::make_unique<NodesPanel> (rig, midiRouter, makeEditor(), graphView.getSelection());
    viewport.setViewedComponent (nodesPanel.get(), false);
}

void MainComponent::rebuildGraphViews()
{
    meter.reset();

    graphView.setGraph (rig.graph(), savedPositions());   // also drops a selection that no longer exists
    rebuildPanel();

    // One meter row for what matters on each kind of node. Each row keeps its node alive.
    meter = std::make_unique<LevelMeter>();

    for (const auto& node : rig.graph().nodes())
    {
        if (auto* in = dynamic_cast<InputNode*> (node.get()))
        {
            meter->addRow (node->displayName() + ": interface", &in->meterBeforeGain, nullptr, true, true, node);
            meter->addRow (node->displayName() + ": after gain", &in->meterAfterGain, nullptr, false, true, node);
        }
        else if (auto* amp = dynamic_cast<NamAmpNode*> (node.get()))
        {
            meter->addRow (node->displayName() + ": output", &amp->meterOut, nullptr, false, false, node);
        }
        else if (auto* ir = dynamic_cast<IrNode*> (node.get()))
        {
            meter->addRow (node->displayName() + ": output", &ir->meterOut, nullptr, false, false, node);
        }
        else if (auto* out = dynamic_cast<OutputNode*> (node.get()))
        {
            meter->addRow (node->displayName(), &out->meterAfterLimiter, &out->meterBeforeLimiter, false, false, node);
        }
    }

    addChildComponent (*meter);

    // The playing view only shows the outputs.
    playMeter = std::make_unique<LevelMeter>();
    for (const auto& node : rig.graph().nodes())
        if (auto* out = dynamic_cast<OutputNode*> (node.get()))
            playMeter->addRow (node->displayName(), &out->meterAfterLimiter, &out->meterBeforeLimiter, false, false, node);
    addChildComponent (*playMeter);

    playView.setNodes (rig.graph().nodes());
    stageView.setNodes (rig.graph().nodes());
    updateTabVisibility();
}

void MainComponent::refreshSceneLists()
{
    const int revision = library.revision.load();
    const juce::String scene = sceneManager.currentSceneId();
    const juce::String preset = rig.getCurrentScenePreset();

    if (revision != lastLibraryRevision || scene != lastShownScene)
    {
        sceneIds.clear();
        sceneBox.clear (juce::dontSendNotification);

        int item = 1;
        for (const auto& s : library.listScenes())
        {
            sceneBox.addItem (s.name + (s.pc >= 0 ? "  (PC " + juce::String (s.pc) + ")" : juce::String()), item++);
            sceneIds.add (s.id);
        }

        scenePresetIds.clear();
        scenePresetBox.clear (juce::dontSendNotification);
        scenePresetBox.setTextWhenNothingSelected ("(none saved)");

        item = 1;
        for (const auto& p : library.listScenePresets (scene))
        {
            scenePresetBox.addItem (p.name + "  (slot " + juce::String (p.slot) + ")", item++);
            scenePresetIds.add (p.id);
        }

        lastLibraryRevision = revision;
        lastShownScene = scene;
        lastShownPreset = "\n";   // forces the selection below to be set
    }

    const int sceneIndex = sceneIds.indexOf (scene);
    if (sceneBox.getSelectedItemIndex() != sceneIndex)
        sceneBox.setSelectedItemIndex (sceneIndex, juce::dontSendNotification);

    const int presetIndex = scenePresetIds.indexOf (preset);
    if (scenePresetBox.getSelectedItemIndex() != presetIndex)
        scenePresetBox.setSelectedItemIndex (presetIndex, juce::dontSendNotification);

    const bool learningThis = midiRouter.learningParam() == "scenepreset";
    const juce::String bound = midiRouter.sceneBindingText ("scenepreset");
    scenePresetLearn.setButtonText (learningThis ? juce::String ("Learning...") : bound.isNotEmpty() ? bound : juce::String ("MIDI"));
    scenePresetLearn.setColour (juce::TextButton::buttonColourId,
                                learningThis ? juce::Colour (0xffc07a10)
                                             : bound.isNotEmpty() ? juce::Colour (0xff2a6b5a) : juce::Colour (0xff3a3a3a));
    scenePresetClear.setVisible (currentTab() == tabEdit && bound.isNotEmpty());

    // The "recall this preset" binding belongs to whichever preset is selected above.
    {
        const int sel = scenePresetBox.getSelectedItemIndex();
        const std::string target = (sel >= 0 && sel < scenePresetIds.size())
                                       ? "scenepreset:" + scenePresetIds[sel].toStdString() : std::string();
        const bool learning = ! target.empty() && midiRouter.learningParam() == target;
        const juce::String text = target.empty() ? juce::String() : midiRouter.sceneBindingText (target);

        recallLearn.setEnabled (! target.empty());
        recallLearn.setButtonText (learning ? juce::String ("Learning...") : text.isNotEmpty() ? text : juce::String ("MIDI"));
        recallLearn.setColour (juce::TextButton::buttonColourId,
                               learning ? juce::Colour (0xffc07a10)
                                        : text.isNotEmpty() ? juce::Colour (0xff2a6b5a) : juce::Colour (0xff3a3a3a));
        recallClear.setVisible (currentTab() == tabEdit && text.isNotEmpty());
    }
}

void MainComponent::updateStatusStrip (const juce::StringArray& freshMidi)
{
    // Where you are.
    juce::String left = "Scene: " + sceneBox.getText().upToFirstOccurrenceOf ("  (PC", false, false);

    if (setlists.hasSetlist())
    {
        const int playing = setlists.playingIndex();
        left << "     " << setlists.setlist().name;
        if (playing >= 0)
            left << "  " << (playing + 1) << "/" << (int) setlists.setlist().entries.size();
    }

    // How the machine is coping.
    juce::String middle;
    auto level = StatusStrip::Level::Ok;

    if (deviceLost)
    {
        middle = "Audio device lost: reconnecting to " + intendedDeviceName() + "...";
        level = StatusStrip::Level::Problem;
    }
    else if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int bs = dev->getCurrentBufferSizeSamples();
        const double deadlineNs = 1.0e9 * bs / juce::jmax (1.0, sr);
        const auto blocks = engine.blocks.load();
        const double meanPct = blocks > 0 ? 100.0 * ((double) engine.sumNs.load() / (double) blocks) / deadlineNs : 0.0;
        const double peakPct = 100.0 * (double) engine.maxNs.load() / deadlineNs;
        const int xruns = dev->getXRunCount();

        middle = "CPU " + juce::String (meanPct, 0) + "% (peak " + juce::String (peakPct, 0) + "%)   "
                 + juce::String (sr / 1000.0, 1) + " kHz / " + juce::String (bs) + "   xruns " + juce::String (juce::jmax (0, xruns));

        if (engine.overruns.load() > 0 || xruns > 0)
            level = StatusStrip::Level::Problem;
        else if (meanPct > 60.0)
            level = StatusStrip::Level::Warning;
    }
    else
    {
        middle = "No audio device";
        level = StatusStrip::Level::Problem;
    }

    // The latest warning, else the latest MIDI message.
    for (int i = freshMidi.size() - 1; i >= 0; --i)
        if (freshMidi[i].startsWith ("Ch "))
        {
            lastMidiLine = freshMidi[i].upToFirstOccurrenceOf ("\n", false, false);
            lastMidiTime = juce::Time::getCurrentTime();
            break;
        }

    juce::String right;
    const juce::String warning = currentWarnings();

    if (warning.isNotEmpty())
    {
        right = warning;
        if (level == StatusStrip::Level::Ok)
            level = StatusStrip::Level::Warning;
    }
    else if (lastMidiLine.isNotEmpty() && (juce::Time::getCurrentTime() - lastMidiTime).inSeconds() < 5.0)
    {
        right = "MIDI: " + lastMidiLine;
    }

    statusStrip.set (left, middle, right, level);
}

// The Play tab's top row shows the current setlist's entries if there is one, and the scenes otherwise.
void MainComponent::updatePlaySetlist()
{
    const auto& list = setlists.setlist();
    const bool useList = setlists.hasSetlist() && ! list.entries.empty();

    if (useList)
    {
        const int revision = library.revision.load();

        if (revision != lastPlayRevision || list.id != lastPlaySetlist || (int) list.entries.size() != lastPlayEntries || ! playUsesSetlist)
        {
            lastPlayRevision = revision;
            lastPlaySetlist = list.id;
            lastPlayEntries = (int) list.entries.size();

            playNames.clear();
            playIds.clear();

            const auto scenes = library.listScenes();

            for (size_t i = 0; i < list.entries.size(); ++i)
            {
                const auto& e = list.entries[i];

                juce::String name = e.name;   // the entry's own name wins

                if (name.isEmpty())
                {
                    name = e.scene;
                    for (const auto& s : scenes)
                        if (s.id == e.scene)
                            name = s.name;

                    if (e.preset.isNotEmpty())
                    {
                        juce::String presetName = e.preset;
                        for (const auto& p : library.listScenePresets (e.scene))
                            if (p.id == e.preset)
                                presetName = p.name;

                        name << "\n" << presetName;
                    }
                }

                playNames.add (name);
                playIds.add (juce::String ((int) i));
            }
        }

        playUsesSetlist = true;
        playView.setScenes (playNames, playIds, juce::String (setlists.playingIndex()));
        return;
    }

    playUsesSetlist = false;
    lastPlayEntries = -1;

    juce::StringArray names;
    for (int i = 0; i < sceneIds.size(); ++i)
        names.add (sceneBox.getItemText (i).upToFirstOccurrenceOf ("  (PC", false, false));
    playView.setScenes (names, sceneIds, sceneManager.currentSceneId());
}

void MainComponent::applySlotCount (int n)
{
    slotCount = juce::jlimit (1, SlotsPanel::maxSlots, n);
    saveSettings();
    updateStage();
}

// A footswitch button. In a song, it recalls the scene preset the song assigns to it (nothing if the button is
// unassigned there). With no song playing, button n recalls the current scene's preset in slot number n - 1.
void MainComponent::pressSlot (int slot)
{
    const int index = slot - 1;
    if (index < 0)
        return;

    juce::String presetId;
    const int playing = setlists.playingIndex();

    if (playing >= 0)
    {
        const auto& e = setlists.setlist().entries[(size_t) playing];
        if (index < (int) e.slots.size())
            presetId = e.slots[(size_t) index];
    }
    else
    {
        for (const auto& p : library.listScenePresets (sceneManager.currentSceneId()))
            if (p.slot == index)
                presetId = p.id;
    }

    if (presetId.isEmpty())
        return;

    Command c;
    c.type = Command::Type::LoadScenePreset;
    c.text = presetId;
    rig.submit (std::move (c));
}

// Fills the Stage tab: the previous, current and next song, and what each footswitch button does.
void MainComponent::updateStage()
{
    StageView::Song song;
    std::vector<StageView::Button> buttons ((size_t) slotCount);

    const auto presets = library.listScenePresets (sceneManager.currentSceneId());
    const juce::String playingPreset = rig.getCurrentScenePreset();

    auto presetName = [&] (const juce::String& scene, const juce::String& id)
    {
        for (const auto& p : library.listScenePresets (scene))
            if (p.id == id)
                return p.name;
        return id;
    };

    auto entryName = [&] (const SetlistEntry& e)
    {
        if (e.name.isNotEmpty())
            return e.name;
        for (const auto& s : library.listScenes())
            if (s.id == e.scene)
                return s.name;
        return e.scene;
    };

    const int playing = setlists.playingIndex();
    juce::String sceneName = sceneManager.currentSceneId();
    for (const auto& s : library.listScenes())
        if (s.id == sceneManager.currentSceneId())
            sceneName = s.name;

    if (playing >= 0)
    {
        const auto& entries = setlists.setlist().entries;
        song.inSetlist = true;
        song.current = entryName (entries[(size_t) playing]);
        song.previous = playing > 0 ? entryName (entries[(size_t) playing - 1]) : juce::String();
        song.next = playing + 1 < (int) entries.size() ? entryName (entries[(size_t) playing + 1]) : juce::String();
        song.detail = "Scene: " + sceneName + "     " + juce::String (playing + 1) + " of " + juce::String ((int) entries.size());

        const auto& e = entries[(size_t) playing];
        for (int k = 0; k < slotCount; ++k)
            if (k < (int) e.slots.size() && e.slots[(size_t) k].isNotEmpty())
            {
                buttons[(size_t) k].preset = presetName (e.scene, e.slots[(size_t) k]);
                buttons[(size_t) k].active = e.slots[(size_t) k] == playingPreset;
            }
    }
    else
    {
        // No song: the scene on its own, with its scene presets on the buttons by their slot numbers.
        song.current = sceneName;
        song.detail = setlists.hasSetlist() ? "Not on a setlist entry" : "No setlist";

        for (const auto& p : presets)
            if (p.slot >= 0 && p.slot < slotCount)
            {
                buttons[(size_t) p.slot].preset = p.name;
                buttons[(size_t) p.slot].active = p.id == playingPreset;
            }
    }

    stageView.setState (song, buttons);
}

// Global settings that belong to no scene: the switching times, and which setlist is current.
void MainComponent::loadSettings()
{
    const juce::File file = logFolder().getChildFile ("settings.json");
    const juce::var v = juce::JSON::parse (file);

    if (v.isObject())
    {
        auto apply = [this, &v] (const char* key, const char* paramId, juce::Slider& slider)
        {
            if (v.hasProperty (key))
            {
                const double ms = juce::jlimit (0.0, 250.0, (double) v.getProperty (key, juce::var (10.0)));
                if (auto* p = rig.params().find (paramId))
                    p->set ((float) ms);
                slider.setValue (ms, juce::dontSendNotification);
            }
        };

        apply ("captureFadeMs", "rig/captureFadeMs", captureFadeSlider);
        apply ("connectionFadeMs", "rig/connectionFadeMs", connectionFadeSlider);

        const bool mute = (bool) v.getProperty ("presetMute", juce::var (false));
        if (auto* p = rig.params().find ("rig/presetMute"))
            p->set (mute ? 1.0f : 0.0f);
        presetMuteToggle.setToggleState (mute, juce::dontSendNotification);
        setlists.load (v.getProperty ("setlist", juce::var()).toString());
        slotCount = juce::jlimit (1, SlotsPanel::maxSlots, (int) v.getProperty ("slots", juce::var (8)));
        savedEntry = (int) v.getProperty ("entry", juce::var (-1));
        savedEntrySetlist = v.getProperty ("entrySetlist", juce::var()).toString();
    }

    setlistPanel.setSlotCount (slotCount);
}

// The setlist entry that was playing when the app closed, if settings.json names one and it still exists.
MainComponent::StartupEntry MainComponent::readStartupEntry() const
{
    StartupEntry e;
    const juce::var v = juce::JSON::parse (logFolder().getChildFile ("settings.json"));
    if (! v.isObject())
        return e;

    const juce::String setlistId = v.getProperty ("setlist", juce::var()).toString();
    const int index = (int) v.getProperty ("entry", juce::var (-1));

    if (setlistId.isEmpty() || index < 0 || v.getProperty ("entrySetlist", juce::var()).toString() != setlistId)
        return e;

    Setlist s;
    if (! library.readSetlist (setlistId, s) || index >= (int) s.entries.size())
        return e;

    e.scene = s.entries[(size_t) index].scene;
    e.preset = s.entries[(size_t) index].preset;
    e.index = index;
    return e;
}

void MainComponent::saveSettings()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("captureFadeMs", captureFadeSlider.getValue());
    o->setProperty ("connectionFadeMs", connectionFadeSlider.getValue());
    o->setProperty ("presetMute", presetMuteToggle.getToggleState());
    o->setProperty ("setlist", setlists.hasSetlist() ? setlists.setlist().id : juce::String());
    o->setProperty ("slots", slotCount);
    o->setProperty ("entry", savedEntry);                 // the song to come back to at startup
    o->setProperty ("entrySetlist", savedEntrySetlist);   // and the setlist it is in

    logFolder().createDirectory();
    logFolder().getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

void MainComponent::appendLogLine (const juce::String& line)
{
    logFile.appendText (line + "\n");
}

namespace
{
    // The audio device selector builds its own sliders and menus (and rebuilds them when the device
    // changes), so the mouse wheel is switched off on whatever it currently holds.
    void lockWheel (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* cb = dynamic_cast<juce::ComboBox*> (child))
                cb->setScrollWheelEnabled (false);
            else if (auto* sl = dynamic_cast<juce::Slider*> (child))
                sl->setScrollWheelEnabled (false);

            lockWheel (*child);
        }
    }
}

juce::String MainComponent::currentWarnings()
{
    juce::String text = rig.getStatusMessage();

    for (const auto& node : rig.graph().nodes())
        if (auto* amp = dynamic_cast<NamAmpNode*> (node.get()))
            if (node->activeParam()->on() && ! amp->hasModel())
                text << (text.isEmpty() ? "" : "  ") << node->displayName()
                     << ": no capture loaded, so the signal passes through it unprocessed";

    return text;
}

// ===== Tuner =============================================================================================

void MainComponent::setTuner (bool on)
{
    if (on == engine.tunerOn.load())
        return;

    if (on)
    {
        // Listen to the guitar: the lowest-numbered interface channel that an input node of this scene uses.
        int channel = -1;
        for (const auto& node : rig.graph().nodes())
            if (node->isInput())
            {
                const int c = node->deviceInputChannel();
                if (c >= 0 && (channel < 0 || c < channel))
                    channel = c;
            }

        engine.tunerChannel = juce::jmax (0, channel);

        tunerWindow.assign (engine.currentSampleRate() > 50000.0 ? 8192 : 4096, 0.0f);
        tunerFilled = 0;
        tunerQuiet = 0;
        tunerHz = 0.0;

        float old[1024];
        while (engine.readTunerSamples (old, 1024) > 0) {}   // anything left from last time

        engine.tunerOn = true;
        tabs.setCurrentTabIndex (tabStage);   // the tuner is shown on the Stage tab
        stageView.setTuner (true);
        tunerTimer.startTimerHz (15);
    }
    else
    {
        engine.tunerOn = false;
        tunerTimer.stopTimer();
        stageView.setTuner (false);
    }

    events.add (on ? "Tuner on (outputs muted)" : "Tuner off");
}

// Fifteen times a second while the tuner is on: take the new input, and find the pitch of the last 85 ms.
void MainComponent::tunerTick()
{
    const int size = (int) tunerWindow.size();
    float fresh[2048];

    for (int n; (n = engine.readTunerSamples (fresh, 2048)) > 0;)
    {
        std::copy (tunerWindow.begin() + n, tunerWindow.end(), tunerWindow.begin());   // n is never more than 2048
        std::copy (fresh, fresh + n, tunerWindow.end() - n);
        tunerFilled = juce::jmin (size, tunerFilled + n);
    }

    if (tunerFilled < size)
        return;

    const auto result = pitch::detect (tunerWindow.data(), size, engine.currentSampleRate());

    if (result.found)
    {
        // Steadied a little, unless the note has changed.
        const bool sameNote = tunerHz > 0.0 && std::abs (1200.0 * std::log2 (result.hz / tunerHz)) < 40.0;
        tunerHz = sameNote ? 0.6 * tunerHz + 0.4 * result.hz : result.hz;
        tunerQuiet = 0;

        const auto note = pitch::nearestNote (tunerHz);
        stageView.setTuner (true, juce::String (note.name) + juce::String (note.octave), (float) note.cents);
    }
    else if (++tunerQuiet > 6)   // nothing for about half a second
    {
        tunerHz = 0.0;
        stageView.setTuner (true);
    }
}

// Writes what changed since the last tick to the event log: the device, the scene, scene preset and song,
// warnings, late audio blocks and driver dropouts. Called by the UI timer, four times a second.
void MainComponent::logChanges()
{
    if (! deviceLost)
    {
        const juce::String device = describeDevice();
        if (device != loggedDevice)
        {
            loggedDevice = device;
            events.add ("Audio device: " + device);
        }
    }

    const juce::String scene = sceneManager.currentSceneId();
    if (scene != loggedScene)
    {
        loggedScene = scene;
        juce::String name = scene;
        for (const auto& s : library.listScenes())
            if (s.id == scene)
                name = s.name;
        events.add ("Scene: " + name);
    }

    const juce::String preset = rig.getCurrentScenePreset();
    if (preset != loggedPreset)
    {
        loggedPreset = preset;
        if (preset.isNotEmpty())
            events.add ("Scene preset: " + preset);
    }

    const int playing = setlists.playingIndex();
    if (playing != loggedEntry)
    {
        loggedEntry = playing;

        if (playing >= 0)
        {
            const auto& list = setlists.setlist();
            const auto& entry = list.entries[(size_t) playing];
            events.add ("Song " + juce::String (playing + 1) + " of " + juce::String ((int) list.entries.size()) + ": "
                        + (entry.name.isNotEmpty() ? entry.name : entry.scene));

            // Remembered, so the next start comes back to this song.
            savedEntry = playing;
            savedEntrySetlist = list.id;
            saveSettings();
        }
    }

    const juce::String status = currentWarnings();
    if (status != loggedStatus)
    {
        loggedStatus = status;
        events.add (status.isEmpty() ? juce::String ("Warnings cleared") : "Warning: " + status);
    }

    // Late audio blocks (the app's own count) and dropouts (the driver's). Both counts restart at zero when
    // the statistics are reset or the device restarts.
    auto* dev = deviceManager.getCurrentAudioDevice();
    const auto overruns = engine.overruns.load();
    if (overruns > loggedOverruns && dev != nullptr)
    {
        const double deadlineNs = 1.0e9 * dev->getCurrentBufferSizeSamples() / juce::jmax (1.0, dev->getCurrentSampleRate());
        events.add ("Late audio blocks: " + juce::String ((juce::int64) (overruns - loggedOverruns))
                    + " (worst block " + juce::String (100.0 * (double) engine.maxNs.load() / deadlineNs, 0) + "% of the deadline)");
    }
    loggedOverruns = overruns;

    const int xruns = dev != nullptr ? juce::jmax (0, dev->getXRunCount()) : 0;
    if (xruns > loggedXruns)
        events.add ("Driver dropouts: " + juce::String (xruns - loggedXruns) + " (total " + juce::String (xruns) + ")");
    loggedXruns = xruns;

    events.flush();
}

void MainComponent::timerCallback()
{
    checkAudioDevice();

    if (selector != nullptr)
        lockWheel (*selector);

    refreshSceneLists();
    prunePluginWindows();

    updatePlaySetlist();
    playView.refresh();
    stageView.refresh();
    updateStage();
    graphView.refresh();
    setlistPanel.refresh();

    // The graph changed (or a different scene was selected): rebuild the controls and meters, and
    // give anything new its default MIDI address.
    {
        const int graphRevision = rig.graph().revision();
        const juce::String scene = sceneManager.currentSceneId();

        if (graphRevision != lastGraphRevision || scene != lastDefaultsScene)
        {
            const bool graphChanged = graphRevision != lastGraphRevision;
            lastGraphRevision = graphRevision;
            lastDefaultsScene = scene;

            if (graphChanged)
                rebuildGraphViews();

            if (scene.isNotEmpty())
                midiRouter.ensureDefaults (sceneManager.printableMapFile());
        }
    }

    const auto fresh = midiRouter.takeLog();
    updateStatusStrip (fresh);
    logChanges();

    // The tuner's MIDI button on the Setup tab.
    {
        const bool learning = midiRouter.learningParam() == "tuner";
        const juce::String bound = midiRouter.sceneBindingText ("tuner");
        tunerLearn.setButtonText (learning ? juce::String ("Learning...") : bound.isNotEmpty() ? bound : juce::String ("MIDI"));
        tunerLearn.setColour (juce::TextButton::buttonColourId,
                              learning ? juce::Colour (0xffc07a10) : bound.isNotEmpty() ? juce::Colour (0xff2a6b5a) : juce::Colour (0xff3a3a3a));
        tunerClear.setVisible (currentTab() == tabSetup && bound.isNotEmpty());
    }

    if (fresh.size() > 0)
    {
        midiLines.addArray (fresh);
        while (midiLines.size() > 18)
            midiLines.remove (0);

        midiBox.setText ("MIDI monitor\n" + midiLines.joinIntoString ("\n"), false);
    }

    if (buildError.isNotEmpty())
    {
        statsBox.setText (buildError, false);
        return;
    }

    auto* dev = deviceManager.getCurrentAudioDevice();

    if (dev == nullptr)
    {
        statsBox.setText ("No audio device is open. Choose one on the left (ASIO if available).", false);
        return;
    }

    const double sr = dev->getCurrentSampleRate();
    const int bs = dev->getCurrentBufferSizeSamples();
    const double bufferMs = 1000.0 * bs / sr;
    const double deadlineNs = 1.0e9 * bs / sr;

    const auto blocks = engine.blocks.load();
    const double meanPct = blocks > 0 ? 100.0 * ((double) engine.sumNs.load() / (double) blocks) / deadlineNs : 0.0;
    const double peakPct = 100.0 * (double) engine.maxNs.load() / deadlineNs;
    const auto overruns = engine.overruns.load();
    const int xruns = dev->getXRunCount();

    const int inLat = dev->getInputLatencyInSamples();
    const int outLat = dev->getOutputLatencyInSamples();
    const double reportedMs = 1000.0 * (inLat + outLat) / sr;

    const int loops = engine.loopCount.load();
    const double loopAvgMs = loops > 0 ? 1000.0 * ((double) engine.loopSum.load() / loops) / sr : 0.0;

    const juce::String status = currentWarnings();

    juce::String s;
    s << "Device       : " << dev->getName() << "  (" << dev->getTypeName() << ")\n";
    s << "Format       : " << juce::String (sr, 0) << " Hz, buffer " << bs << " samples (" << juce::String (bufferMs, 2) << " ms)\n";
    s << "Scene        : " << sceneManager.currentSceneId() << "\n";
    if (status.isNotEmpty())
        s << "WARNING      : " << status << "\n";

    for (const auto& node : rig.graph().nodes())
    {
        if (auto* amp = dynamic_cast<NamAmpNode*> (node.get()))
        {
            const auto info = amp->getInfo();
            const std::string id = node->id();

            s << "\n" << node->displayName() << "\n";
            s << "  Model      : " << (info.fileName.isEmpty() ? juce::String ("(none)") : info.fileName);
            if (info.sampleRate > 0.0) s << "  (trained at " << juce::String (info.sampleRate, 0) << " Hz)";
            s << "\n";

            if (info.sampleRate > 0.0 && std::abs (info.sampleRate - sr) > 1.0)
                s << "  WARNING    : model sample rate differs from the device. Set the device to " << juce::String (info.sampleRate, 0) << " Hz.\n";

            if (info.gearType.isNotEmpty())
            {
                s << "  Type       : " << info.gearType;
                if (info.toneType.isNotEmpty())
                    s << ", tagged " << info.toneType << " (tags can be wrong)";
                s << "\n";

                if (info.gearType.containsIgnoreCase ("cab"))
                    s << "  NOTE       : this model includes a cab. Switch any Cab IR on its path off.\n";
            }

            const auto* matchParam = rig.params().find (id + "/match");
            const auto* targetParam = rig.params().find (id + "/target");

            if (matchParam != nullptr && ! matchParam->on())
                s << "  Loudness   : matching off\n";
            else if (info.fileName.isNotEmpty() && ! std::isfinite (info.loudness))
                s << "  Loudness   : no loudness data in the file, so no trim is applied\n";
            else if (info.fileName.isNotEmpty() && targetParam != nullptr)
                s << "  Loudness   : target " << juce::String (targetParam->get(), 1) << " dB, trim "
                  << juce::String (amp->getAppliedTrimDb(), 1) << " dB (the model reports " << juce::String (info.loudness, 1) << " dB)\n";

            if (info.fileName.isNotEmpty())
            {
                const double totalDb = amp->getAppliedInputDb();
                const double calDb = amp->getCalibrationDb();

                s << "  Input      : " << juce::String (totalDb, 1) << " dB into the model";
                if (std::isfinite (info.inputLevelDbu))
                    s << " (calibration " << juce::String (calDb, 1) << " dB from the model's " << juce::String (info.inputLevelDbu, 1)
                      << " dBu reference, manual " << juce::String (totalDb - calDb, 1) << " dB)";
                else
                    s << " (no input level in the file, so no calibration)";
                s << "\n";
            }

            if (info.slimmable)
            {
                const auto* sizeParam = rig.params().find (id + "/size");
                const double v = sizeParam != nullptr ? (double) sizeParam->get() : 1.0;
                const auto& bp = info.breakpoints;

                size_t idx = bp.empty() ? 0 : bp.size() - 1;
                for (size_t i = 0; i < bp.size(); ++i)
                    if (v < bp[i]) { idx = i; break; }

                s << "  Size       : " << juce::String (v, 2) << " -> submodel " << (int) (idx + 1) << " of " << (int) bp.size() << "\n";
            }
        }
        else if (auto* ir = dynamic_cast<IrNode*> (node.get()))
        {
            const juce::String name = ir->getFileName();
            s << "\n" << node->displayName() << ": " << (name.isEmpty() ? juce::String ("(none)") : name) << "\n";
        }
    }

    s << "\n";

    s << "Callback CPU : mean " << juce::String (meanPct, 1) << "%  peak " << juce::String (peakPct, 1)
      << "%  (of the " << juce::String (bufferMs, 2) << " ms buffer deadline)\n";
    s << "Overruns     : " << (juce::int64) overruns << "   Driver xruns: " << (xruns < 0 ? juce::String ("n/a") : juce::String (xruns)) << "\n";
    // Where the time goes: each node's share of the buffer deadline, the costliest first.
    {
        struct Line { juce::String name; double mean, peak; };
        std::vector<Line> lines;
        const double deadlineSeconds = (double) bs / sr;

        for (const auto& node : rig.graph().nodes())
        {
            const auto t = node->timing();
            lines.push_back ({ node->displayName(), 100.0 * t.meanSeconds / deadlineSeconds, 100.0 * t.maxSeconds / deadlineSeconds });
        }

        std::sort (lines.begin(), lines.end(), [] (const Line& a, const Line& b) { return a.mean > b.mean; });

        s << "Per node     : mean / peak, as % of the deadline, since Reset stats (a node that is off costs almost nothing)\n";
        for (const auto& l : lines)
            s << "  " << l.name.paddedRight (' ', 28).substring (0, 28) << " " << juce::String (l.mean, 1).paddedLeft (' ', 5)
              << " / " << juce::String (l.peak, 1).paddedLeft (' ', 5) << "\n";
    }

    s << "Reported lat : in " << inLat << " + out " << outLat << " samples = "
      << juce::String (reportedMs, 2) << " ms (driver-reported, not measured)\n";

    if (loops > 0)
        s << "Loopback     : last " << juce::String (1000.0 * engine.loopLast.load() / sr, 2) << " ms,  min "
          << juce::String (1000.0 * engine.loopMin.load() / sr, 2) << ",  avg " << juce::String (loopAvgMs, 2)
          << ",  max " << juce::String (1000.0 * engine.loopMax.load() / sr, 2) << " ms  (" << loops << " clicks)\n";
    else
        s << "Loopback     : " << (engine.loopbackMode.load() ? "waiting for a click on the input..." : "off") << "\n";

    s << "Elapsed      : " << (juce::Time::getCurrentTime() - statsStart).getDescription() << "\n\n";

    const bool cpuOk = meanPct < 60.0;
    const bool dropsOk = overruns == 0 && xruns <= 0;
    const bool latOk = loops > 0 && loopAvgMs <= 10.0;
    s << "Proposed targets:\n";
    s << "  mean CPU < 60%      : " << (cpuOk ? "OK" : "FAIL") << "\n";
    s << "  no overruns/xruns   : " << (dropsOk ? "OK" : "FAIL") << "\n";
    s << "  round trip <= 10 ms : " << (loops == 0 ? "not measured yet" : (latOk ? "OK" : "FAIL")) << "\n";

    statsBox.setText (s, false);

    if (++tickCount % 40 == 0)   // every 10 seconds
        appendLogLine (juce::Time::getCurrentTime().formatted ("%H:%M:%S") + ","
                       + juce::String (sr, 0) + "," + juce::String (bs) + ","
                       + juce::String (meanPct, 2) + "," + juce::String (peakPct, 2) + ","
                       + juce::String ((juce::int64) overruns) + "," + juce::String (xruns) + ","
                       + juce::String (loopAvgMs, 3));
}
