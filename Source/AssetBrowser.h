#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <vector>

#include "Core/Library.h"
#include "NamMetadata.h"

// Lists the capture (.nam) or cab IR (.wav) files in the library, with what each NAM file says about
// itself, and a filter box. Choosing one calls `onChosen`. "Browse elsewhere..." opens an ordinary file
// chooser for a file outside the library (a scene then refers to it by its full path, and finds it by
// name in the library if it has moved).
class AssetBrowser : public juce::Component,
                     private juce::ListBoxModel
{
public:
    std::function<void (const juce::File&)> onChosen;

    AssetBrowser (const Library& lib, bool forIr) : library (lib), isIr (forIr)
    {
        files = library.listAssets (isIr);

        struct ByName { bool operator() (const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural (b.getFileName()) < 0; } };
        std::sort (files.begin(), files.end(), ByName());

        filter.setTextToShowWhenEmpty ("Type to filter by name", juce::Colours::grey);
        filter.onTextChange = [this] { rebuildShown(); };
        addAndMakeVisible (filter);

        info.setText (juce::String (files.size()) + (isIr ? " IR files" : " captures") + " in " + library.assetFolder (isIr).getFullPathName()
                          + (isIr ? juce::String() : juce::String ("     Columns: name, gear, tone tag (can be wrong), loudness, input level, size")),
                      juce::dontSendNotification);
        info.setColour (juce::Label::textColourId, juce::Colour (0xff8a8f96));
        info.setFont (13.0f);
        addAndMakeVisible (info);

        list.setModel (this);
        list.setRowHeight (28);
        addAndMakeVisible (list);

        chooseButton.setButtonText ("Use this one");
        chooseButton.onClick = [this] { chooseRow (list.getSelectedRow()); };
        addAndMakeVisible (chooseButton);

        elsewhereButton.setButtonText ("Browse elsewhere...");
        elsewhereButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (isIr ? "Select a cab IR" : "Select a NAM capture",
                                                           library.assetFolder (isIr), isIr ? "*.wav" : "*.nam");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f != juce::File())
                    finish (f);
            });
        };
        addAndMakeVisible (elsewhereButton);

        cancelButton.setButtonText ("Cancel");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        rebuildShown();
        setSize (860, 520);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        filter.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        info.setBounds (area.removeFromTop (20));
        area.removeFromTop (4);

        auto bottom = area.removeFromBottom (30);
        area.removeFromBottom (8);
        list.setBounds (area);

        chooseButton.setBounds (bottom.removeFromRight (130));
        bottom.removeFromRight (8);
        cancelButton.setBounds (bottom.removeFromRight (90));
        elsewhereButton.setBounds (bottom.removeFromLeft (170));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1b1d20)); }

private:
    int getNumRows() override { return shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= shown.size())
            return;

        if (selected)
            g.fillAll (juce::Colour (0xff35506e));

        const auto& f = shown.getReference (row);
        const Details d = details (f);

        g.setColour (juce::Colours::white);
        g.setFont (15.0f);

        int x = 10;
        auto cell = [&] (const juce::String& text, int w, juce::Colour colour = juce::Colours::white)
        {
            g.setColour (colour);
            g.drawText (text, x, 0, w - 8, height, juce::Justification::centredLeft, true);
            x += w;
        };

        const int nameW = juce::jmax (200, width - 10 - (isIr ? 110 : 410));
        cell (f.getFileNameWithoutExtension(), nameW);

        const auto dim = juce::Colour (0xffb0b4ba);
        if (isIr)
        {
            cell (d.size, 100, dim);
        }
        else
        {
            cell (d.gear, 90, dim);
            cell (d.tone, 90, dim);
            cell (d.loudness, 80, dim);
            cell (d.input, 80, dim);
            cell (d.size, 70, dim);
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { chooseRow (row); }

    void returnKeyPressed (int row) override { chooseRow (row); }

    struct Details
    {
        juce::String gear, tone, loudness, input, size;
    };

    // Read the first time a row is drawn, so opening the list does not read every file.
    Details details (const juce::File& f) const
    {
        const auto key = f.getFullPathName();
        const auto it = cache.find (key);
        if (it != cache.end())
            return it->second;

        Details d;
        const auto kb = f.getSize() / 1024;
        d.size = kb >= 1024 ? juce::String ((double) kb / 1024.0, 1) + " MB" : juce::String (kb) + " KB";

        if (! isIr)
        {
            NamMetadata m;
            if (readNamMetadata (f, m))
            {
                d.gear = m.gearType;
                d.tone = m.toneType;
                if (std::isfinite (m.loudness))
                    d.loudness = juce::String (m.loudness, 1) + " dB";
                if (std::isfinite (m.inputLevelDbu))
                    d.input = juce::String (m.inputLevelDbu, 1) + " dBu";
            }
        }

        cache[key] = d;
        return d;
    }

    void rebuildShown()
    {
        shown.clear();
        const auto text = filter.getText().trim();

        for (const auto& f : files)
            if (text.isEmpty() || f.getFileName().containsIgnoreCase (text))
                shown.add (f);

        list.updateContent();
        list.repaint();
    }

    void chooseRow (int row)
    {
        if (row >= 0 && row < shown.size())
            finish (shown[row]);
    }

    void finish (const juce::File& f)
    {
        if (onChosen)
            onChosen (f);

        close();
    }

    void close()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    const Library& library;
    bool isIr;

    juce::Array<juce::File> files, shown;
    mutable std::map<juce::String, Details> cache;

    juce::TextEditor filter;
    juce::Label info;
    juce::ListBox list;
    juce::TextButton chooseButton, elsewhereButton, cancelButton;
    std::unique_ptr<juce::FileChooser> chooser;
};
