#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "Core/Library.h"
#include "Core/Rig.h"
#include "Dialogs.h"
#include "MidiRouter.h"
#include "SceneManager.h"
#include "SetlistManager.h"
#include "SlotsPanel.h"

// The Setlist tab: choose or create a setlist, build it from scene and scene preset pairs, put them in
// order, and bind switches to next and previous. A setlist is just a saved list of pairs. Its entries are
// the buttons on the Play tab. Double-click an entry (or press Go) to jump to it.
class SetlistPanel : public juce::Component,
                     private juce::ListBoxModel
{
public:
    std::function<void()> onSetlistChanged;   // the current setlist was switched, or its entries edited
    std::function<void (int)> onSlotCountChanged;   // the number of footswitch buttons was changed

    int slotCount() const noexcept { return slots.count(); }
    void setSlotCount (int n) { slots.setCount (n); }

    SetlistPanel (Library& lib, SceneManager& scenes, SetlistManager& lists, MidiRouter& midi, Rig& rigToUse)
        : library (lib), sceneManager (scenes), setlists (lists), router (midi), rig (rigToUse)
    {
        for (auto* l : { &setlistLabel, &nextLabel, &prevLabel, &addLabel })
            addAndMakeVisible (*l);

        setlistLabel.setText ("Setlist", juce::dontSendNotification);
        nextLabel.setText ("Next entry", juce::dontSendNotification);
        prevLabel.setText ("Previous entry", juce::dontSendNotification);
        addLabel.setText ("Add", juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { &setlistBox, &sceneBox, &presetBox, &newButton, &deleteButton,
                                                                  &addButton, &addCurrentButton, &upButton, &downButton,
                                                                  &removeButton, &renameButton, &goButton, &nextLearn, &prevLearn, &list })
            addAndMakeVisible (*c);

        addChildComponent (nextClear);
        addChildComponent (prevClear);

        list.setModel (this);
        list.setRowHeight (30);

        slotsViewport.setViewedComponent (&slots, false);
        slotsViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (slotsViewport);

        slots.onCountChanged = [this] (int n) { if (onSlotCountChanged) onSlotCountChanged (n); };
        slots.onAssign = [this] (int slot, const juce::String& presetId)
        {
            const int row = list.getSelectedRow();
            auto s = setlists.setlist();
            if (row < 0 || row >= (int) s.entries.size())
                return;

            auto& e = s.entries[(size_t) row];
            if ((int) e.slots.size() < slots.count())
                e.slots.resize ((size_t) slots.count());
            e.slots[(size_t) slot] = presetId;
            save (s);
        };

        setlistBox.setTextWhenNothingSelected ("(none)");
        setlistBox.onChange = [this]
        {
            const int i = setlistBox.getSelectedItemIndex();
            if (i < 0 || i >= setlistIds.size())
                return;

            setlists.load (setlistIds[i]);
            list.updateContent();
            list.repaint();
            changed();
        };

        for (auto* box : { &setlistBox, &sceneBox, &presetBox })
            box->setScrollWheelEnabled (false);

        sceneBox.onChange = [this] { refreshPresets(); };
        presetBox.setTextWhenNothingSelected ("(first preset)");

        newButton.onClick = [this]
        {
            askName ("New setlist", "Name for the new setlist.", "Setlist", [this] (const juce::String& name)
            {
                juce::String id = Library::slug (name);
                const juce::String base = id;
                for (int n = 2; library.readSetlist (id, existing); ++n)
                    id = base + "-" + juce::String (n);

                Setlist s;
                s.id = id;
                s.name = name;
                library.writeSetlist (s);
                setlists.load (id);
                changed();
            });
        };

        deleteButton.onClick = [this]
        {
            if (! setlists.hasSetlist())
                return;

            const auto s = setlists.setlist();
            askConfirm ("Delete " + s.name, "Delete the setlist \"" + s.name + "\"? The scenes and presets in it are not touched.",
                        [this, s]
                        {
                            library.deleteSetlist (s.id);
                            setlists.load ({});
                            changed();
                        });
        };

        addButton.onClick = [this]
        {
            const int si = sceneBox.getSelectedItemIndex();
            if (si < 0 || si >= sceneIds.size())
                return;

            const int pi = presetBox.getSelectedItemIndex();   // 0 is "(first preset)"
            addEntry (sceneIds[si], pi > 0 && pi - 1 < presetIds.size() ? presetIds[pi - 1] : juce::String());
        };

        addCurrentButton.onClick = [this] { addEntry (sceneManager.currentSceneId(), rig.getCurrentScenePreset()); };

        upButton.onClick = [this] { moveSelected (-1); };
        downButton.onClick = [this] { moveSelected (1); };

        removeButton.onClick = [this]
        {
            const int row = list.getSelectedRow();
            auto s = setlists.setlist();
            if (row < 0 || row >= (int) s.entries.size())
                return;

            s.entries.erase (s.entries.begin() + row);
            save (s);
        };

        renameButton.onClick = [this]
        {
            const int row = list.getSelectedRow();
            const auto current = setlists.setlist();
            if (row < 0 || row >= (int) current.entries.size())
                return;

            const auto& e = current.entries[(size_t) row];
            askName ("Name this entry", "What this entry is called, such as a song title. It is the button on the Play tab.",
                     e.name.isNotEmpty() ? e.name : defaultName (e.scene, e.preset),
                     [this, row] (const juce::String& name)
                     {
                         auto s2 = setlists.setlist();
                         if (row >= 0 && row < (int) s2.entries.size())
                         {
                             s2.entries[(size_t) row].name = name;
                             save (s2);
                         }
                     });
        };

        goButton.onClick = [this]
        {
            const int row = list.getSelectedRow();
            if (row >= 0)
                setlists.go (row);
        };

        auto learn = [this] (juce::TextButton& b, const std::string& target)
        {
            b.onClick = [this, target]
            {
                if (router.learningParam() == target)
                    router.cancelLearn();
                else
                    router.startLearn (target, false);
            };
        };

        learn (nextLearn, "setlist:next");
        learn (prevLearn, "setlist:prev");
        nextClear.onClick = [this] { router.clearSceneBinding ("setlist:next"); };
        prevClear.onClick = [this] { router.clearSceneBinding ("setlist:prev"); };

        newButton.setButtonText ("New...");
        deleteButton.setButtonText ("Delete");
        addButton.setButtonText ("Add");
        addCurrentButton.setButtonText ("Add current");
        upButton.setButtonText ("Move up");
        downButton.setButtonText ("Move down");
        removeButton.setButtonText ("Remove");
        renameButton.setButtonText ("Rename...");
        goButton.setButtonText ("Go to entry");
        nextLearn.setButtonText ("MIDI");
        prevLearn.setButtonText ("MIDI");
        nextClear.setButtonText ("x");
        prevClear.setButtonText ("x");
    }

    // Called from the UI timer.
    void refresh()
    {
        const int revision = library.revision.load();
        if (revision != lastRevision)
        {
            lastRevision = revision;
            refreshLists();
        }

        const int playing = setlists.playingIndex();
        if (playing != lastPlaying)
        {
            lastPlaying = playing;
            list.repaint();
        }

        auto update = [this] (juce::TextButton& b, juce::TextButton& clear, const std::string& target)
        {
            const bool learning = router.learningParam() == target;
            const juce::String bound = router.sceneBindingText (target);

            b.setButtonText (learning ? juce::String ("Learning...") : bound.isNotEmpty() ? bound : juce::String ("MIDI"));
            b.setColour (juce::TextButton::buttonColourId,
                         learning ? juce::Colour (0xffc07a10) : bound.isNotEmpty() ? juce::Colour (0xff2a6b5a) : juce::Colour (0xff3a3a3a));
            clear.setVisible (isVisible() && bound.isNotEmpty());
        };

        update (nextLearn, nextClear, "setlist:next");
        update (prevLearn, prevClear, "setlist:prev");
        slots.refresh();
    }

    void visibilityChanged() override { lastRevision = -1; }

    void resized() override
    {
        auto area = getLocalBounds();
        auto row = [&area] (int h = 30) { auto r = area.removeFromTop (h); area.removeFromTop (6); return r; };

        {
            auto r = row();
            setlistLabel.setBounds (r.removeFromLeft (110));
            deleteButton.setBounds (r.removeFromRight (90));
            r.removeFromRight (6);
            newButton.setBounds (r.removeFromRight (90));
            r.removeFromRight (6);
            setlistBox.setBounds (r);
        }

        {
            auto r = row();
            nextLabel.setBounds (r.removeFromLeft (110));
            nextLearn.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (4);
            nextClear.setBounds (r.removeFromLeft (24));
            r.removeFromLeft (30);
            prevLabel.setBounds (r.removeFromLeft (120));
            prevLearn.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (4);
            prevClear.setBounds (r.removeFromLeft (24));
        }

        auto bottom = area.removeFromBottom (30 * 2 + 12);
        area.removeFromBottom (6);

        // The footswitch buttons take the lower part of what is left (it scrolls if it does not fit).
        const int slotsH = juce::jlimit (150, 420, area.getHeight() * 45 / 100);
        slotsViewport.setBounds (area.removeFromBottom (slotsH));
        area.removeFromBottom (6);
        slots.setSize (slotsViewport.getMaximumVisibleWidth(), slots.layout (slotsViewport.getMaximumVisibleWidth(), false));

        list.setBounds (area);

        {
            auto r = bottom.removeFromTop (30);
            bottom.removeFromTop (6);
            addLabel.setBounds (r.removeFromLeft (110));
            addCurrentButton.setBounds (r.removeFromRight (110));
            r.removeFromRight (6);
            addButton.setBounds (r.removeFromRight (70));
            r.removeFromRight (6);
            const int w = (r.getWidth() - 6) / 2;
            sceneBox.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (6);
            presetBox.setBounds (r);
        }

        {
            auto r = bottom.removeFromTop (30);
            upButton.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (6);
            downButton.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (6);
            removeButton.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (6);
            renameButton.setBounds (r.removeFromLeft (110));
            goButton.setBounds (r.removeFromRight (130));
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1b1d20)); }

