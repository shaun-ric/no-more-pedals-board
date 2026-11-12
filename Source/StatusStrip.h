#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A thin strip along the bottom of the window, on every tab: where you are, how hard the machine is
// working, and the latest warning or MIDI message. It turns amber for a warning and red for a problem.
class StatusStrip : public juce::Component
{
public:
    enum class Level { Ok, Warning, Problem };

    void set (const juce::String& leftText, const juce::String& middleText, const juce::String& rightText, Level newLevel)
    {
        if (leftText == left && middleText == middle && rightText == right && newLevel == level)
            return;

        left = leftText;
        middle = middleText;
        right = rightText;
        level = newLevel;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour background = level == Level::Problem ? juce::Colour (0xff5a1f1f)
                                      : level == Level::Warning ? juce::Colour (0xff5a4515)
                                                                : juce::Colour (0xff1f3a2a);
        g.setColour (background);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (juce::FontOptions (14.0f));

        auto area = getLocalBounds().reduced (10, 0);
        const int third = area.getWidth() / 3;
        g.drawText (left, area.removeFromLeft (third), juce::Justification::centredLeft, true);
        g.drawText (middle, area.removeFromLeft (third), juce::Justification::centred, true);
        g.drawText (right, area, juce::Justification::centredRight, true);
    }

private:
    juce::String left, middle, right;
    Level level = Level::Ok;
};
