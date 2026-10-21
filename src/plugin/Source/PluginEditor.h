/*
 * PluginEditor.h
 *
 * The plugin's window. formuls is played from its web GUI (on a tablet or
 * in a browser), so this only says where that GUI is, and opens it here.
 */

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace formuls
{

class FormulsPluginEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit FormulsPluginEditor (FormulsPluginProcessor&);
    ~FormulsPluginEditor() override   { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;   // follows the engine starting / the port changing

    FormulsPluginProcessor& processor;
    juce::Label title, status, addresses;
    juce::TextButton openButton { "Open the control GUI in a browser" };
    juce::StringArray shownAddresses;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormulsPluginEditor)
};

} // namespace formuls
