#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <string>

// A plugin's own window (its editor, or a plain list of controls if it has none). It keeps the plugin
// alive while it is open, so removing the node never leaves the window pointing at nothing.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (std::string node, std::shared_ptr<juce::AudioPluginInstance> pluginInstance,
                  const juce::String& title, std::function<void()> closed)
        : juce::DocumentWindow (title, juce::Colour (0xff1b1d20), juce::DocumentWindow::closeButton),
          nodeId_ (std::move (node)), plugin (std::move (pluginInstance)), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);

        if (plugin->hasEditor())
            editor.reset (plugin->createEditorIfNeeded());
        if (editor == nullptr)
            editor = std::make_unique<juce::GenericAudioProcessorEditor> (*plugin);

        setContentNonOwned (editor.get(), true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginWindow() override
    {
        clearContentComponent();
        editor.reset();   // before the plugin it belongs to
    }

    const std::string& nodeId() const noexcept { return nodeId_; }
    const juce::AudioPluginInstance* instance() const noexcept { return plugin.get(); }

    void closeButtonPressed() override
    {
        if (onClosed)
            onClosed();
    }

private:
    std::string nodeId_;
    std::shared_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<void()> onClosed;
};
