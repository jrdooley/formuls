/*
 * PluginEditor.cpp -- see PluginEditor.h.
 */

#include "PluginEditor.h"

namespace formuls
{

FormulsPluginEditor::FormulsPluginEditor (FormulsPluginProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    title.setText ("formuls", juce::dontSendNotification);
    title.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);

    for (auto* label : { &status, &addresses })
    {
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        label->setJustificationType (juce::Justification::topLeft);
    }
    status.setFont (juce::FontOptions (15.0f));
    addresses.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));

    openButton.onClick = [this]
    {
        if (! shownAddresses.isEmpty())
            juce::URL (shownAddresses[0]).launchInDefaultBrowser();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &status, &addresses, &openButton })
        addAndMakeVisible (c);

    setSize (520, 300);
    timerCallback();
    startTimer (1000);
}

void FormulsPluginEditor::timerCallback()
{
    status.setText (processor.getStatus(), juce::dontSendNotification);

    const auto now = processor.getGuiAddresses();
    if (now == shownAddresses && ! addresses.getText().isEmpty())
        return;

    shownAddresses = now;
    juce::String text;

    if (now.isEmpty())
        text = "The control GUI starts with audio.";
    else
    {
        text << "On this machine:\n   " << now[0] << "\n";
        if (now.size() > 1)
        {
            text << "\nOn a tablet or phone on the same network:\n";
            for (int i = 1; i < now.size(); ++i)
                text << "   " << now[i] << "\n";
        }
    }

    addresses.setText (text, juce::dontSendNotification);
    openButton.setEnabled (! now.isEmpty());
}

void FormulsPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f22));
}

void FormulsPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    title.setBounds (area.removeFromTop (40));
    status.setBounds (area.removeFromTop (44));
    openButton.setBounds (area.removeFromBottom (32));
    area.removeFromBottom (10);
    addresses.setBounds (area);
}

} // namespace formuls
