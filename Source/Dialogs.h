#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Small modal prompts. The window deletes itself when it is dismissed.

// Asks for one name. `done` is called with the trimmed text only if the user presses Save.
inline void askName (const juce::String& title, const juce::String& prompt, const juce::String& initial,
                     std::function<void (const juce::String&)> done)
{
    auto* aw = new juce::AlertWindow (title, prompt, juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", initial);
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, done] (int result)
    {
        if (result != 1)
            return;

        const auto name = aw->getTextEditorContents ("name").trim();
        if (name.isNotEmpty())
            done (name);
    }), true);
}

// Asks for a scene's name and its Program Change number (blank or -1 for none).
inline void askScene (std::function<void (const juce::String&, int)> done)
{
    auto* aw = new juce::AlertWindow ("New scene",
                                      "The new scene starts with a copy of the current scene's MIDI bindings.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", {}, "Name");
    aw->addTextEditor ("pc", {}, "Program Change number, 0 to 127 (leave blank for none)");
    aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, done] (int result)
    {
        if (result != 1)
            return;

        const auto name = aw->getTextEditorContents ("name").trim();
        const auto pcText = aw->getTextEditorContents ("pc").trim();

        if (name.isNotEmpty())
            done (name, pcText.isEmpty() ? -1 : pcText.getIntValue());
    }), true);
}

inline void showError (const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "RigBench", text);
}

// Asks a yes or no question. `onYes` is called only if the user agrees.
inline void askConfirm (const juce::String& title, const juce::String& message, std::function<void()> onYes)
{
    auto* aw = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon);
    aw->addButton ("Yes", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([onYes] (int result)
    {
        if (result == 1)
            onYes();
    }), true);
}

// Asks which kind of node to add, and what to call it. `types` pairs each type ID with its label.
// One kind of node in the Add node list. `category` groups it under a heading.
struct NewNodeChoice
{
    std::string type;
    juce::String label, category;
};

inline void askNewNode (const std::vector<NewNodeChoice>& types,
                        std::function<void (const std::string&, const juce::String&)> done)
{
    auto* aw = new juce::AlertWindow ("Add a node", "A new node starts switched off, so adding one changes nothing you hear.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addComboBox ("type", {}, "Kind of node");
    aw->addTextEditor ("name", {}, "Name (leave blank to use the kind)");
    aw->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Items are listed under a heading for each category. An item's ID is 1000 + its place in `types`.
    auto* box = aw->getComboBoxComponent ("type");
    box->clear (juce::dontSendNotification);

    juce::String lastCategory;
    for (size_t i = 0; i < types.size(); ++i)
    {
        if (types[i].category != lastCategory)
        {
            lastCategory = types[i].category;
            box->addSectionHeading (lastCategory);
        }
        box->addItem (types[i].label, 1000 + (int) i);
    }

    if (! types.empty())
        box->setSelectedId (1000, juce::dontSendNotification);

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, types, done] (int result)
    {
        if (result != 1)
            return;

        const int index = aw->getComboBoxComponent ("type")->getSelectedId() - 1000;
        if (index < 0 || index >= (int) types.size())
            return;

        auto name = aw->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            name = types[(size_t) index].label;

        done (types[(size_t) index].type, name);
    }), true);
}

// Asks which two nodes to connect. `nodes` pairs each node ID with its name.
inline void askConnect (const std::vector<std::pair<std::string, juce::String>>& nodes,
                        std::function<void (const std::string&, const std::string&)> done)
{
    juce::StringArray labels;
    for (const auto& n : nodes)
        labels.add (n.second);

    auto* aw = new juce::AlertWindow ("Connect two nodes", "The signal flows from the first to the second.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addComboBox ("from", labels, "From");
    aw->addComboBox ("to", labels, "To");
    aw->addButton ("Connect", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, nodes, done] (int result)
    {
        if (result != 1)
            return;

        const int from = aw->getComboBoxComponent ("from")->getSelectedItemIndex();
        const int to = aw->getComboBoxComponent ("to")->getSelectedItemIndex();

        if (from >= 0 && to >= 0 && from < (int) nodes.size() && to < (int) nodes.size())
            done (nodes[(size_t) from].first, nodes[(size_t) to].first);
    }), true);
}
