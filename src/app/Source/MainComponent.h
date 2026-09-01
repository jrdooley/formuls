/*
 * MainComponent.h
 *
 * The contents of the main formuls window:
 *
 *   - a combo box listing the available audio output devices,
 *   - a combo box for the number of output channels (2 = stereo master mix,
 *     14 = individual synth/bus outputs -- see kChannelChoices in
 *     MainComponent.cpp to add other counts),
 *   - a combo box for the sample rate (see kSampleRateChoices in
 *     MainComponent.cpp; 48 kHz default). If the chosen device cannot run
 *     at the chosen rate, the engine opens it at the nearest rate the
 *     device supports and says so in the status line,
 *   - a Start/Stop button that boots (or shuts down) the formuls engine
 *     (libpd, see FormulsEngine.h) and the Open Stage Control server
 *     (see OpenStageControlProcess.h) together,
 *   - a read-only panel listing the web addresses the control GUI can be
 *     opened at once it is running: the loopback address for a browser on
 *     this machine, plus this machine's address on each network it is
 *     attached to, for a tablet or phone on the same network. The text can
 *     be selected and copied, but not edited.
 *   - a performance readout (DSP load, peak, whole-process CPU, and any
 *     missed audio deadlines), so this build can be compared like for like
 *     against the à la carte build -- both use the same measurement code
 *     from src/shared/PerformanceMeter.h.
 *   - a status line reporting what is going on.
 *
 * Stopping keeps the app (and this window) alive so a different device or
 * channel count can be chosen and the engine started again.
 *
 * Layout metrics and all colours/fonts live in FormulsLookAndFeel.h.
 */

#pragma once

#include <JuceHeader.h>
#include "FormulsEngine.h"
#include "OpenStageControlProcess.h"

namespace formuls
{

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Clicking the performance readout clears its peak / missed counters. */
    void mouseDown (const juce::MouseEvent& event) override;

private:
    void populateDeviceList();
    void startStopClicked();
    void startEverything();
    void stopEverything();
    void setStatus (const juce::String& message);

    /** Fills the address panel: the GUI's web addresses when the server is
        running, or a short hint when it is not. */
    void updateAddressPanel (bool guiIsRunning);

    /** juce::Timer -- refreshes the performance readout twice a second. */
    void timerCallback() override;
    void updateMeterLabel();

    juce::AudioDeviceManager deviceManager;
    FormulsEngine engine;
    OpenStageControlProcess openStageControl;

    juce::ComboBox audioDeviceBox;
    juce::ComboBox channelsBox;
    juce::ComboBox sampleRateBox;
    juce::TextButton startStopButton;
    juce::TextEditor addressPanel;
    juce::Label meterLabel;
    juce::Label statusLabel;

    juce::StringArray outputDeviceNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace formuls
