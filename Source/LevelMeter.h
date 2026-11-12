#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Core/Node.h"
#include "Core/PeakMeter.h"

// A stack of peak meters in dBFS. Each row reads a PeakMeter that the audio thread fills.
// Peaks fall back at about 20 dB per second, and warnings are held for two seconds.
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter() { startTimerHz (20); }

    // `meter` is the level shown. With `preLimiter`, the row shows LIMIT when the signal reached the
    // limiter's threshold. With `warnAtFullScale`, it shows CLIP at full scale. `aimMarks` draws the
    // -12 and -6 dBFS marks that bound a good input level.
    // `owner` keeps the node alive for as long as this row reads its meter.
    void addRow (const juce::String& label, PeakMeter* meter, PeakMeter* preLimiter = nullptr,
                 bool warnAtFullScale = false, bool aimMarks = false, std::shared_ptr<Node> owner = nullptr)
    {
        Row r;
        r.owner = std::move (owner);
        r.label = label;
        r.meter = meter;
        r.pre = preLimiter;
        r.warnFull = warnAtFullScale;
        r.aim = aimMarks;
        rows.push_back (r);
    }

    int getRequiredHeight() const { return (int) rows.size() * 30 + 26; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        for (const auto& r : rows)
        {
            drawRow (g, area.removeFromTop (26.0f), r);
            area.removeFromTop (4.0f);
        }

        g.setColour (juce::Colours::grey);
        g.setFont (12.0f);
        g.drawText ("Aim for input peaks of about -12 to -6 dBFS. LIMIT means an output has reached its limiter.",
                    area, juce::Justification::centredLeft);
    }

private:
    struct Row
    {
        juce::String label;
        std::shared_ptr<Node> owner;
        PeakMeter* meter = nullptr;
        PeakMeter* pre = nullptr;
        bool warnFull = false;
        bool aim = false;
        float level = 0.0f;
        int flagHold = 0;
    };

    static constexpr float floorDb = -60.0f;

    void timerCallback() override
    {
        for (auto& r : rows)
        {
            const float p = r.meter->take();
            r.level = juce::jmax (p, r.level * 0.89f);   // about 1 dB per tick

            bool warn = false;
            if (r.pre != nullptr)
                warn = r.pre->take() >= 0.89f;            // -1 dBFS, the limiter's threshold
            else if (r.warnFull)
                warn = p >= 0.99f;

            if (warn)
                r.flagHold = 40;
            else if (r.flagHold > 0)
                --r.flagHold;
        }

        repaint();
    }

    void drawRow (juce::Graphics& g, juce::Rectangle<float> area, const Row& r)
    {
        g.setColour (juce::Colours::white);
        g.setFont (13.0f);
        g.drawText (r.label, area.removeFromLeft (140.0f), juce::Justification::centredLeft);

        const float db = juce::Decibels::gainToDecibels (r.level, floorDb);
        g.drawText (r.level > 0.0f ? juce::String (db, 1) + " dBFS" : juce::String ("-inf"),
                    area.removeFromRight (84.0f), juce::Justification::centredRight);

        const auto bar = area.reduced (0.0f, 5.0f);
        g.setColour (juce::Colour (0xff202020));
        g.fillRoundedRectangle (bar, 3.0f);

        const float frac = juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
        g.setColour (db > -3.0f ? juce::Colour (0xffe05050)
                                : db > -12.0f ? juce::Colour (0xffe0b040)
                                              : juce::Colour (0xff50b070));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 3.0f);

        if (r.aim)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            for (const float mark : { -12.0f, -6.0f })
                g.drawVerticalLine ((int) (bar.getX() + bar.getWidth() * (mark - floorDb) / -floorDb), bar.getY(), bar.getBottom());
        }

        if (r.flagHold > 0)
        {
            g.setColour (juce::Colour (0xffe05050));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (r.pre != nullptr ? "LIMIT" : "CLIP", bar.reduced (6.0f, 0.0f), juce::Justification::centredRight);
        }
    }

    std::vector<Row> rows;
};
