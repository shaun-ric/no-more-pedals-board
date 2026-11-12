#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "Core/Node.h"
#include "PlayView.h"

// A second playing view, built around songs: the previous, current and next entry of the setlist, a
// large button for each footswitch slot (showing the scene preset it recalls in this song), and a strip
// of node chips. Everything can be touched: the song names step through the setlist, the slot buttons
// recall their scene preset, and a chip switches its node.
class StageView : public juce::Component
{
public:
    struct Song
    {
        juce::String previous, current, next, detail;   // detail: the scene and position, such as "Scene: Clean   3 of 12"
        bool inSetlist = false;
        bool operator== (const Song& o) const
        {
            return previous == o.previous && current == o.current && next == o.next && detail == o.detail && inSetlist == o.inSetlist;
        }
    };

    struct Button
    {
        juce::String preset;    // what it recalls; empty = nothing
        bool active = false;    // that preset is the one playing
        bool operator== (const Button& o) const { return preset == o.preset && active == o.active; }
    };

    std::function<void (int delta)> onStep;
    std::function<void (int slot)> onSlot;                  // from 1
    std::function<void (const std::string& nodeId)> onToggleNode;

    void setState (const Song& s, const std::vector<Button>& b)
    {
        if (s == song && b == buttons)
            return;

        song = s;
        buttons = b;
        resized();
        repaint();
    }

    void setNodes (const std::vector<std::shared_ptr<Node>>& all)
    {
        chips.clear();
        for (const auto& n : all)
            if (! n->isInput() && ! n->isOutput())
                chips.push_back (n);
        resized();
        repaint();
    }

    void refresh() { repaint(); }   // chips follow changes made by MIDI

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101214));

        // ---- The song header -------------------------------------------------------------------
        auto drawSide = [&] (juce::Rectangle<int> r, const juce::String& text, const char* arrow, bool enabled)
        {
            g.setColour (enabled ? juce::Colour (0xff23262a) : juce::Colour (0xff17191c));
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
            g.setColour (enabled ? juce::Colour (0xffb8bcc2) : juce::Colour (0xff4a4e54));
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (arrow, r.reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawFittedText (text, r.reduced (12, 8).withTrimmedTop (22), juce::Justification::centred, 2);
        };

        drawSide (prevRect, song.previous, "Previous", song.inSetlist && song.previous.isNotEmpty());
        drawSide (nextRect, song.next, "Next", song.inSetlist && song.next.isNotEmpty());

        g.setColour (juce::Colour (0xfff2f2f2));
        g.fillRoundedRectangle (currentRect.toFloat(), 12.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawFittedText (song.current.isNotEmpty() ? song.current : juce::String ("(no song)"),
                          currentRect.reduced (16, 10).withTrimmedBottom (28), juce::Justification::centred, 2);
        g.setFont (juce::FontOptions (16.0f));
        g.setColour (juce::Colour (0xff3a3d42));
        g.drawText (song.detail, currentRect.reduced (16, 8).removeFromBottom (24), juce::Justification::centred);

        // ---- The footswitch buttons ----------------------------------------------------------------
        for (size_t i = 0; i < buttonRects.size() && i < buttons.size(); ++i)
        {
            const auto& b = buttons[i];
            const bool assigned = b.preset.isNotEmpty();
            auto r = buttonRects[i].toFloat();

            g.setColour (b.active ? juce::Colour (0xffd9822b) : assigned ? juce::Colour (0xff2e3238) : juce::Colour (0xff181a1d));
            g.fillRoundedRectangle (r, 12.0f);

            if (b.active)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.reduced (1.5f), 12.0f, 3.0f);
            }

            const auto text = b.active ? juce::Colours::black : assigned ? juce::Colour (0xfff2f2f2) : juce::Colour (0xff4a4e54);
            auto inner = buttonRects[i].reduced (12, 8);

            g.setColour (text.withAlpha (b.active ? 0.7f : 0.6f));
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (juce::String ((int) i + 1), inner.removeFromTop (24), juce::Justification::centredLeft);

            g.setColour (text);
            g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
            g.drawFittedText (assigned ? b.preset : juce::String ("-"), inner, juce::Justification::centred, 2);
        }

        // ---- Node chips ---------------------------------------------------------------------------
        for (size_t i = 0; i < chipRects.size() && i < chips.size(); ++i)
        {
            const auto& n = *chips[i];
            const bool on = n.activeParam() != nullptr && n.activeParam()->on();
            const auto base = PlayView::colourFor (n);

            g.setColour (on ? base : juce::Colour (0xff23262a));
            g.fillRoundedRectangle (chipRects[i].toFloat(), 8.0f);
            g.setColour (on ? juce::Colours::black : juce::Colour (0xff7c8086));
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawFittedText (n.displayName(), chipRects[i].reduced (8, 2), juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        auto header = area.removeFromTop (juce::jmin (170, area.getHeight() / 4));
        area.removeFromTop (12);
        const int sideW = header.getWidth() / 4;
        prevRect = header.removeFromLeft (sideW);
        nextRect = header.removeFromRight (sideW);
        header.reduce (12, 0);
        currentRect = header;

        chipRects.clear();
        auto chipArea = area.removeFromBottom (chips.empty() ? 0 : 56);
        if (! chips.empty())
        {
            area.removeFromBottom (12);
            const int gap = 8;
            const int w = juce::jmin (180, (chipArea.getWidth() - gap * ((int) chips.size() - 1)) / (int) chips.size());
            for (size_t i = 0; i < chips.size(); ++i)
                chipRects.push_back (juce::Rectangle<int> (chipArea.getX() + (int) i * (w + gap), chipArea.getY(), w, chipArea.getHeight()));
        }

        buttonRects.clear();
        const int n = (int) buttons.size();
        if (n > 0)
        {
            const int cols = n <= 4 ? n : (n + 1) / 2;
            const int rows = (n + cols - 1) / cols;
            const int gap = 12;
            const int w = (area.getWidth() - gap * (cols - 1)) / cols;
            const int h = (area.getHeight() - gap * (rows - 1)) / rows;
            for (int i = 0; i < n; ++i)
                buttonRects.push_back (juce::Rectangle<int> (area.getX() + (i % cols) * (w + gap),
                                                             area.getY() + (i / cols) * (h + gap), w, h));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        if (prevRect.contains (p)) { if (onStep && song.inSetlist) onStep (-1); return; }
        if (nextRect.contains (p)) { if (onStep && song.inSetlist) onStep (1); return; }

        for (size_t i = 0; i < buttonRects.size(); ++i)
            if (buttonRects[i].contains (p))
            {
                if (onSlot) onSlot ((int) i + 1);
                return;
            }

        for (size_t i = 0; i < chipRects.size() && i < chips.size(); ++i)
            if (chipRects[i].contains (p))
            {
                if (onToggleNode) onToggleNode (chips[i]->id());
                repaint();
                return;
            }
    }

private:
    Song song;
    std::vector<Button> buttons;
    std::vector<std::shared_ptr<Node>> chips;
    juce::Rectangle<int> prevRect, nextRect, currentRect;
    std::vector<juce::Rectangle<int>> buttonRects, chipRects;
};