private:
    // ---- ListBoxModel ---------------------------------------------------------------------------
    int getNumRows() override { return (int) setlists.setlist().entries.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto& entries = setlists.setlist().entries;
        if (row < 0 || row >= (int) entries.size())
            return;

        const bool playing = row == setlists.playingIndex();

        if (selected)
            g.fillAll (juce::Colour (0xff35506e));
        else if (playing)
            g.fillAll (juce::Colour (0xff2b4a33));

        g.setColour (juce::Colours::white);
        g.setFont (16.0f);
        const auto& entry = entries[(size_t) row];
        const juce::String where = describe (entry);
        const juce::String text = juce::String (row + 1) + ".  "
                                  + (entry.name.isNotEmpty() ? entry.name + "      (" + where + ")" : where)
                                  + (playing ? "     (playing)" : "");
        g.drawText (text, 10, 0, width - 20, height, juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { setlists.go (row); }
    void selectedRowsChanged (int) override { updateSlots(); }

    // Shows the selected entry's footswitch assignments.
    void updateSlots()
    {
        const int row = list.getSelectedRow();
        const auto& entries = setlists.setlist().entries;
        slots.setEntry (row >= 0 && row < (int) entries.size() ? &entries[(size_t) row] : nullptr);
    }

    // ---- Editing --------------------------------------------------------------------------------
    // The name a new entry starts with: its preset's name, or its scene's if it uses the first preset.
    juce::String defaultName (const juce::String& scene, const juce::String& preset) const
    {
        if (preset.isNotEmpty())
            for (const auto& p : library.listScenePresets (scene))
                if (p.id == preset)
                    return p.name;

        for (const auto& sc : library.listScenes())
            if (sc.id == scene)
                return sc.name;

        return scene;
    }

    juce::String describe (const SetlistEntry& e) const
    {
        juce::String sceneName = e.scene + " (missing)";
        for (const auto& s : library.listScenes())
            if (s.id == e.scene)
                sceneName = s.name;

        juce::String presetName = "first preset";
        if (e.preset.isNotEmpty())
        {
            presetName = e.preset + " (missing)";
            for (const auto& p : library.listScenePresets (e.scene))
                if (p.id == e.preset)
                    presetName = p.name;
        }

        return sceneName + "   /   " + presetName;
    }

    void addEntry (const juce::String& scene, const juce::String& preset)
    {
        if (! setlists.hasSetlist() || scene.isEmpty())
            return;

        // Each entry is a song (or a part of one), so it is named as it is added. The name is what the
        // button on the Play tab says.
        askName ("Name this entry", "The song title, or whatever this entry should be called. It is the button on the Play tab.",
                 defaultName (scene, preset), [this, scene, preset] (const juce::String& name)
                 {
                     auto s = setlists.setlist();
                     s.entries.push_back ({ scene, preset, name });
                     save (s);
                     list.selectRow ((int) s.entries.size() - 1);
                 });
    }

    void moveSelected (int delta)
    {
        const int row = list.getSelectedRow();
        auto s = setlists.setlist();
        const int to = row + delta;

        if (row < 0 || to < 0 || row >= (int) s.entries.size() || to >= (int) s.entries.size())
            return;

        std::swap (s.entries[(size_t) row], s.entries[(size_t) to]);
        save (s);
        list.selectRow (to);
    }

    void save (const Setlist& s)
    {
        library.writeSetlist (s);
        setlists.reload();
        list.updateContent();
        list.repaint();
        updateSlots();
        changed();
    }

    void changed()
    {
        lastRevision = -1;   // refresh the lists on the next tick
        if (onSetlistChanged)
            onSetlistChanged();
    }

    void refreshLists()
    {
        setlistIds.clear();
        setlistBox.clear (juce::dontSendNotification);

        int item = 1;
        for (const auto& s : library.listSetlists())
        {
            setlistBox.addItem (s.name, item++);
            setlistIds.add (s.id);
        }

        setlistBox.setSelectedItemIndex (setlistIds.indexOf (setlists.setlist().id), juce::dontSendNotification);

        const juce::String keepScene = sceneBox.getSelectedItemIndex() >= 0 && sceneBox.getSelectedItemIndex() < sceneIds.size()
                                           ? sceneIds[sceneBox.getSelectedItemIndex()] : juce::String();
        sceneIds.clear();
        sceneBox.clear (juce::dontSendNotification);

        item = 1;
        for (const auto& s : library.listScenes())
        {
            sceneBox.addItem (s.name, item++);
            sceneIds.add (s.id);
        }

        int idx = sceneIds.indexOf (keepScene);
        if (idx < 0)
            idx = sceneIds.indexOf (sceneManager.currentSceneId());
        sceneBox.setSelectedItemIndex (juce::jmax (0, idx), juce::dontSendNotification);

        refreshPresets();
        list.updateContent();
        list.repaint();
        updateSlots();
    }

    void refreshPresets()
    {
        presetIds.clear();
        presetBox.clear (juce::dontSendNotification);
        presetBox.addItem ("(first preset)", 1);

        const int si = sceneBox.getSelectedItemIndex();
        if (si >= 0 && si < sceneIds.size())
        {
            int item = 2;
            for (const auto& p : library.listScenePresets (sceneIds[si]))
            {
                presetBox.addItem (p.name, item++);
                presetIds.add (p.id);
            }
        }

        presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    Library& library;
    SceneManager& sceneManager;
    SetlistManager& setlists;
    MidiRouter& router;
    Rig& rig;

    juce::Label setlistLabel, nextLabel, prevLabel, addLabel;
    juce::ComboBox setlistBox, sceneBox, presetBox;
    juce::TextButton newButton, deleteButton, addButton, addCurrentButton, upButton, downButton, removeButton, renameButton, goButton;
    juce::TextButton nextLearn, prevLearn, nextClear, prevClear;
    juce::ListBox list;
    SlotsPanel slots { library, router };
    juce::Viewport slotsViewport;

    juce::StringArray setlistIds, sceneIds, presetIds;
    Setlist existing;   // scratch for id checks
    int lastRevision = -1;
    int lastPlaying = -2;
};
