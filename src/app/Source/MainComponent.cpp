/*
 * MainComponent.cpp -- see MainComponent.h for an overview.
 */

#include "MainComponent.h"
#include "FormulsLookAndFeel.h"
#include "ResourceLocator.h"

namespace formuls
{

// The channel counts offered in the "Channels" combo box, matching the old
// Python front end: 2 = stereo master mix, 14 = each synth voice and the
// master bus on its own output pair. Add entries here to offer other counts.
static const int kChannelChoices[] = { 2, 14 };

// The sample rates offered in the "Sample rate" combo box. 48 kHz is the
// default (the rate the patch was designed at). If a device cannot run at
// the chosen rate the engine falls back to what the device supports and
// libpd follows the device, so any entry added here is safe.
static const int kSampleRateChoices[] = { 44100, 48000, 88200, 96000 };
static constexpr int kDefaultSampleRate = 48000;

static const juce::String kStartText { "Start formuls" };
static const juce::String kStopText  { "Stop formuls" };

MainComponent::MainComponent()
{
    setSize (style::windowWidth, style::windowHeight);

    // ------------------------------------------------- audio device combo box
    audioDeviceBox.setTextWhenNothingSelected ("Select audio output...");
    populateDeviceList();
    addAndMakeVisible (audioDeviceBox);

    // ------------------------------------------------------ channels combo box
    for (int i = 0; i < (int) std::size (kChannelChoices); ++i)
        channelsBox.addItem (juce::String (kChannelChoices[i]) + " channels",
                             kChannelChoices[i]);   // item id == channel count

    channelsBox.setSelectedId (kChannelChoices[0], juce::dontSendNotification);
    addAndMakeVisible (channelsBox);

    // --------------------------------------------------- sample rate combo box
    for (auto rate : kSampleRateChoices)
        sampleRateBox.addItem (juce::String (rate) + " Hz", rate);   // id == rate

    sampleRateBox.setSelectedId (kDefaultSampleRate, juce::dontSendNotification);
    addAndMakeVisible (sampleRateBox);

    // -------------------------------------------------------- start/stop button
    startStopButton.setButtonText (kStartText);
    startStopButton.onClick = [this] { startStopClicked(); };
    addAndMakeVisible (startStopButton);

    // ------------------------------------------------------- GUI address panel
    // Read-only, but still selectable so the address can be copied out
    // (Cmd+C, or right-click for the Copy menu).
    addressPanel.setMultiLine (true, false);
    addressPanel.setReadOnly (true);
    addressPanel.setCaretVisible (false);
    addressPanel.setScrollbarsShown (true);
    addressPanel.setPopupMenuEnabled (true);
    addressPanel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                             style::addressFontHeight,
                                             juce::Font::plain));
    addAndMakeVisible (addressPanel);
    updateAddressPanel (false);

    // -------------------------------------------------- performance readout
    meterLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                           style::meterFontHeight,
                                           juce::Font::plain));
    meterLabel.setColour (juce::Label::textColourId, style::meterColour);
    meterLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (meterLabel);
    updateMeterLabel();

    // --------------------------------------------------------------- status line
    statusLabel.setFont (juce::FontOptions (style::labelFontHeight));
    statusLabel.setColour (juce::Label::textColourId, style::statusColour);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
    setStatus ("Ready.");

    // The Pd patch can ask the whole app to quit (see FormulsEngine.h).
    engine.onQuitRequested = []
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    };

   #if FORMULS_AUTOSTART_TEST
    // Development aid: build with GCC_PREPROCESSOR_DEFINITIONS including
    // FORMULS_AUTOSTART_TEST=1 to select the first output device and start
    // the engine automatically one second after launch. Not compiled into
    // normal builds.
    juce::Timer::callAfterDelay (1000, [safeThis = juce::Component::SafePointer (this)]
    {
        if (safeThis != nullptr && ! safeThis->engine.isRunning())
        {
            safeThis->audioDeviceBox.setSelectedId (1);

            // FORMULS_TEST_SAMPLERATE overrides the sample rate choice, so
            // non-default rates can be tested without clicking.
            if (auto rate = juce::SystemStats::getEnvironmentVariable (
                                "FORMULS_TEST_SAMPLERATE", {}).getIntValue();
                rate > 0)
                safeThis->sampleRateBox.setSelectedId (rate);

            safeThis->startStopClicked();

            juce::Logger::writeToLog ("GUI addresses:\n"
                                      + safeThis->addressPanel.getText());
            juce::Logger::writeToLog ("status: "
                                      + safeThis->statusLabel.getText());
        }
    });

    // If FORMULS_SNAPSHOT_PATH is set, write a PNG of the window to it a few
    // seconds after launch. Lets the layout be checked without needing screen
    // recording permission.
    if (auto path = juce::SystemStats::getEnvironmentVariable ("FORMULS_SNAPSHOT_PATH", {});
        path.isNotEmpty())
    {
        juce::Timer::callAfterDelay (4000, [safeThis = juce::Component::SafePointer (this), path]
        {
            if (safeThis == nullptr)
                return;

            auto image = safeThis->createComponentSnapshot (safeThis->getLocalBounds());
            juce::File file (path);
            file.deleteFile();   // FileOutputStream appends, so start clean

            if (juce::FileOutputStream stream (file); stream.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, stream);
                juce::Logger::writeToLog ("wrote snapshot to " + file.getFullPathName());
            }
        });
    }
   #endif
}

