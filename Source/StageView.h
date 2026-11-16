#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
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
    std::function<void()> onTuner;                          // the Tuner button, or a touch on the tuner display

    // The tuner display, drawn over the footswitch buttons while the tuner is on. `note` is empty when no
    // pitch is heard; `cents` runs from -50 (flat) to +50 (sharp).
    void setTuner (bool on, const juce::String& note = {}, float cents = 0.0f)
    {
        if (on == tunerOn && note == tunerNote && std::abs (cents - tunerCents) < 0.05f)
            return;

        tunerOn = on;
        tunerNote = note;
        tunerCents = cents;
        repaint();
    }

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

        // The Tuner button, in the corner of the song box.
        g.setColour (tunerOn ? juce::Colour (0xffd9822b) : juce::Colour (0xff2e3238));
        g.fillRoundedRectangle (tunerRect.toFloat(), 6.0f);
        g.setColour (tunerOn ? juce::Colours::black : juce::Colour (0xfff2f2f2));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("Tuner", tunerRect, juce::Justification::centred);

        if (tunerOn)
        {
            paintTuner (g);
            return;   // the footswitch buttons and node chips are covered while tuning
        }

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

        tunerRect = juce::Rectangle<int> (currentRect.getRight() - 92, currentRect.getBottom() - 38, 84, 30);
        tunerArea = area.getUnion (chipArea);

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

        if (tunerRect.contains (p) || (tunerOn && tunerArea.contains (p)))
        {
            if (onTuner) onTuner();
            return;
        }

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
    // The note name large in the middle, and a needle on a scale from 50 cents flat to 50 cents sharp. Green
    // within 3 cents, amber within 10, red beyond.
    void paintTuner (juce::Graphics& g)
    {
        auto r = tunerArea;
        g.setColour (juce::Colour (0xff181a1d));
        g.fillRoundedRectangle (r.toFloat(), 12.0f);

        const bool heard = tunerNote.isNotEmpty();
        const float off = std::abs (tunerCents);
        const auto colour = ! heard ? juce::Colour (0xff4a4e54)
                          : off <= 3.0f ? juce::Colour (0xff3fd06a)
                          : off <= 10.0f ? juce::Colour (0xffe0b030) : juce::Colour (0xffe05a4a);

        auto inner = r.reduced (24, 16);

        g.setColour (juce::Colour (0xffb8bcc2));
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("TUNER   outputs muted   touch here, press the tuner switch, or change part to leave",
                    inner.removeFromTop (26), juce::Justification::centred);

        auto scale = inner.removeFromBottom (juce::jmin (110, inner.getHeight() / 3));
        auto readout = scale.removeFromBottom (30);

        g.setColour (colour);
        g.setFont (juce::FontOptions ((float) juce::jlimit (60, 220, inner.getHeight() - 10), juce::Font::bold));
        g.drawText (heard ? tunerNote : juce::String ("--"), inner, juce::Justification::centred);

        // The scale: a track, a mark every 10 cents, a taller one in the middle, and the needle.
        const auto track = scale.reduced (20, 0).toFloat();
        const float midX = track.getCentreX(), y = track.getCentreY();
        g.setColour (juce::Colour (0xff3a3d42));
        g.fillRoundedRectangle (track.getX(), y - 4.0f, track.getWidth(), 8.0f, 4.0f);

        for (int c = -50; c <= 50; c += 10)
        {
            const float x = midX + track.getWidth() * 0.5f * (float) c / 50.0f;
            const float h = c == 0 ? track.getHeight() * 0.5f : 12.0f;
            g.setColour (c == 0 ? juce::Colour (0xfff2f2f2) : juce::Colour (0xff6a6f76));
            g.fillRect (x - 1.5f, y - h, 3.0f, 2.0f * h);
        }

        if (heard)
        {
            const float x = midX + track.getWidth() * 0.5f * juce::jlimit (-50.0f, 50.0f, tunerCents) / 50.0f;
            g.setColour (colour);
            g.fillRoundedRectangle (x - 7.0f, y - track.getHeight() * 0.45f, 14.0f, track.getHeight() * 0.9f, 5.0f);
        }

        g.setColour (heard ? colour : juce::Colour (0xff6a6f76));
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        const int rounded = juce::roundToInt (tunerCents);
        g.drawText (! heard ? juce::String ("play a string")
                            : rounded == 0 ? juce::String ("in tune")
                            : juce::String (std::abs (rounded)) + (rounded > 0 ? " cents sharp" : " cents flat"),
                    readout, juce::Justification::centred);
    }

    bool tunerOn = false;
    juce::String tunerNote;
    float tunerCents = 0.0f;
    juce::Rectangle<int> tunerRect, tunerArea;

    Song song;
    std::vector<Button> buttons;
    std::vector<std::shared_ptr<Node>> chips;
    juce::Rectangle<int> prevRect, nextRect, currentRect;
    std::vector<juce::Rectangle<int>> buttonRects, chipRects;
};
