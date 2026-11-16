#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "AudioEngine.h"

// The master output volume, one control per interface output (README section 39). The volume lives outside
// scenes; MainComponent keeps the values, saves them in settings.json and gives them to the engine. Two looks:
//  - Stage: vertical channel strips (name, level meter, fader, value), which can be minimised to thin meters;
//  - Setup: one row per output (name, horizontal slider, level meter).
// Both read the engine's output meters, which show what actually leaves each output, after the master.
class OutputStrips : public juce::Component,
                     private juce::Timer
{
public:
    enum class Style { stage, setup };

    static constexpr float offDb = -60.0f;   // the bottom of the fader: silent
    static constexpr float maxDb = 0.0f;     // the outputs are limited before this, so no boost

    static float gainFor (float db) { return db <= offDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

    OutputStrips (AudioEngine& e, Style s) : engine (e), style (s)
    {
        collapseButton.onClick = [this] { setCollapsed (! collapsed); if (onCollapsedChanged) onCollapsedChanged (collapsed); };
        collapseButton.setTooltip ("Minimise or show the output strips");
        if (style == Style::stage)
            addAndMakeVisible (collapseButton);

        startTimerHz (20);
    }

    std::function<void (int channel, float db)> onChange;   // a fader was moved (message thread)
    std::function<void (bool collapsed)> onCollapsedChanged;

    // The device's active outputs, in the order the audio callback sees them. Rebuilds the strips.
    void setChannels (const juce::StringArray& names, const std::vector<float>& dbs)
    {
        strips.clear();

        for (int c = 0; c < juce::jmin (names.size(), AudioEngine::maxOutputs); ++c)
        {
            auto s = std::make_unique<Strip>();
            s->name = names[c];

            auto& sl = s->slider;
            sl.setSliderStyle (style == Style::stage ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
            sl.setTextBoxStyle (style == Style::stage ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight, false, 70, 24);
            sl.setRange (offDb, maxDb, 0.5);
            sl.setSkewFactorFromMidPoint (-18.0);
            sl.setDoubleClickReturnValue (true, 0.0);
            // A touch on the track does not jump the volume there: only dragging the fader moves it, so a
            // stray touch on stage cannot suddenly make an output loud or silent.
            sl.setSliderSnapsToMousePosition (false);
            sl.setScrollWheelEnabled (false);   // the mouse wheel never changes a setting
            sl.textFromValueFunction = [] (double v) { return textFor ((float) v); };
            sl.valueFromTextFunction = [] (const juce::String& t)
            {
                return t.trim().equalsIgnoreCase ("off") ? (double) offDb : juce::jlimit ((double) offDb, (double) maxDb, t.getDoubleValue());
            };
            sl.setValue (c < (int) dbs.size() ? dbs[(size_t) c] : 0.0, juce::dontSendNotification);
            sl.onValueChange = [this, c]
            {
                const float db = (float) strips[(size_t) c]->slider.getValue();
                if (onChange)
                    onChange (c, db);
                repaint();
            };
            addChildComponent (sl);
            strips.push_back (std::move (s));
        }

        updateVisibility();
        resized();
        repaint();
    }

    void setDb (int channel, float db)
    {
        if (channel >= 0 && channel < (int) strips.size())
            strips[(size_t) channel]->slider.setValue (db, juce::dontSendNotification);
        repaint();
    }

    void setCollapsed (bool c)
    {
        collapsed = c;
        updateVisibility();
        if (auto* p = getParentComponent())
            p->resized();   // the strips' width changes
        resized();
        repaint();
    }

    bool isCollapsed() const { return collapsed; }

    // Stage: the width the strips want. Setup: the height of the rows.
    int preferredWidth() const
    {
        if (strips.empty())
            return 0;
        return collapsed ? 20 + 14 * (int) strips.size() : 16 + stripWidth * (int) strips.size();
    }
    int preferredHeight() const { return 26 + rowHeight * (int) strips.size(); }

    static juce::String textFor (float db) { return db <= offDb ? juce::String ("Off") : juce::String (db, 1) + " dB"; }

    void resized() override
    {
        auto area = getLocalBounds();

        if (style == Style::setup)
        {
            area.removeFromTop (26);   // the heading
            for (auto& s : strips)
            {
                auto row = area.removeFromTop (rowHeight).withTrimmedBottom (6);
                s->nameArea = row.removeFromLeft (190);
                s->meterArea = row.removeFromRight (200).reduced (0, 6);
                row.removeFromRight (12);
                s->slider.setBounds (row);
            }
            return;
        }

        auto header = area.removeFromTop (36);
        collapseButton.setButtonText (collapsed ? "<" : ">");
        collapseButton.setBounds (header.removeFromRight (36).reduced (4));
        titleArea = header;

        if (collapsed)
        {
            area.reduce (8, 4);
            for (auto& s : strips)
                s->meterArea = area.removeFromLeft (14).reduced (2, 0);
            return;
        }

        area.reduce (8, 0);
        for (auto& s : strips)
        {
            auto strip = area.removeFromLeft (stripWidth).reduced (4, 0);
            s->stripArea = strip;
            s->nameArea = strip.removeFromTop (24);
            s->valueArea = strip.removeFromBottom (28);
            strip.reduce (0, 6);
            s->meterArea = strip.removeFromLeft (16);
            strip.removeFromLeft (4);
            s->slider.setBounds (strip);
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (style == Style::setup)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText ("Master output volume (kept for every scene; also on the Stage tab)",
                        getLocalBounds().removeFromTop (22), juce::Justification::centredLeft);

            for (auto& s : strips)
            {
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (14.0f));
                g.drawText (s->name, s->nameArea, juce::Justification::centredLeft);
                drawMeter (g, *s, false);
            }
            return;
        }

        g.setColour (juce::Colour (0xff17191c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

        if (! collapsed)
        {
            g.setColour (juce::Colour (0xffb8bcc2));
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText ("Outputs", titleArea.withTrimmedLeft (12), juce::Justification::centredLeft);
        }

        for (auto& s : strips)
        {
            drawMeter (g, *s, true);
            if (collapsed)
                continue;

            g.setColour (juce::Colour (0xfff2f2f2));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawFittedText (s->name, s->nameArea, juce::Justification::centred, 1);

            const float db = (float) s->slider.getValue();
            g.setColour (db <= offDb ? juce::Colour (0xffe05050) : juce::Colour (0xffb8bcc2));
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (textFor (db), s->valueArea, juce::Justification::centred);
        }
    }

    // Minimised strips open again with a touch anywhere on them.
    void mouseUp (const juce::MouseEvent&) override
    {
        if (style == Style::stage && collapsed)
        {
            setCollapsed (false);
            if (onCollapsedChanged)
                onCollapsedChanged (false);
        }
    }

private:
    struct Strip
    {
        juce::String name;
        juce::Slider slider;
        juce::Rectangle<int> stripArea, nameArea, meterArea, valueArea;
        float level = 0.0f;
        int clipHold = 0;
    };

    static constexpr int stripWidth = 92;
    static constexpr int rowHeight = 36;
    static constexpr float floorDb = -60.0f;

    void updateVisibility()
    {
        for (auto& s : strips)
            s->slider.setVisible (style == Style::setup || ! collapsed);
    }

    void timerCallback() override
    {
        // The meters are shared by the Stage and Setup strips, and reading one resets it, so only the strips
        // on screen read them.
        if (! isShowing())
            return;

        for (size_t c = 0; c < strips.size(); ++c)
        {
            auto& s = *strips[c];
            const float p = engine.outputMeters[c].take();
            s.level = juce::jmax (p, s.level * 0.89f);   // falls back about 1 dB per tick
            if (p >= 0.89f)                              // -1 dBFS: at the outputs' limiter
                s.clipHold = 40;
            else if (s.clipHold > 0)
                --s.clipHold;
        }

        repaint();
    }

    void drawMeter (juce::Graphics& g, const Strip& s, bool vertical)
    {
        const auto bar = s.meterArea.toFloat();
        g.setColour (juce::Colour (0xff202020));
        g.fillRoundedRectangle (bar, 3.0f);

        const float db = juce::Decibels::gainToDecibels (s.level, floorDb);
        const float frac = juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
        g.setColour (s.clipHold > 0 || db > -3.0f ? juce::Colour (0xffe05050)
                                                  : db > -12.0f ? juce::Colour (0xffe0b040) : juce::Colour (0xff50b070));

        if (vertical)
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * frac), 3.0f);
        else
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 3.0f);
    }

    AudioEngine& engine;
    Style style;
    bool collapsed = false;
    juce::TextButton collapseButton;
    juce::Rectangle<int> titleArea;
    std::vector<std::unique_ptr<Strip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputStrips)
};
