#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Rig.h"
#include "Dialogs.h"
#include "GraphView.h"
#include "MidiRouter.h"
#include "Nodes/MidiOutNode.h"
#include "Nodes/NodeFactory.h"

// What the panel can ask the app to do to the scene's graph.
struct GraphEditor
{
    std::function<void (const std::string& type, const juce::String& name)> addNode;
    std::function<void (const std::string& nodeId)> removeNode;
    std::function<void (const std::string& from, const std::string& to)> connect, disconnect;
    std::function<void (const std::string& nodeId, const juce::File& file)> loadFile;
    std::function<void (const std::string& nodeId)> openPlugin;           // shows a VST3 node's own window
    std::function<void (const std::string& nodeId, bool isIr)> browse;      // opens the library browser for a node's file
    std::function<void (const std::string& nodeId, const juce::String& name)> rename;
    std::function<void (const std::string& nodeId, juce::uint32 argb)> recolour;   // 0 = the node type's colour
    std::function<void (const std::string& nodeId, const juce::String& text)> setText;   // a node's text setting (a MIDI output's port)
    std::function<void()> structuralChanged;   // a graph setting, such as an interface channel, was changed
};

// Builds a control for every visible setting in the current graph, grouped by node in signal order,
// followed by the connections and a bar for adding nodes and connections. It is built for one version of
// the graph, and the window builds a fresh one whenever the graph changes.
//
// Every change is sent as a command, and the controls follow the registry values (so changes made from
// elsewhere, such as MIDI, show up too).
//
// Each node has a preset row and, for amps and IRs, a file row. Each row has a MIDI button: click it, then
// press a switch or move a pedal, and that message is bound to the setting in the current scene. Hold
// Shift while clicking a switch's button to bind on/off by value (64 or more = on) instead of toggling.
class NodesPanel : public juce::Component,
                   private juce::Timer
{
public:
    // Shows the settings of what is selected in the graph view: one node (with the connections that
    // touch it), or one connection. With nothing selected it shows a hint.
    NodesPanel (Rig& rigToControl, MidiRouter& routerToUse, GraphEditor editorToUse, GraphSelection selected = {})
        : rig (rigToControl), router (routerToUse), editor (std::move (editorToUse))
    {
        const auto nodes = rig.graph().nodes();
        const auto edges = rig.graph().edges();

        if (! selected.isNode() && ! selected.isEdge())
            addHeader ("Select a node or a connection above to see its settings.");

        bool anyRigSetting = false;
        for (auto* p : rig.params().all())
        {
            if (p->nodeId != "rig" || p->hidden || selected.isNode() || selected.isEdge())
                continue;

            if (! anyRigSetting)
            {
                addHeader ("Rig settings");
                anyRigSetting = true;
            }
            addParamRow (p, p->name);
        }

        for (const auto& node : nodes)
        {
            if (! selected.isNode() || node->id() != selected.node)
                continue;

            addNodeHeader (*node);

            const std::string activeId = node->id() + "/active";
            std::vector<Param*> settings;
            for (auto* p : rig.params().all())
                if (p->nodeId == node->id() && p->id != activeId && ! p->hidden)
                    settings.push_back (p);   // (custom-drawn settings are listed too: they make the node presettable)

            bool presetable = false;
            for (auto* p : settings)
                if (! p->structural)
                    presetable = true;

            if (presetable)
                addPresetRow (*node);

            if (node->hasFile())
                addFileRow (*node);

            if (node->hasText())
                addTextRow (*node);

            for (auto* p : settings)
                if (! p->customUi)
                    addParamRow (p, p->name);

            if (auto* mo = dynamic_cast<MidiOutNode*> (node.get()))
                addMidiRows (*mo);
        }

        bool anyEdge = false;
        for (const auto& e : edges)
        {
            const bool shown = selected.isEdge() ? (e.from == selected.from && e.to == selected.to)
                                                 : (selected.isNode() && (e.from == selected.node || e.to == selected.node));
            if (! shown)
                continue;

            if (! anyEdge)
            {
                addHeader (selected.isEdge() ? "Connection" : "Connections");
                anyEdge = true;
            }

            addParamRow (e.enabled, e.enabled->name, e.from, e.to);
            addParamRow (e.level, "level");
        }

        startTimerHz (10);
    }

    // Lays the rows out for `width`. With apply == false it only measures.
    int layout (int width, bool apply)
    {
        constexpr int learnW = 96, clearW = 24, saveW = 70, removeW = 70, gap = 4, margin = 6;
        const int clearX = width - margin - clearW;
        const int learnX = clearX - gap - learnW;

        int y = 4;

        for (auto& r : rows)
        {
            if (r.kind == Kind::NodeHeader || r.kind == Kind::Header)
                y += 8;

            const int h = (r.kind == Kind::NodeHeader || r.kind == Kind::Header) ? 28 : 26;

            if (apply)
            {
                switch (r.kind)
                {
                    case Kind::Header:
                        r.label->setBounds (margin, y, width - 2 * margin, h);
                        break;

                    case Kind::NodeHeader:
                    {
                        const int buttonsW = 3 * removeW + 3 * gap;
                        r.toggle->setBounds (margin, y, juce::jmax (60, learnX - gap - buttonsW - margin), h);
                        r.extra2->setBounds (learnX - buttonsW, y + 1, removeW, h - 2);                          // Name...
                        r.extra3->setBounds (learnX - buttonsW + removeW + gap, y + 1, removeW, h - 2);          // Colour...
                        r.extra->setBounds (learnX - gap - removeW, y + 1, removeW, h - 2);                      // Remove
                        break;
                    }

                    case Kind::Preset:
                    {
                        const int saveX = learnX - gap - saveW;
                        const int deleteX = saveX - gap - saveW;
                        r.label->setBounds (20, y, 110, h);
                        r.presetBox->setBounds (134, y + 1, juce::jmax (60, deleteX - gap - 134), h - 2);
                        r.extra->setBounds (deleteX, y + 1, saveW, h - 2);
                        r.saveButton->setBounds (saveX, y + 1, saveW, h - 2);
                        break;
                    }

                    case Kind::File:
                    {
                        const int loadX = learnX - gap - saveW;
                        r.label->setBounds (20, y, 110, h);
                        const int fileRight = r.extra2 != nullptr ? loadX - gap - saveW : loadX;
                        r.fileText->setBounds (134, y, juce::jmax (60, fileRight - gap - 134), h);
                        r.extra->setBounds (loadX, y + 1, saveW, h - 2);
                        if (r.extra2 != nullptr)
                            r.extra2->setBounds (loadX - gap - saveW, y + 1, saveW, h - 2);
                        break;
                    }

                    case Kind::Text:
                        r.label->setBounds (20, y, 110, h);
                        r.presetBox->setBounds (134, y + 1, juce::jmax (60, width - margin - 134), h - 2);
                        break;

                    case Kind::MidiMsg:
                    {
                        r.label->setBounds (20, y, 40, h);
                        r.presetBox->setBounds (64, y + 1, 160, h - 2);
                        r.slider->setBounds (232, y + 1, 110, h - 2);
                        r.slider2->setBounds (350, y + 1, 110, h - 2);
                        r.extra->setBounds (468, y + 1, removeW, h - 2);
                        break;
                    }

                    case Kind::MidiAdd:
                        r.extra->setBounds (134, y + 1, 130, h - 2);
                        r.extra2->setBounds (268, y + 1, 100, h - 2);
                        r.fileText->setBounds (376, y, juce::jmax (60, width - margin - 376), h);
                        break;

                    case Kind::Param:
                        if (r.toggle != nullptr)
                        {
                            const int toggleW = r.extra != nullptr ? learnX - gap - 24 - gap - margin : learnX - gap - margin;
                            r.toggle->setBounds (margin, y, toggleW, h);
                            if (r.extra != nullptr)
                                r.extra->setBounds (learnX - gap - 24, y + 1, 24, h - 2);
                        }
                        else
                        {
                            r.label->setBounds (20, y, 110, h);
                            r.slider->setBounds (134, y, juce::jmax (60, learnX - gap - 134), h);
                        }
                        break;

                }

                if (r.learn != nullptr)
                {
                    r.learn->setBounds (learnX, y + 1, learnW, h - 2);
                    r.clear->setBounds (clearX, y + 1, clearW, h - 2);
                }
            }

            y += h;
        }

        return y + 8;
    }

    void resized() override { layout (getWidth(), true); }

private:
    enum class Kind { Header, NodeHeader, Preset, File, Param, Text, MidiMsg, MidiAdd };

    struct Row
    {
        Kind kind = Kind::Param;
        Param* param = nullptr;        // the setting a parameter row controls
        std::string target;            // what MIDI learn binds: a setting's ID, or "preset:<node id>"
        std::string nodeId, nodeType;  // node, preset and file rows

        std::shared_ptr<juce::StringArray> presetIds = std::make_shared<juce::StringArray>();

        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::Slider> slider, slider2;
        int index = 0;                 // MIDI output rows: which message (from 0), or how many messages were drawn
        std::unique_ptr<juce::Label> label, fileText;
        std::unique_ptr<juce::ComboBox> presetBox;
        std::unique_ptr<juce::TextButton> saveButton, learn, clear, extra, extra2, extra3;
    };

    void send (const Param* p, float value)
    {
        Command c;
        c.type = Command::Type::SetParam;
        c.target = p->id;
        c.value = value;
        rig.submit (std::move (c));

        if (p->structural)
            structuralDirty = true;
    }

    void addHeader (const juce::String& text)
    {
        Row r;
        r.kind = Kind::Header;
        r.label = std::make_unique<juce::Label>();
        r.label->setText (text, juce::dontSendNotification);
        r.label->setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        addAndMakeVisible (*r.label);
        rows.push_back (std::move (r));
    }

    void addLearnButtons (Row& r, bool toggleByDefault)
    {
        const std::string target = r.target;

        r.learn = std::make_unique<juce::TextButton> ("MIDI");
        r.learn->onClick = [this, target, toggleByDefault]
        {
            if (router.learningParam() == target)
            {
                router.cancelLearn();
                return;
            }

            const bool shift = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
            router.startLearn (target, toggleByDefault && ! shift);
        };

        r.clear = std::make_unique<juce::TextButton> ("x");
        r.clear->onClick = [this, target] { router.clearSceneBinding (target); };

        addAndMakeVisible (*r.learn);
        addChildComponent (*r.clear);
    }

    void addNodeHeader (const Node& node)
    {
        Row r;
        r.kind = Kind::NodeHeader;
        r.nodeId = node.id();
        r.param = node.activeParam();
        r.target = r.param->id;

        r.toggle = std::make_unique<juce::ToggleButton> (node.displayName());
        r.toggle->setToggleState (r.param->on(), juce::dontSendNotification);
        r.toggle->setColour (juce::ToggleButton::textColourId, juce::Colours::white);

        auto* t = r.toggle.get();
        auto* p = r.param;
        t->onClick = [this, p, t] { send (p, t->getToggleState() ? 1.0f : 0.0f); };

        const std::string nodeId = node.id();
        const juce::String nodeName = node.displayName();
        r.extra = std::make_unique<juce::TextButton> ("Remove");
        r.extra->onClick = [this, nodeId, nodeName]
        {
            askConfirm ("Remove " + nodeName, "Remove " + nodeName + " from this scene? Its connections go with it. "
                        "Scene presets that mention it keep working and simply ignore it.",
                        [this, nodeId] { if (editor.removeNode) editor.removeNode (nodeId); });
        };

        r.extra2 = std::make_unique<juce::TextButton> ("Name...");
        r.extra2->onClick = [this, nodeId, nodeName]
        {
            askName ("Name this node", "The label shown on the node's tile and in the graph.", nodeName,
                     [this, nodeId] (const juce::String& name) { if (editor.rename) editor.rename (nodeId, name); });
        };

        r.extra3 = std::make_unique<juce::TextButton> ("Colour...");
        auto* colourButton = r.extra3.get();
        r.extra3->onClick = [this, nodeId, colourButton]
        {
            struct Swatch { const char* name; juce::uint32 argb; };
            static const Swatch swatches[] =
            {
                { "Red", 0xffd9534f }, { "Orange", 0xffd9822b }, { "Amber", 0xffe0b030 }, { "Green", 0xff3fae5a },
                { "Teal", 0xff2fb5b0 }, { "Blue", 0xff3a9ad9 }, { "Purple", 0xff8a6fd1 }, { "Pink", 0xffd96fa8 },
                { "Grey", 0xff9aa0a6 }, { "White", 0xffe8e8e8 }
            };

            juce::PopupMenu menu;
            menu.addItem (1, "Default (by kind of node)");
            menu.addSeparator();
            for (int i = 0; i < (int) std::size (swatches); ++i)
                menu.addItem (100 + i, swatches[i].name);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (colourButton),
                                [this, nodeId] (int result)
            {
                if (result == 0 || ! editor.recolour)
                    return;

                editor.recolour (nodeId, result == 1 ? 0 : swatches[result - 100].argb);
            });
        };

        addAndMakeVisible (*t);
        addAndMakeVisible (*r.extra);
        addAndMakeVisible (*r.extra2);
        addAndMakeVisible (*r.extra3);
        addLearnButtons (r, true);
        rows.push_back (std::move (r));
    }

    void addPresetRow (const Node& node)
    {
        Row r;
        r.kind = Kind::Preset;
        r.target = "preset:" + node.id();
        r.nodeId = node.id();
        r.nodeType = node.type();

        r.label = std::make_unique<juce::Label>();
        r.label->setText ("Preset", juce::dontSendNotification);

        r.presetBox = std::make_unique<juce::ComboBox>();
        r.presetBox->setScrollWheelEnabled (false);
        r.presetBox->setTextWhenNothingSelected ("(none saved)");

        auto* box = r.presetBox.get();
        auto ids = r.presetIds;
        const std::string nodeId = node.id();
        r.presetBox->onChange = [this, box, ids, nodeId]
        {
            const int i = box->getSelectedItemIndex();
            if (i < 0 || i >= ids->size())
                return;

            Command c;
            c.type = Command::Type::LoadNodePreset;
            c.target = nodeId;
            c.text = (*ids)[i];
            rig.submit (std::move (c));
        };

        const juce::String nodeName = node.displayName();
        r.saveButton = std::make_unique<juce::TextButton> ("Save...");
        r.saveButton->onClick = [this, nodeId, nodeName]
        {
            askName ("Save " + nodeName + " preset", "Name for this preset. Saving over an existing name replaces it.",
                     nodeName, [this, nodeId] (const juce::String& name)
                     {
                         juce::String error;
                         if (! rig.saveNodePreset (nodeId, name, error))
                             showError (error);
                     });
        };

        r.extra = std::make_unique<juce::TextButton> ("Delete");
        r.extra->onClick = [this, box, ids, nodeId, nodeName, type = node.type()]
        {
            const int i = box->getSelectedItemIndex();
            if (i < 0 || i >= ids->size() || rig.getLibrary() == nullptr)
                return;

            const juce::String presetId = (*ids)[i];
            const juce::String shown = box->getText().upToFirstOccurrenceOf ("  (slot", false, false);
            const int users = rig.getLibrary()->countNodePresetUsers (type, presetId);

            juce::String message = "Delete the " + nodeName + " preset \"" + shown + "\"?";
            if (users > 0)
                message << "\n\nIt is used by " << users << (users == 1 ? " scene preset" : " scene presets")
                        << ", which will report a missing preset until they are saved again.";

            askConfirm ("Delete preset", message, [this, type, presetId]
            {
                if (rig.getLibrary() != nullptr)
                    rig.getLibrary()->deleteNodePreset (type, presetId);
            });
        };

        addAndMakeVisible (*r.label);
        addAndMakeVisible (*box);
        addAndMakeVisible (*r.saveButton);
        addAndMakeVisible (*r.extra);
        addLearnButtons (r, false);
        rows.push_back (std::move (r));
    }

    // A node's text setting as a list to choose from (the MIDI output node's port).
    void addTextRow (const Node& node)
    {
        Row r;
        r.kind = Kind::Text;
        r.nodeId = node.id();

        r.label = std::make_unique<juce::Label>();
        r.label->setText (node.textLabel(), juce::dontSendNotification);

        r.presetBox = std::make_unique<juce::ComboBox>();
        r.presetBox->setScrollWheelEnabled (false);
        r.presetBox->setTextWhenNothingSelected ("(none chosen)");

        auto options = node.textOptions();
        const juce::String current = node.textValue();
        if (current.isNotEmpty() && ! options.contains (current))
            options.add (current);   // a port that is not plugged in now stays visible

        int item = 1;
        for (const auto& o : options)
            r.presetBox->addItem (o, item++);

        if (current.isNotEmpty())
            r.presetBox->setText (current, juce::dontSendNotification);

        auto* box = r.presetBox.get();
        const std::string nodeId = node.id();
        box->onChange = [this, box, nodeId] { if (editor.setText) editor.setText (nodeId, box->getText()); };

        addAndMakeVisible (*r.label);
        addAndMakeVisible (*box);
        rows.push_back (std::move (r));
    }

    // The MIDI output node's messages: one row each, added as needed, starting empty.
    void addMidiRows (MidiOutNode& node)
    {
        const int count = juce::jlimit (0, MidiOutNode::maxRows, (int) node.rowsParam()->get());
        MidiOutNode* mo = &node;

        {
            Row h;
            h.kind = Kind::Header;
            h.label = std::make_unique<juce::Label>();
            h.label->setText ("Messages, sent in this order when a preset is recalled", juce::dontSendNotification);
            h.label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
            addAndMakeVisible (*h.label);
            rows.push_back (std::move (h));
        }

        for (int k = 0; k < count; ++k)
        {
            Row r;
            r.kind = Kind::MidiMsg;
            r.nodeId = node.id();
            r.index = k;

            r.label = std::make_unique<juce::Label>();
            r.label->setText (juce::String (k + 1), juce::dontSendNotification);
            r.label->setJustificationType (juce::Justification::centredRight);

            r.presetBox = std::make_unique<juce::ComboBox>();
            r.presetBox->setScrollWheelEnabled (false);
            r.presetBox->addItem ("Program Change", 1);
            r.presetBox->addItem ("Control Change", 2);
            r.presetBox->setSelectedId (node.typeParam (k)->on() ? 2 : 1, juce::dontSendNotification);

            auto makeNumber = [] (const char* prefix)
            {
                auto s = std::make_unique<juce::Slider> (juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
                s->setRange (0.0, 127.0, 1.0);
                s->setScrollWheelEnabled (false);
                s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 62, 22);
                s->setTextValueSuffix ("");
                s->setName (prefix);
                return s;
            };

            r.slider = makeNumber ("number");
            r.slider->setValue (node.numberParam (k)->get(), juce::dontSendNotification);
            r.slider2 = makeNumber ("value");
            r.slider2->setValue (node.valueParam (k)->get(), juce::dontSendNotification);
            r.slider2->setVisible (node.typeParam (k)->on());

            auto* typeBox = r.presetBox.get();
            auto* number = r.slider.get();
            auto* value = r.slider2.get();

            typeBox->onChange = [mo, k, typeBox, value]
            {
                const bool cc = typeBox->getSelectedId() == 2;
                mo->typeParam (k)->set (cc ? 1.0f : 0.0f);
                value->setVisible (cc);
            };
            number->onValueChange = [mo, k, number] { mo->numberParam (k)->set ((float) number->getValue()); };
            value->onValueChange = [mo, k, value] { mo->valueParam (k)->set ((float) value->getValue()); };

            r.extra = std::make_unique<juce::TextButton> ("Delete");
            r.extra->onClick = [this, mo, k]
            {
                const int n = (int) mo->rowsParam()->get();
                for (int j = k; j + 1 < n; ++j)   // the rows below move up
                {
                    mo->typeParam (j)->set (mo->typeParam (j + 1)->get());
                    mo->numberParam (j)->set (mo->numberParam (j + 1)->get());
                    mo->valueParam (j)->set (mo->valueParam (j + 1)->get());
                }
                mo->rowsParam()->set ((float) juce::jmax (0, n - 1));
                rig.graph().touch();   // rebuilds the panel
            };

            addAndMakeVisible (*r.label);
            addAndMakeVisible (*typeBox);
            addAndMakeVisible (*number);
            addChildComponent (*value);
            value->setVisible (node.typeParam (k)->on());
            addAndMakeVisible (*r.extra);
            rows.push_back (std::move (r));
        }

        Row add;
        add.kind = Kind::MidiAdd;
        add.nodeId = node.id();
        add.index = count;   // so the timer can see the list changed (a preset was recalled)

        add.extra = std::make_unique<juce::TextButton> ("Add message");
        add.extra->onClick = [this, mo]
        {
            const int n = (int) mo->rowsParam()->get();
            if (n >= MidiOutNode::maxRows)
                return;

            mo->typeParam (n)->set (0.0f);
            mo->numberParam (n)->set (0.0f);
            mo->valueParam (n)->set (0.0f);
            mo->rowsParam()->set ((float) (n + 1));
            rig.graph().touch();
        };

        add.extra2 = std::make_unique<juce::TextButton> ("Send now");
        add.extra2->onClick = [this, mo]
        {
            juce::String problem;
            midiOutProblem = mo->sendNow (problem) ? juce::String ("Sent.") : problem;
        };

        add.fileText = std::make_unique<juce::Label>();

        addAndMakeVisible (*add.extra);
        addAndMakeVisible (*add.extra2);
        addAndMakeVisible (*add.fileText);
        rows.push_back (std::move (add));
    }

    void addFileRow (const Node& node)
    {
        Row r;
        r.kind = Kind::File;
        r.nodeId = node.id();
        r.nodeType = node.type();

        r.label = std::make_unique<juce::Label>();
        const bool isPlugin = node.type() == "vst3" || node.type() == "vsti";
        r.label->setText (isPlugin ? "Plugin" : node.type() == "ir" ? "IR file" : "Capture", juce::dontSendNotification);

        r.fileText = std::make_unique<juce::Label>();
        r.fileText->setText ("(none)", juce::dontSendNotification);
        r.fileText->setMinimumHorizontalScale (0.7f);

        const std::string nodeId = node.id();
        const bool isIr = node.type() == "ir";
        r.extra = std::make_unique<juce::TextButton> ("Load...");
        r.extra->onClick = [this, nodeId, isIr, isPlugin]
        {
            if (isPlugin)
            {
                // The usual place for VST3 plugins on Windows; a plugin can also be a folder.
                juce::File start ("C:\\Program Files\\Common Files\\VST3");
                if (! start.isDirectory())
                    start = juce::File();

                chooser = std::make_unique<juce::FileChooser> ("Select a VST3 plugin", start, "*.vst3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectDirectories,
                                      [this, nodeId] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file != juce::File() && editor.loadFile)
                        editor.loadFile (nodeId, file);
                });
                return;
            }

            if (editor.browse)
            {
                editor.browse (nodeId, isIr);
                return;
            }

            chooser = std::make_unique<juce::FileChooser> (isIr ? "Select a cab IR" : "Select a NAM capture",
                                                           juce::File(), isIr ? "*.wav" : "*.nam");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, nodeId] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file != juce::File() && editor.loadFile)
                    editor.loadFile (nodeId, file);
            });
        };

        if (isPlugin)
        {
            r.extra2 = std::make_unique<juce::TextButton> ("Window");
            r.extra2->onClick = [this, nodeId] { if (editor.openPlugin) editor.openPlugin (nodeId); };
            addAndMakeVisible (*r.extra2);
        }

        addAndMakeVisible (*r.label);
        addAndMakeVisible (*r.fileText);
        addAndMakeVisible (*r.extra);
        rows.push_back (std::move (r));
    }

    // A setting's control. For a connection's on/off switch, `from` and `to` add a disconnect button.
    void addParamRow (Param* p, const juce::String& text, const std::string& from = {}, const std::string& to = {})
    {
        if (p == nullptr)
            return;

        Row r;
        r.kind = Kind::Param;
        r.param = p;
        r.target = p->id;

        if (p->kind == ParamKind::Toggle)
        {
            r.toggle = std::make_unique<juce::ToggleButton> (text);
            r.toggle->setToggleState (p->on(), juce::dontSendNotification);

            auto* t = r.toggle.get();
            t->onClick = [this, p, t] { send (p, t->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (*t);

            if (! from.empty())
            {
                r.extra = std::make_unique<juce::TextButton> ("x");
                r.extra->onClick = [this, from, to]
                {
                    if (editor.disconnect)
                        editor.disconnect (from, to);
                };
                addAndMakeVisible (*r.extra);
            }
        }
        else
        {
            r.label = std::make_unique<juce::Label>();
            r.label->setText (text, juce::dontSendNotification);

            r.slider = std::make_unique<juce::Slider>();
            r.slider->setScrollWheelEnabled (false);   // scrolling the panel must never change a setting
            r.slider->setSliderStyle (juce::Slider::LinearHorizontal);
            r.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
            r.slider->setRange (p->min, p->max, p->step);
            r.slider->setTextValueSuffix (p->unit);
            r.slider->setValue (p->get(), juce::dontSendNotification);

            auto* s = r.slider.get();
            s->onValueChange = [this, p, s] { send (p, (float) s->getValue()); };

            addAndMakeVisible (*r.label);
            addAndMakeVisible (*s);
        }

        addLearnButtons (r, p->kind == ParamKind::Toggle);
        rows.push_back (std::move (r));
    }

    void refreshPresetList (Row& r)
    {
        const Library* library = rig.getLibrary();
        r.presetIds->clear();
        r.presetBox->clear (juce::dontSendNotification);

        if (library == nullptr)
            return;

        int item = 1;
        for (const auto& p : library->listNodePresets (r.nodeType))
        {
            r.presetBox->addItem (p.name + "  (slot " + juce::String (p.slot) + ")", item++);
            r.presetIds->add (p.id);
        }
    }

    void timerCallback() override
    {
        const std::string learningId = router.learningParam();
        const int revision = rig.getLibrary() != nullptr ? rig.getLibrary()->revision.load() : 0;
        const bool listsChanged = revision != lastRevision;
        lastRevision = revision;

        bool dragging = false;

        for (auto& r : rows)
        {
            if (r.slider != nullptr && r.slider->isMouseButtonDown())
                dragging = true;

            if (r.kind == Kind::Preset)
            {
                if (listsChanged)
                    refreshPresetList (r);

                const int index = r.presetIds->indexOf (rig.getNodePresetId (r.nodeId));
                if (r.presetBox->getSelectedItemIndex() != index)
                    r.presetBox->setSelectedItemIndex (index, juce::dontSendNotification);
            }

            if (r.kind == Kind::MidiAdd)
            {
                const auto node = rig.graph().findNode (r.nodeId);
                if (auto* mo = dynamic_cast<MidiOutNode*> (node.get()))
                {
                    if ((int) mo->rowsParam()->get() != r.index && ! rebuildRequested)
                    {
                        rebuildRequested = true;   // a preset changed the list: draw it again
                        rig.graph().touch();
                    }

                    if (r.fileText->getText() != midiOutProblem)
                        r.fileText->setText (midiOutProblem, juce::dontSendNotification);
                }
            }

            if (r.kind == Kind::MidiMsg)
            {
                const auto node = rig.graph().findNode (r.nodeId);
                if (auto* mo = dynamic_cast<MidiOutNode*> (node.get()))
                {
                    const bool cc = mo->typeParam (r.index)->on();
                    if (r.presetBox->getSelectedId() != (cc ? 2 : 1))
                        r.presetBox->setSelectedId (cc ? 2 : 1, juce::dontSendNotification);

                    if (! r.slider->isMouseButtonDown() && (int) r.slider->getValue() != (int) mo->numberParam (r.index)->get())
                        r.slider->setValue (mo->numberParam (r.index)->get(), juce::dontSendNotification);

                    if (! r.slider2->isMouseButtonDown() && (int) r.slider2->getValue() != (int) mo->valueParam (r.index)->get())
                        r.slider2->setValue (mo->valueParam (r.index)->get(), juce::dontSendNotification);

                    r.slider2->setVisible (cc);
                }
            }

            if (r.kind == Kind::File)
            {
                const auto node = rig.graph().findNode (r.nodeId);
                const juce::String name = node != nullptr ? node->fileName() : juce::String();
                const juce::String shown = name.isEmpty() ? juce::String ("(none)") : name;

                if (r.fileText->getText() != shown)
                    r.fileText->setText (shown, juce::dontSendNotification);
            }

            if (r.param != nullptr)
            {
                if (r.toggle != nullptr)
                    r.toggle->setToggleState (r.param->on(), juce::dontSendNotification);
                else if (r.slider != nullptr && ! r.slider->isMouseButtonDown())
                    r.slider->setValue (r.param->get(), juce::dontSendNotification);
            }

            if (r.learn != nullptr)
            {
                const bool isLearning = learningId == r.target;
                const juce::String bound = router.sceneBindingText (r.target);

                r.learn->setButtonText (isLearning ? juce::String ("Learning...") : bound.isNotEmpty() ? bound : juce::String ("MIDI"));
                r.learn->setColour (juce::TextButton::buttonColourId,
                                    isLearning ? juce::Colour (0xffc07a10)
                                               : bound.isNotEmpty() ? juce::Colour (0xff2a6b5a)
                                                                    : juce::Colour (0xff3a3a3a));
                r.clear->setVisible (bound.isNotEmpty());
            }
        }

        // Settings that belong to the graph's layout are saved once a drag has finished.
        if (structuralDirty && ! dragging)
        {
            structuralDirty = false;
            if (editor.structuralChanged)
                editor.structuralChanged();
        }
    }

    Rig& rig;
    MidiRouter& router;
    GraphEditor editor;
    std::vector<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastRevision = -1;
    bool structuralDirty = false;
    bool rebuildRequested = false;
    juce::String midiOutProblem;
};
