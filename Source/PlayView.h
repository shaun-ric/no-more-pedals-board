#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "Core/Node.h"

// The playing view: big scene buttons along the top and one large tile per processing node.
// A tile is bright when its node is active and dim when it is bypassed. Touching a tile toggles
// the node. Nothing else can be changed from here.
class PlayView : public juce::Component
{
public:
    std::function<void (const juce::String& sceneId)> onSelectScene;
    std::function<void (const std::string& nodeId)> onToggleNode;

    // Colour per node type. Kept in one place so it can become a per-node property later.
    static juce::Colour colourForType (const std::string& type)
    {
        if (type == "amp")        return juce::Colour (0xffd9822b);
        if (type == "ir")         return juce::Colour (0xff8a6fd1);
        if (type == "overdrive")  return juce::Colour (0xff3fae5a);
        if (type == "chorus")     return juce::Colour (0xff3a9ad9);
        if (type == "delay")      return juce::Colour (0xff2fb5b0);
        if (type == "reverb")     return juce::Colour (0xff5a74d6);
        if (type == "gate")       return juce::Colour (0xffc9514d);
        if (type == "compressor") return juce::Colour (0xffd1b640);
        if (type.rfind ("eq", 0) == 0) return juce::Colour (0xffc9a227);
        if (type == "vst3" || type == "vsti") return juce::Colour (0xffd0689a);
        if (type == "input")      return juce::Colour (0xff6d8cae);
        if (type == "output")     return juce::Colour (0xffae8a6d);
        return juce::Colour (0xff7a7a7a);
    }

    // The node's own colour if it has one, otherwise its type's.
    static juce::Colour colourFor (const Node& n)
    {
        return n.colour() != 0 ? juce::Colour (n.colour()) : colourForType (n.type());
    }

    void setScenes (const juce::StringArray& names, const juce::StringArray& ids, const juce::String& currentId)
    {
        sceneNames = names;
        sceneIds = ids;
        currentScene = currentId;
        repaint();
    }

    void setNodes (const std::vector<std::shared_ptr<Node>>& all)
    {
        tiles.clear();
        for (const auto& n : all)
            if (! n->isInput() && ! n->isOutput())
                tiles.push_back (n);
        resized();
        repaint();
    }

    // Called from the UI timer so tiles follow changes made by MIDI.
    void refresh() { repaint(); }

    int getRequiredHeightForMeters() const { return 0; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101214));

        for (size_t i = 0; i < sceneRects.size(); ++i)
        {
            const bool current = sceneIds[(int) i] == currentScene;
            auto r = sceneRects[i].toFloat();
            g.setColour (current ? juce::Colour (0xfff2f2f2) : juce::Colour (0xff2a2d31));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (current ? juce::Colours::black : juce::Colour (0xffb8bcc2));
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawFittedText (sceneNames[(int) i], sceneRects[i].reduced (8, 2), juce::Justification::centred, 2);
        }

        if (tiles.empty())
        {
            g.setColour (juce::Colour (0xff8a8f96));
            g.setFont (18.0f);
            g.drawText ("No processing nodes in this scene. Add some on the Edit tab.",
                        getLocalBounds().withTrimmedTop (sceneBarHeight), juce::Justification::centred);
            return;
        }

        for (size_t i = 0; i < tileRects.size() && i < tiles.size(); ++i)
        {
            const auto& n = *tiles[i];
            const bool on = n.activeParam() != nullptr && n.activeParam()->on();
            const auto base = colourFor (n);
            auto r = tileRects[i].toFloat();

            g.setColour (on ? base : base.withSaturation (0.1f).darker (0.85f));
            g.fillRoundedRectangle (r, 12.0f);

            if (on)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r.reduced (1.5f), 12.0f, 3.0f);
            }

            const auto text = on ? juce::Colours::black : juce::Colour (0xff7c8086);
            auto inner = tileRects[i].reduced (14, 10);

            g.setColour (text);
            g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
            g.drawFittedText (n.displayName(), inner.removeFromTop (inner.getHeight() * 55 / 100),
                              juce::Justification::centredLeft, 1);

            g.setFont (juce::FontOptions (16.0f));
            const juce::String sub = n.hasFile() ? (n.fileName().isNotEmpty() ? n.fileName() : juce::String ("(no file)"))
                                                 : juce::String();
            g.drawFittedText (sub, inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft, 2);

            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (on ? "ON" : "OFF", inner, juce::Justification::bottomLeft);
        }
    }

    void resized() override
    {
        sceneRects.clear();
        tileRects.clear();

        auto area = getLocalBounds().reduced (12);

        auto bar = area.removeFromTop (sceneBarHeight - 12);
        area.removeFromTop (14);

        const int n = sceneIds.size();
        if (n > 0)
        {
            const int gap = 8;
            const int w = juce::jmin (260, (bar.getWidth() - gap * (n - 1)) / n);
            for (int i = 0; i < n; ++i)
                sceneRects.push_back (juce::Rectangle<int> (bar.getX() + i * (w + gap), bar.getY(), w, bar.getHeight()));
        }

        const int tileW = 230, tileH = 140, gap = 14;
        const int perRow = juce::jmax (1, (area.getWidth() + gap) / (tileW + gap));
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            const int col = (int) i % perRow, row = (int) i / perRow;
            tileRects.push_back (juce::Rectangle<int> (area.getX() + col * (tileW + gap),
                                                       area.getY() + row * (tileH + gap), tileW, tileH));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        for (size_t i = 0; i < sceneRects.size(); ++i)
            if (sceneRects[i].contains (p))
            {
                if (onSelectScene) onSelectScene (sceneIds[(int) i]);
                return;
            }

        for (size_t i = 0; i < tileRects.size() && i < tiles.size(); ++i)
            if (tileRects[i].contains (p))
            {
                if (onToggleNode) onToggleNode (tiles[i]->id());
                repaint();
                return;
            }
    }

private:
    static constexpr int sceneBarHeight = 84;

    juce::StringArray sceneNames, sceneIds;
    juce::String currentScene;
    std::vector<std::shared_ptr<Node>> tiles;
    std::vector<juce::Rectangle<int>> sceneRects, tileRects;
};