MainComponent::~MainComponent()
{
    stopEverything();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (style::margin);

    audioDeviceBox.setBounds (area.removeFromTop (style::controlHeight)
                                  .withWidth (style::comboWidth));
    area.removeFromTop (style::controlSpacing);

    channelsBox.setBounds (area.removeFromTop (style::controlHeight)
                               .withWidth (style::comboWidth / 2));
    area.removeFromTop (style::controlSpacing);

    sampleRateBox.setBounds (area.removeFromTop (style::controlHeight)
                                 .withWidth (style::comboWidth / 2));
    area.removeFromTop (style::controlSpacing);

    startStopButton.setBounds (area.removeFromTop (style::buttonHeight)
                                   .withWidth (style::buttonWidth));
    area.removeFromTop (style::controlSpacing);

    // status line and the performance readout sit at the bottom;
    // the address panel fills whatever is left
    statusLabel.setBounds (area.removeFromBottom (style::controlHeight));
    meterLabel.setBounds (area.removeFromBottom (style::controlHeight - 8));
    addressPanel.setBounds (area.withTrimmedBottom (style::controlSpacing));
}

//==============================================================================
void MainComponent::populateDeviceList()
{
    audioDeviceBox.clear (juce::dontSendNotification);
    outputDeviceNames.clear();

    // Collect output-capable devices from every available device type
    // (on macOS this is just CoreAudio; on Linux it may be ALSA and JACK).
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        outputDeviceNames.addArray (type->getDeviceNames (false));  // false = outputs
    }

    outputDeviceNames.removeDuplicates (false);

    for (int i = 0; i < outputDeviceNames.size(); ++i)
        audioDeviceBox.addItem (outputDeviceNames[i], i + 1);
}

void MainComponent::startStopClicked()
{
    if (engine.isRunning())
        stopEverything();
    else
        startEverything();
}

void MainComponent::startEverything()
{
    const auto deviceIndex = audioDeviceBox.getSelectedId() - 1;

    if (deviceIndex < 0)
    {
        setStatus ("Please select an audio output device first.");
        return;
    }

    const auto resourceRoot = findResourceRoot();

    if (! resourceRoot.isDirectory())
    {
        setStatus ("Could not locate the formuls resources (pd/_main.pd).");
        return;
    }

    const auto numChannels = channelsBox.getSelectedId();
    const auto requestedRate = sampleRateBox.getSelectedId();

    // 1. the audio engine (libpd running _main.pd)
    if (auto result = engine.start (deviceManager, outputDeviceNames[deviceIndex],
                                    numChannels, requestedRate, resourceRoot);
        result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    // 2. the Open Stage Control GUI server
    auto oscResult = openStageControl.start (resourceRoot);

    startStopButton.setButtonText (kStopText);

    // While running, lock the settings to what the engine uses.
    audioDeviceBox.setEnabled (false);
    channelsBox.setEnabled (false);
    sampleRateBox.setEnabled (false);

    updateAddressPanel (oscResult.wasOk());
    startTimerHz (2);           // refresh the performance readout

    // If the device couldn't do the requested rate, say what it's really at.
    const auto actualRate = (int) engine.getActualSampleRate();
    const auto rateNote = actualRate != requestedRate
        ? " Device runs at " + juce::String (actualRate) + " Hz (not "
              + juce::String (requestedRate) + " Hz)."
        : juce::String();

    if (oscResult.failed())
        setStatus ("Engine running. GUI failed: " + oscResult.getErrorMessage()
                   + rateNote);
    else
        setStatus ("Running." + rateNote);
}

void MainComponent::stopEverything()
{
    openStageControl.stop();
    engine.stop();

    stopTimer();
    updateMeterLabel();

    startStopButton.setButtonText (kStartText);
    audioDeviceBox.setEnabled (true);
    channelsBox.setEnabled (true);
    sampleRateBox.setEnabled (true);
    updateAddressPanel (false);
    setStatus ("Stopped.");
}

void MainComponent::setStatus (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
}

void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    if (meterLabel.getBounds().contains (event.getPosition()))
    {
        engine.getMeter().resetPeak();
        updateMeterLabel();
    }
}

void MainComponent::timerCallback()
{
    updateMeterLabel();
}

void MainComponent::updateMeterLabel()
{
    if (! engine.isRunning())
    {
        meterLabel.setText ("DSP --   CPU --", juce::dontSendNotification);
        return;
    }

    auto& meter = engine.getMeter();

    // "DSP" is the audio callback measured against its own deadline -- the
    // number that predicts dropouts. "CPU" is the whole process as a share
    // of one core, i.e. the Activity Monitor number. See PerformanceMeter.h.
    juce::String text;
    text << "DSP " << juce::String (meter.dspLoad(), 1) << "%"
         << "  peak " << juce::String (meter.dspPeak(), 1) << "%"
         << "   CPU " << juce::String (meter.processCpu(), 1) << "%";

    if (auto missed = meter.overloadCount(); missed > 0)
        text << "   missed " << juce::String (missed);

    meterLabel.setText (text, juce::dontSendNotification);
}

void MainComponent::updateAddressPanel (bool guiIsRunning)
{
    if (! guiIsRunning)
    {
        addressPanel.setText ("The control GUI's web addresses will be listed here\n"
                              "once formuls is running.",
                              juce::dontSendNotification);
        return;
    }

    // getBrowserAddresses() always returns the loopback address first,
    // followed by this machine's address on each attached network.
    auto addresses = OpenStageControlProcess::getBrowserAddresses();

    juce::StringArray lines { "Open the control GUI in a web browser:",
                              "",
                              "On this machine:",
                              "   " + addresses[0],
                              "",
                              "On a tablet or phone on the same network:" };

    if (addresses.size() > 1)
    {
        for (int i = 1; i < addresses.size(); ++i)
            lines.add ("   " + addresses[i]);
    }
    else
    {
        lines.add ("   (no network connection found -- connect this");
        lines.add ("    machine to a network and press Stop, then Start)");
    }

    addressPanel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

} // namespace formuls
