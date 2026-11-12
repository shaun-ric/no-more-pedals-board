#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "Core/Library.h"
#include "MidiRouter.h"

// The footswitch buttons, on the Setlist tab. Two parts:
//  - how many buttons the pedal has, and a MIDI learn button for each (global: learned once);
//  - for the selected setlist entry (a song), which scene preset each button recalls.
class SlotsPanel : public juce::Component
{
public:
    std::function<void (int)> onCountChanged;                                   // the number of buttons was changed
    std::function<void (int slotIndex, const juce::String& presetId)> onAssign;  // slotIndex from 0; empty = nothing

    SlotsPanel (Library& lib, MidiRouter& midi) : library (lib), router (midi)
    {
        countLabel.setText ("Footswitch buttons", juce::dontSendNotification);
        learnLabel.setText ("Learn each button: click it, then press the switch on the pedal.", juce::dontSendNotification);
        assignLabel.setText ("Scene presets the selected entry's buttons recall:", juce::dontSendNotification);
        learnLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc2));
        assignLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc2));

        for (int n = 1; n <= maxSlots; ++n)
            countBox.addItem (juce::String (n), n);

        countBox.setScrollWheelEnabled (false);
        countBox.onChange = [this]
        {
            const int n = countBox.getSelectedId();
            if (n >= 1 && n != slotCount)
            {
                setCount (n);
                if (onCountChanged)
                    onCountChanged (n);
            }
        };

        for (auto* c : std::initializer_list<juce::Component*> { &countLabel, &countBox, &learnLabel, &assignLabel })
            addAndMakeVisible (*c);

        setCount (8);
    }

    static constexpr int maxSlots = 20;

    int count() const noexcept { return slotCount; }

    void setCount (int n)
    {
        n = juce::jlimit (1, maxSlots, n);
        slotCount = n;
        countBox.setSelectedId (n, juce::dontSendNotification);

        learn.clear();
        labels.clear();
        combos.clear();

        for (int k = 0; k < n; ++k)
        {
            auto b = std::make_unique<juce::TextButton> (juce::String (k + 1) + ": MIDI");
            const std::string target = "slot:" + std::to_string (k + 1);
            b->onClick = [this, target]
            {
                if (router.learningParam() == target)
                    router.cancelLearn();
                else
                    router.startLearn (target, false);
            };
            addAndMakeVisible (*b);
            learn.push_back (std::move (b));

            auto l = std::make_unique<juce::Label>();
            l->setText (juce::String (k + 1), juce::dontSendNotification);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
            labels.push_back (std::move (l));

            auto c = std::make_unique<juce::ComboBox>();
            c->setScrollWheelEnabled (false);
            c->onChange = [this, k]
            {
                if (suppress)
                    return;

                const int id = combos[(size_t) k]->getSelectedId();
                if (onAssign)
                    onAssign (k, id >= 2 && id - 2 < presetIds.size() ? presetIds[id - 2] : juce::String());
            };
            addAndMakeVisible (*c);
            combos.push_back (std::move (c));
        }

        fillCombos();
        resized();
    }

    // The entry whose buttons are shown (null when none is selected).
    void setEntry (const SetlistEntry* entry)
    {
        hasEntry = entry != nullptr;
        entryScene = entry != nullptr ? entry->scene : juce::String();
        entrySlots = entry != nullptr ? entry->slots : std::vector<juce::String>();
        fillCombos();
    }

    void refresh()
    {
        for (size_t k = 0; k < learn.size(); ++k)
        {
            const std::string target = "slot:" + std::to_string (k + 1);
            const bool learning = router.learningParam() == target;
            const juce::String bound = router.sceneBindingText (target);

            learn[k]->setButtonText (juce::String ((int) k + 1) + ": " + (learning ? juce::String ("Learning...")
                                                                          : bound.isNotEmpty() ? bound : juce::String ("MIDI")));
            learn[k]->setColour (juce::TextButton::buttonColourId,
                                 learning ? juce::Colour (0xffc07a10)
                                          : bound.isNotEmpty() ? juce::Colour (0xff2a6b5a) : juce::Colour (0xff3a3a3a));
        }
    }

    // Height needed for `width` (and lays the parts out when apply is true).
    int layout (int width, bool apply)
    {
        int y = 4;
        const int h = 28, gap = 6;

        auto place = [&] (juce::Component& c, int x, int w) { if (apply) c.setBounds (x, y, w, h); };

        place (countLabel, 0, 150);
        place (countBox, 154, 90);
        y += h + gap;

        place (learnLabel, 0, width);
        y += h;

        const int cols = juce::jmax (1, juce::jmin (4, slotCount));
        const int cellW = (width - gap * (cols - 1)) / cols;
        for (int k = 0; k < slotCount; ++k)
        {
            if (apply)
                learn[(size_t) k]->setBounds ((k % cols) * (cellW + gap), y + (k / cols) * (h + gap), cellW, h);
        }
        y += ((slotCount + cols - 1) / cols) * (h + gap) + gap;

        place (assignLabel, 0, width);
        y += h;

        const int aCols = width > 700 ? 2 : 1;
        const int aW = (width - gap * (aCols - 1)) / aCols;
        for (int k = 0; k < slotCount; ++k)
            if (apply)
            {
                const int x = (k % aCols) * (aW + gap);
                const int yy = y + (k / aCols) * (h + gap);
                labels[(size_t) k]->setBounds (x, yy, 30, h);
                combos[(size_t) k]->setBounds (x + 34, yy, aW - 34, h);
            }
        y += ((slotCount + aCols - 1) / aCols) * (h + gap);

        return y + 4;
    }

    void resized() override { layout (getWidth(), true); }

private:
    void fillCombos()
    {
        presetIds.clear();
        std::vector<ScenePreset> presets;
        if (hasEntry)
            presets = library.listScenePresets (entryScene);

        suppress = true;

        for (size_t k = 0; k < combos.size(); ++k)
        {
            auto& c = *combos[k];
            c.clear (juce::dontSendNotification);
            c.setEnabled (hasEntry);
            c.addItem (hasEntry ? "(nothing)" : "(select an entry above)", 1);

            presetIds.clear();
            int item = 2;
            for (const auto& p : presets)
            {
                c.addItem (p.name, item++);
                presetIds.add (p.id);
            }

            const juce::String wanted = k < entrySlots.size() ? entrySlots[k] : juce::String();
            const int index = wanted.isEmpty() ? -1 : presetIds.indexOf (wanted);
            c.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
        }

        suppress = false;
    }

    Library& library;
    MidiRouter& router;

    int slotCount = 8;
    bool hasEntry = false, suppress = false;
    juce::String entryScene;
    std::vector<juce::String> entrySlots;
    juce::StringArray presetIds;

    juce::Label countLabel, learnLabel, assignLabel;
    juce::ComboBox countBox;
    std::vector<std::unique_ptr<juce::TextButton>> learn;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
};
