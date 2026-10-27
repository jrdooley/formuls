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

static const juce::String kRecordText     { "Record" };
static const juce::String kStopRecordText { "Stop recording" };

// A preset save is confirmed by watching for the file to appear, because
// nothing else reports back. 200 ms x 15 gives o-s-c three seconds to write
// it, against the ~1 s a full 220 kB state file took when measured.
static constexpr int kPresetPollIntervalMs = 200;
static constexpr int kPresetSaveAttempts   = 15;

MainComponent::MainComponent()
{
    setSize (style::windowWidth, style::windowHeight);

    // ------------------------------------------------------------------ title
    titleLabel.setText ("formuls", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (style::titleFontHeight, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    // ------------------------------------------------------- MIDI keyboards
    enableAllMidiInputs();
    deviceManager.addMidiInputDeviceCallback ({}, this);   // {} = every enabled input
    midiDeviceListConnection = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });

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

    // ------------------------------------------------------------ record button
    // Every block the engine renders is offered to the recorder from the
    // audio thread; the recorder ignores them until a take is started.
    engine.setRecorder (&recorder);

    recordButton.onClick = [this] { recordClicked(); };
    addAndMakeVisible (recordButton);
    updateButtonStates();

    // Take Screenshot, Save Preset and Load Preset are not shown: they were
    // added in haste and how they should work has not been settled. Their
    // code is kept, but with the buttons never on screen nothing reaches it.
    screenshotButton.onClick = [this] { screenshotClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };
    loadPresetButton.onClick = [this] { loadPresetClicked(); };

    // ----------------------------------------------------- replay (uxlog builds)
    replayAvailable = OpenStageControlProcess::canReplay (findResourceRoot());
    replayButton.setButtonText ("Replay Session...");
    replayButton.onClick = [this] { replayClicked(); };
    addChildComponent (replayButton);
    replayButton.setVisible (replayAvailable);

    playPauseButton.setButtonText ("Play");
    playPauseButton.onClick = [this]
    {
        replayer.setPlaying (! replayer.isPlaying());
        playPauseButton.setButtonText (replayer.isPlaying() ? "Pause" : "Play");
    };
    addChildComponent (playPauseButton);

    positionSlider.onValueChange = [this] { replayer.setPositionMs (positionSlider.getValue()); };
    addChildComponent (positionSlider);

    replayTimeLabel.setFont (juce::FontOptions (style::labelFontHeight));
    replayTimeLabel.setColour (juce::Label::textColourId, style::statusColour);
    replayTimeLabel.setJustificationType (juce::Justification::centredRight);
    addChildComponent (replayTimeLabel);

    // -------------------------------------------------------------- VU meters
    vuMeter.setPeakLevels (engine.peakLevels.data());
    addAndMakeVisible (vuMeter);

    // ------------------------------------------------------- GUI address panel
    // Read-only, but still selectable so the address can be copied out
    // (Cmd+C, or right-click for the Copy menu).
    addressPanel.setMultiLine (true, true);   // wrapped: no sideways scrolling
    addressPanel.setReadOnly (true);
    addressPanel.setCaretVisible (false);
    addressPanel.setScrollbarsShown (true);
    addressPanel.setPopupMenuEnabled (true);
    addressPanel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                             style::addressFontHeight,
                                             juce::Font::plain));
    addAndMakeVisible (addressPanel);
    updateAddressPanel (false);

    // --------------------------------------------------------------- status line
    statusLabel.setFont (juce::FontOptions (style::labelFontHeight));
    statusLabel.setColour (juce::Label::textColourId, style::statusColour);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
    setStatus ("Ready.");

    // A run that crashed or was force-quit never got to stop its Open Stage
    // Control server, and that server still holds tcp port 9001 -- which is
    // what makes the *next* launch fail. No destructor can cover that case,
    // so the cleanup has to happen here, on the way up, before the engine or
    // a new server is started. See OpenStageControlProcess.h.
    if (const auto root = findResourceRoot(); root.isDirectory())
    {
        if (const auto swept = OpenStageControlProcess::killStrayServers (root); swept > 0)
            setStatus ("Ready. Cleared " + juce::String (swept)
                       + (swept == 1 ? " leftover GUI server." : " leftover GUI servers."));
    }

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
            // FORMULS_TEST_SAMPLERATE / FORMULS_TEST_CHANNELS override the
            // combo boxes, so non-default rates and channel counts can be
            // tested without clicking.
            if (auto rate = juce::SystemStats::getEnvironmentVariable (
                                "FORMULS_TEST_SAMPLERATE", {}).getIntValue();
                rate > 0)
                safeThis->sampleRateBox.setSelectedId (rate);

            if (auto channels = juce::SystemStats::getEnvironmentVariable (
                                    "FORMULS_TEST_CHANNELS", {}).getIntValue();
                channels > 0)
                safeThis->channelsBox.setSelectedId (channels);

            safeThis->startOnLaunch();

            // FORMULS_TEST_RECORD_SECONDS records for that long and finishes
            // the file without a save dialog, so the recorder can be checked
            // end to end from a script.
            if (auto seconds = juce::SystemStats::getEnvironmentVariable (
                                   "FORMULS_TEST_RECORD_SECONDS", {}).getFloatValue();
                seconds > 0.0f && safeThis->engine.isRunning())
            {
                safeThis->recordClicked();

                juce::Timer::callAfterDelay ((int) (seconds * 1000.0f), [safeThis]
                {
                    if (safeThis == nullptr || ! safeThis->recorder.isRecording())
                        return;

                    safeThis->finishRecording (false);
                    safeThis->updateButtonStates();
                    juce::Logger::writeToLog ("status: "
                                              + safeThis->statusLabel.getText());
                });
            }
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
    deviceManager.removeMidiInputDeviceCallback ({}, this);

    // The app is closing, so there is nowhere to show a save dialog: any
    // take in progress is simply finished where it was being recorded.
    stopReplay();
    stopEverything (false);
    engine.setRecorder (nullptr);
}

//==============================================================================
void MainComponent::enableAllMidiInputs()
{
    for (const auto& input : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (input.identifier))
        {
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
            juce::Logger::writeToLog ("MIDI input: " + input.name);
        }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI thread. The engine queues the note for its next audio tick; notes
    // that arrive while it is stopped are cleared when it starts.
    if (message.isNoteOn())   // velocity 0 counts as a note-off
        engine.queueNoteOn (message.getChannel() - 1, message.getNoteNumber(), message.getVelocity());
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (style::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (style::margin);

    titleLabel.setBounds (area.removeFromTop (style::titleHeight));
    area.removeFromTop (style::controlSpacing / 2);

    audioDeviceBox.setBounds (area.removeFromTop (style::controlHeight)
                                  .withWidth (style::comboWidth));
    area.removeFromTop (style::controlSpacing);

    channelsBox.setBounds (area.removeFromTop (style::controlHeight)
                               .withWidth (style::comboWidth / 2));
    area.removeFromTop (style::controlSpacing);

    sampleRateBox.setBounds (area.removeFromTop (style::controlHeight)
                                 .withWidth (style::comboWidth / 2));
    area.removeFromTop (style::controlSpacing);

    // Replay Session... (uxlog builds only) sits right-aligned above the
    // record button, at its size.
    if (replayAvailable)
    {
        auto row = area.removeFromTop (style::buttonHeight);
        replayButton.setBounds (row.removeFromRight (style::recordButtonWidth));
        area.removeFromTop (style::controlSpacing / 2);
    }

    auto buttonRow = area.removeFromTop (style::buttonHeight);
    startStopButton.setBounds (buttonRow.removeFromLeft (style::buttonWidth));
    recordButton.setBounds (buttonRow.removeFromRight (style::recordButtonWidth));
    area.removeFromTop (style::controlSpacing);

    if (replayer.isActive())
    {
        auto transportRow = area.removeFromTop (style::controlHeight);
        playPauseButton.setBounds (transportRow.removeFromLeft (style::buttonWidth / 2));
        replayTimeLabel.setBounds (transportRow.removeFromRight (150));
        positionSlider.setBounds (transportRow.reduced (8, 0));
        area.removeFromTop (style::controlSpacing);
    }

    // stereo VU meters, same width as the address panel
    const int meterGap = 4;
    const int metersTotalHeight = style::meterHeight * 2 + meterGap;
    auto meterArea = area.removeFromTop (metersTotalHeight);
    vuMeter.setBounds (meterArea);
    area.removeFromTop (style::controlSpacing);

    // status line sits at the bottom; the address panel fills what is left
    statusLabel.setBounds (area.removeFromBottom (style::controlHeight));
    addressPanel.setBounds (area.withTrimmedBottom (style::controlSpacing));
}

//==============================================================================
void MainComponent::populateDeviceList()
{
    audioDeviceBox.clear (juce::dontSendNotification);
    outputDeviceNames.clear();
    outputDeviceTypes.clear();

    // Collect output-capable devices from every available device type
    // (on macOS this is just CoreAudio; on Linux it may be ALSA and JACK).
    //
    // Each name is stored with the type it came from, and the two arrays are
    // kept index-aligned. AudioDeviceManager resolves a device name against
    // the CURRENT type only, so without the type a name belonging to another
    // one could never be opened -- and because initialise() is called with
    // selectDefaultDeviceOnFailure, it would quietly open the default device
    // instead and still report success.
    // The device the first type calls its default -- on macOS, whatever is
    // chosen in Sound settings. Taken from the first type that offers one so
    // that a machine with several (ALSA and JACK, say) still gets an answer.
    juce::String systemDefaultName;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();

        const auto names = type->getDeviceNames (false);        // false = outputs

        if (systemDefaultName.isEmpty())
        {
            const int defaultIndex = type->getDefaultDeviceIndex (false);

            if (juce::isPositiveAndBelow (defaultIndex, names.size()))
                systemDefaultName = names[defaultIndex];
        }

        for (const auto& name : names)
        {
            if (outputDeviceNames.contains (name))
                continue;               // same device seen through an earlier type

            outputDeviceNames.add (name);
            outputDeviceTypes.add (type->getTypeName());
        }
    }

    for (int i = 0; i < outputDeviceNames.size(); ++i)
        audioDeviceBox.addItem (outputDeviceNames[i], i + 1);

    // Open on the system default rather than making the user choose before
    // anything will start. If it could not be identified the box stays empty
    // and shows its "Select audio output..." placeholder, as before.
    const int defaultRow = outputDeviceNames.indexOf (systemDefaultName);

    if (defaultRow >= 0)
        audioDeviceBox.setSelectedId (defaultRow + 1, juce::dontSendNotification);
}

void MainComponent::startOnLaunch()
{
    if (engine.isRunning())
        return;

    // Only fall back to the first device if populateDeviceList() could not
    // work out the system default, so this starts on the same device a user
    // would see selected.
    if (audioDeviceBox.getSelectedId() <= 0 && audioDeviceBox.getNumItems() > 0)
        audioDeviceBox.setSelectedId (1, juce::dontSendNotification);

    startEverything();

    // Nobody may be looking at the window (e.g. under xvfb-run), so report
    // the outcome on the console too.
    juce::Logger::writeToLog ("GUI addresses:\n" + addressPanel.getText());
    juce::Logger::writeToLog ("status: " + statusLabel.getText());

    // A start that failed leaves the app sitting at the window with no engine
    // and no GUI server -- invisible when there is no window to look at. The
    // usual cause on a headless box is no audio device at all (the status
    // line then reads "Please select an audio output device first"), but a
    // device that will not open reports the same way. Exit non-zero so
    // whatever launched this -- a service manager, a script -- sees the
    // failure and can restart or report it, rather than treating a dead app
    // as a running one.
    if (! engine.isRunning())
    {
        std::fprintf (stderr, "formuls: --start failed: %s\n",
                      statusLabel.getText().toRawUTF8());

        if (auto* app = juce::JUCEApplication::getInstance())
        {
            app->setApplicationReturnValue (1);
            app->quit();
        }
    }
}

void MainComponent::startStopClicked()
{
    if (engine.isRunning())
        stopEverything (true);
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

    // 1. the audio engine (libpd running _main.pd). With the embedded GUI the
    //    patch's GUI-bound OSC goes straight to the server, not to udp 9001.
    if (useNodeGui)
        engine.setDirectGui (nullptr);
    else
        engine.setDirectGui ([this] (const juce::uint8* data, int size)
                             { embeddedGui.receivePatchPacket (data, size); });

    if (auto result = engine.start (deviceManager, outputDeviceNames[deviceIndex],
                                    outputDeviceTypes[deviceIndex],
                                    numChannels, requestedRate, resourceRoot);
        result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    // 2. the Open Stage Control GUI server
    auto oscResult = useNodeGui
                         ? openStageControl.start (resourceRoot)
                         : embeddedGui.start (resourceRoot, OpenStageControlProcess::guiPort,
                                              "127.0.0.1:" + juce::String (OpenStageControlProcess::patchOscPort),
                                              [this] (const juce::OSCMessage& m) { engine.sendToPatch (m); });

    startStopButton.setButtonText (kStopText);

    // While running, lock the settings to what the engine uses.
    audioDeviceBox.setEnabled (false);
    channelsBox.setEnabled (false);
    sampleRateBox.setEnabled (false);

    updateAddressPanel (oscResult.wasOk());
    updateButtonStates();

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

    if (openStageControl.isUxLogging() && oscResult.wasOk())
        startSessionRecording();
}

void MainComponent::startSessionRecording()
{
    // A UX-logging build records the whole session, so the sound can be lined
    // up with the gesture log. Ogg Vorbis keeps a 4-hour take near 400 MB.
    const auto folder = OpenStageControlProcess::getUxLogDirectory();
    folder.createDirectory();
    const auto destination = AudioRecorder::makeRecordingFile (folder, "uxaudio", "ogg");
    const auto numChannels = engine.getNumOutputChannels();
    const auto sampleRate  = engine.getActualSampleRate();

    if (auto result = recorder.start (destination, numChannels, sampleRate,
                                      AudioRecorder::Format::oggVorbis);
        result.failed())
    {
        setStatus ("UX logging on, but session audio failed: " + result.getErrorMessage());
        return;
    }

    updateButtonStates();
    setStatus ("Running. UX logging and recording this session to "
               + destination.getFullPathName());

    startSpectralLogging (numChannels, sampleRate);

    // The first block's time is only known once the audio thread has handed
    // one over, so report the start a moment later. The log server may also
    // still be loading the layout; o-s-c handles OSC once it is listening.
    juce::Timer::callAfterDelay (1500, [safeThis = juce::Component::SafePointer (this),
                                        destination, numChannels, sampleRate]
    {
        if (safeThis == nullptr || safeThis->recorder.getFile() != destination)
            return;

        auto* ev = new juce::DynamicObject();
        ev->setProperty ("k", "rec-start");
        ev->setProperty ("file", destination.getFileName());
        ev->setProperty ("first_block_st", safeThis->recorder.getFirstBlockTimeMs());
        ev->setProperty ("sr", sampleRate);
        ev->setProperty ("ch", numChannels);
        if (auto* device = safeThis->deviceManager.getCurrentAudioDevice())
            ev->setProperty ("buffer", device->getCurrentBufferSizeSamples());
        safeThis->sendUxLogEvent (juce::var (ev));
    });
}

void MainComponent::startSpectralLogging (int numChannels, double sampleRate)
{
    // Spectral descriptors are logged rather than recorded: 14 channels of
    // audio would be about 700 MB an hour, these frames about 15 MB. With the
    // channel count set to 14 each entry is one synth or bus; with 2 it is the
    // master mix. See SpectralAnalyser.h and docs/uxlog-spectral/README.md.
    const auto requested = juce::SystemStats::getEnvironmentVariable ("FORMULS_UXLOG_SPEC_HZ", {})
                               .getIntValue();
    const auto hz = juce::jlimit (1, 50, requested > 0 ? requested : 5);

    spectral.prepare (numChannels, sampleRate);
    engine.setSpectralAnalyser (&spectral);
    spectralTick.startTimerHz (hz);

    auto* ev = new juce::DynamicObject();
    ev->setProperty ("k", "spec-start");
    ev->setProperty ("hz", hz);
    ev->setProperty ("n", SpectralAnalyser::windowSize);
    ev->setProperty ("ch", numChannels);
    ev->setProperty ("sr", sampleRate);
    sendUxLogEvent (juce::var (ev));
}

void MainComponent::stopSpectralLogging()
{
    spectralTick.stopTimer();
    engine.setSpectralAnalyser (nullptr);
    spectral.reset();
}

void MainComponent::spectralTimerCallback()
{
    if (auto frame = spectral.takeFrame(); ! frame.isVoid())
        sendUxLogEvent (frame);
}

void MainComponent::sendUxLogEvent (const juce::var& event)
{
    // formuls-uxlog-module.js takes this address out of the OSC stream,
    // stamps it with its own clock and writes it into the session log.
    sendOscToOsc ("/formuls/uxlog/event", juce::JSON::toString (event, true));
}

void MainComponent::stopEverything (bool offerToSaveRecording)
{
    // Finish the take before the audio device goes away, so the tail of the
    // recording isn't cut off by the engine shutting down.
    const bool wasRecording = recorder.isRecording();

    if (wasRecording && openStageControl.isUxLogging())
    {
        // Sent while the log server still runs. No save dialog: the session
        // audio belongs next to its log.
        auto* ev = new juce::DynamicObject();
        ev->setProperty ("k", "rec-stop");
        ev->setProperty ("file", recorder.getFile().getFileName());
        ev->setProperty ("samples", recorder.getSamplesWritten());
        ev->setProperty ("first_block_st", recorder.getFirstBlockTimeMs());
        sendUxLogEvent (juce::var (ev));
        offerToSaveRecording = false;
        juce::Thread::sleep (50);   // let the datagram land before the server is killed
    }

    if (wasRecording)
        finishRecording (offerToSaveRecording);

    // Detach the analyser before the engine goes away: pushBlock() is called
    // from its audio thread.
    stopSpectralLogging();

    openStageControl.stop();
    embeddedGui.stop();
    engine.stop();

    startStopButton.setButtonText (kStartText);
    audioDeviceBox.setEnabled (true);
    channelsBox.setEnabled (true);
    sampleRateBox.setEnabled (true);
    updateAddressPanel (false);
    updateButtonStates();

    // finishRecording() has already put the file's fate in the status line;
    // don't overwrite it with "Stopped."
    if (! wasRecording)
        setStatus ("Stopped.");
}

//==============================================================================
void MainComponent::recordClicked()
{
    if (recorder.isRecording())
        finishRecording (true);
    else
        startRecording();

    updateButtonStates();
}

void MainComponent::startRecording()
{
    if (! engine.isRunning())
    {
        setStatus ("Start formuls before recording.");
        return;
    }

    // Record exactly what the engine is producing: the channel count it
    // opened with, and the rate the device really runs at (which is not
    // always the rate that was asked for).
    const auto numChannels = engine.getNumOutputChannels();
    const auto sampleRate  = engine.getActualSampleRate();
    const auto destination = AudioRecorder::makeRecordingFile();

    if (auto result = recorder.start (destination, numChannels, sampleRate);
        result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    setStatus ("Recording " + juce::String (numChannels) + " channels @ "
               + juce::String ((int) sampleRate) + " Hz to "
               + destination.getFileName() + "...");
}

void MainComponent::finishRecording (bool offerToSaveRecording)
{
    recorder.stop();

    const auto recorded = recorder.getFile();

    if (! recorded.existsAsFile())
    {
        setStatus ("Recording failed: no file was written.");
        return;
    }

    const auto describe = [] (const juce::File& f)
    {
        return f.getFullPathName() + " ("
             + juce::File::descriptionOfSizeInBytes (f.getSize()) + ")";
    };

    if (! offerToSaveRecording)
    {
        // Shutting down: no dialog, just leave the file where it is.
        setStatus ("Recording saved to " + describe (recorded));
        return;
    }

    setStatus ("Recording finished -- choose where to save it.");

    saveChooser = std::make_unique<juce::FileChooser> ("Save recording as...",
                                                       recorded, "*." + recorded.getFileExtension().trimCharactersAtStart ("."));

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    saveChooser->launchAsync (flags,
        [safeThis = juce::Component::SafePointer (this), recorded, describe]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            auto chosen = chooser.getResult();

            // Cancelled: the take is not lost, it just stays where it was
            // recorded (see AudioRecorder.h).
            if (chosen == juce::File())
            {
                safeThis->setStatus ("Recording kept at " + describe (recorded));
                return;
            }

            if (! chosen.hasFileExtension (recorded.getFileExtension()))
                chosen = chosen.withFileExtension (recorded.getFileExtension());

            if (chosen == recorded)
            {
                safeThis->setStatus ("Recording saved to " + describe (recorded));
                return;
            }

            chosen.deleteFile();   // moveFileTo won't overwrite on its own

            if (recorded.moveFileTo (chosen))
                safeThis->setStatus ("Recording saved to " + describe (chosen));
            else
                safeThis->setStatus ("Could not save to " + chosen.getFullPathName()
                                     + " -- recording kept at " + describe (recorded));
        });
}

void MainComponent::updateButtonStates()
{
    const bool isRecording = recorder.isRecording();

    // In a UX-logging build the session take runs from Start to Stop; a
    // stray click must not end it, so the button only reports it.
    const bool sessionTake = isRecording && openStageControl.isUxLogging();

    recordButton.setButtonText (sessionTake ? "Recording session" : isRecording ? kStopRecordText : kRecordText);
    recordButton.setEnabled (engine.isRunning() && ! sessionTake);
    if (isRecording)
        recordButton.setColour (juce::TextButton::buttonColourId, style::recordActive);
    else
        recordButton.removeColour (juce::TextButton::buttonColourId);   // back to the look and feel's

    // Not shown (see the constructor); kept disabled as well.
    screenshotButton.setEnabled (false);
    savePresetButton.setEnabled (false);
    loadPresetButton.setEnabled (false);

    startStopButton.setEnabled (! replayer.isActive());
    replayButton.setEnabled (! engine.isRunning());
    replayButton.setButtonText (replayer.isActive() ? "Stop Replay" : "Replay Session...");
}

void MainComponent::screenshotClicked()
{
    // juce::ChildProcess's destructor neither kills nor reaps the child, so
    // replacing a live one would leave a zombie behind and put two Playwright
    // runs on the GUI at once. Wait for the first to finish instead.
    if (screenshotProc != nullptr && screenshotProc->isRunning())
    {
        setStatus ("A screenshot run is already in progress.");
        return;
    }

    // The script lives in src/tools/ in the repo.  In a development build
    // the resource root IS src/, so tools/ is a direct child.  In a packaged
    // app the resource root is Contents/Resources/ — walk up to the repo root
    // and look for src/tools/ there.
    const auto resourceRoot = findResourceRoot();
    auto script = resourceRoot.getChildFile ("tools").getChildFile ("screenshot-gui.py");

    if (! script.existsAsFile())
    {
        auto dir = resourceRoot;
        for (int i = 0; i < 8; ++i)
        {
            dir = dir.getParentDirectory();
            auto candidate = dir.getChildFile ("src").getChildFile ("tools")
                                 .getChildFile ("screenshot-gui.py");
            if (candidate.existsAsFile())
            {
                script = candidate;
                break;
            }
        }
    }

    if (! script.existsAsFile())
    {
        setStatus ("Screenshot tool not found (src/tools/screenshot-gui.py).");
        return;
    }

    auto defaultDir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                          .getChildFile ("formuls-gui");

    screenshotChooser = std::make_unique<juce::FileChooser> ("Save screenshots to...",
                                                              defaultDir, "");

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    screenshotChooser->launchAsync (flags,
        [safeThis = juce::Component::SafePointer (this), script]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            auto dir = chooser.getResult();

            if (dir == juce::File())
            {
                safeThis->setStatus ("Screenshot cancelled.");
                return;
            }

            dir.createDirectory();

            safeThis->screenshotProc = std::make_unique<juce::ChildProcess>();
            juce::StringArray args { "python3", script.getFullPathName(),
                                     "-o", dir.getFullPathName() };

            if (safeThis->screenshotProc->start (args))
                safeThis->setStatus ("Taking screenshots... saving to " + dir.getFileName());
            else
            {
                safeThis->setStatus ("Could not run screenshot tool.");
                safeThis->screenshotProc.reset();
            }
        });
}

void MainComponent::savePresetClicked()
{
    if (! engine.isRunning())
    {
        setStatus ("Start formuls before saving a preset.");
        return;
    }

    // launchAsync keeps the chooser alive through the unique_ptr below, so
    // replacing it while its dialog is open would destroy a live one.
    if (presetDialogOpen)
    {
        setStatus ("A preset dialog is already open.");
        return;
    }

    auto defaultDir = presetDirectory();
    defaultDir.createDirectory();

    presetSaveChooser = std::make_unique<juce::FileChooser> ("Save preset as...",
                                                             defaultDir, "*.state");

    auto flags = juce::FileBrowserComponent::saveMode
               | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::warnAboutOverwriting;

    presetDialogOpen = true;

    presetSaveChooser->launchAsync (flags,
        [safeThis = juce::Component::SafePointer (this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            safeThis->presetDialogOpen = false;

            auto chosen = chooser.getResult();

            if (chosen == juce::File())
            {
                safeThis->setStatus ("Preset save cancelled.");
                return;
            }

            // The dialog's overwrite warning applied to the name as typed. If
            // the extension has to be added here then that warning was about a
            // different file from the one about to be written, so ask again
            // rather than replacing a preset the user was never shown.
            if (! chosen.hasFileExtension ("state"))
            {
                chosen = chosen.withFileExtension ("state");

                if (chosen.existsAsFile())
                {
                    safeThis->confirmOverwriteThenSave (chosen);
                    return;
                }
            }

            safeThis->sendPresetSave (chosen);
        });
}

void MainComponent::confirmOverwriteThenSave (const juce::File& file)
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("Replace preset?")
                       .withMessage (file.getFileName() + " already exists in "
                                     + file.getParentDirectory().getFileName()
                                     + ".\n\nSaving will replace it.")
                       .withButton ("Replace")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // NativeMessageBox, not AlertWindow::showAsync, because only the native
    // box promises a plain button index. AlertWindow's own two-button layout
    // numbers the FIRST button 1 and the second 0 (LookAndFeel_V2::
    // createAlertWindow), and JUCE picks that path unless native alert windows
    // have been turned on -- which they are not by default. Going through
    // AlertWindow here would silently swap Replace and Cancel.
    juce::NativeMessageBox::showAsync (options,
        [safeThis = juce::Component::SafePointer (this), file] (int buttonIndex)
        {
            if (safeThis == nullptr)
                return;

            if (buttonIndex == 0)
                safeThis->sendPresetSave (file);
            else
                safeThis->setStatus ("Preset save cancelled.");
        });
}

void MainComponent::sendPresetSave (const juce::File& file)
{
    // /STATE/SAVE is carried out by the browsers connected to Open Stage
    // Control, not by the server itself: with no browser showing the GUI it is
    // a silent no-op, and nothing is reported back either way. The file
    // arriving on disk is the only confirmation there is, so note what was
    // there beforehand and then watch for it to change.
    const auto before = file.existsAsFile() ? file.getLastModificationTime()
                                            : juce::Time();

    if (! sendOscToOsc ("/STATE/SAVE", file.getFullPathName()))
    {
        setStatus ("Could not reach the formuls GUI to save the preset.");
        return;
    }

    setStatus ("Saving preset...");
    awaitPresetSave (file, before, kPresetSaveAttempts);
}

void MainComponent::awaitPresetSave (const juce::File& file, juce::Time before,
                                     int attemptsLeft)
{
    if (file.existsAsFile() && file.getLastModificationTime() > before)
    {
        setStatus ("Preset saved to " + file.getFileName());
        return;
    }

    if (attemptsLeft <= 0)
    {
        setStatus ("Preset was not written -- open the formuls GUI in a browser, "
                   "then save again.");
        return;
    }

    juce::Timer::callAfterDelay (kPresetPollIntervalMs,
        [safeThis = juce::Component::SafePointer (this), file, before, attemptsLeft]
        {
            if (safeThis != nullptr)
                safeThis->awaitPresetSave (file, before, attemptsLeft - 1);
        });
}

void MainComponent::loadPresetClicked()
{
    if (! engine.isRunning())
    {
        setStatus ("Start formuls before loading a preset.");
        return;
    }

    if (presetDialogOpen)
    {
        setStatus ("A preset dialog is already open.");
        return;
    }

    auto defaultDir = presetDirectory();

    if (! defaultDir.isDirectory())
        defaultDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    presetLoadChooser = std::make_unique<juce::FileChooser> ("Load preset...",
                                                             defaultDir, "*.state");

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    presetDialogOpen = true;

    presetLoadChooser->launchAsync (flags,
        [safeThis = juce::Component::SafePointer (this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            safeThis->presetDialogOpen = false;

            auto chosen = chooser.getResult();

            if (chosen == juce::File())
            {
                safeThis->setStatus ("Preset load cancelled.");
                return;
            }

            safeThis->sendPresetLoad (chosen);
        });
}

void MainComponent::sendPresetLoad (const juce::File& file)
{
    // A bad path is invisible downstream: /STATE/OPEN on a file that is missing
    // or unreadable prints nothing in the server's log and nothing in the
    // browser's console. Whatever can be checked has to be checked here.
    if (! file.existsAsFile())
    {
        setStatus ("Preset file not found: " + file.getFileName());
        return;
    }

    if (! juce::JSON::parse (file.loadFileAsString()).isObject())
    {
        setStatus ("Not a readable preset: " + file.getFileName());
        return;
    }

    if (! sendOscToOsc ("/STATE/OPEN", file.getFullPathName()))
    {
        setStatus ("Could not reach the formuls GUI to load the preset.");
        return;
    }

    // Deliberately not "loaded". Unlike a save, which leaves a file behind to
    // look at, a load produces no reply, no log line and no file, so whether a
    // browser was there to act on it cannot be seen from here.
    setStatus ("Preset sent to the GUI: " + file.getFileName());
}

juce::File MainComponent::presetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("formuls").getChildFile ("presets");
}

bool MainComponent::sendOscToOsc (const juce::String& address, const juce::String& value)
{
    if (! useNodeGui)
    {
        if (! embeddedGui.isRunning())
            return false;
        embeddedGui.receiveFromApp (juce::OSCMessage (juce::OSCAddressPattern (address), value));
        return true;
    }

    // Construct a minimal OSC message: address + ",s" type tag + string value.
    // Sent as UDP to the O-S-C server's OSC receive port, which is the port the
    // GUI is served on: o-s-c's --osc-port defaults to --port.
    juce::MemoryBlock msg;

    // OSC address (null-terminated, padded to 4-byte boundary)
    auto addrBytes = address.toRawUTF8();
    auto addrLen   = (int) strlen (addrBytes);
    msg.append (addrBytes, (size_t) addrLen);
    int addrPad = 4 - (addrLen % 4);
    for (int i = 0; i < addrPad; ++i)
        msg.append ("\0", 1);

    // OSC type tag string: ",s" (null-terminated, padded to 4 bytes)
    msg.append (",s\0\0", 4);

    // OSC string argument (null-terminated, padded to 4-byte boundary)
    auto valBytes = value.toRawUTF8();
    auto valLen   = (int) strlen (valBytes);
    msg.append (valBytes, (size_t) valLen);
    int valPad = 4 - (valLen % 4);
    for (int i = 0; i < valPad; ++i)
        msg.append ("\0", 1);

    juce::DatagramSocket socket (false);

    if (! socket.bindToPort (0))   // bind to any available local port
        return false;

    return socket.write ("127.0.0.1", OpenStageControlProcess::guiPort,
                         msg.getData(), (int) msg.getSize()) == (int) msg.getSize();
}

void MainComponent::setStatus (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
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

//==============================================================================
void MainComponent::replayClicked()
{
    if (replayer.isActive())
    {
        stopReplay();
        return;
    }

    replayChooser = std::make_unique<juce::FileChooser> ("Replay which session log?",
                                                         OpenStageControlProcess::getUxLogDirectory(),
                                                         "*.jsonl");
    replayChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
        {
            if (safeThis != nullptr && chooser.getResult().existsAsFile())
                safeThis->beginReplay (chooser.getResult());
        });
}

juce::File MainComponent::findSessionAudio (const juce::File& sessionLog)
{
    // rec-start is written within seconds of the log opening, so it is near
    // the top; reading line by line avoids loading a multi-hour log here.
    juce::FileInputStream in (sessionLog);

    for (int lines = 0; in.openedOk() && ! in.isExhausted() && lines < 200000; ++lines)
    {
        const auto line = in.readNextLine();

        if (! line.contains ("\"rec-start\""))
            continue;

        const auto ev = juce::JSON::parse (line);
        const auto name = ev.getProperty ("file", {}).toString();

        if (name.isNotEmpty())
            return sessionLog.getSiblingFile (name);
    }

    return {};
}

void MainComponent::beginReplay (const juce::File& sessionLog)
{
    const auto deviceIndex = audioDeviceBox.getSelectedId() - 1;

    if (deviceIndex < 0)
    {
        setStatus ("Please select an audio output device first.");
        return;
    }

    const auto audio = findSessionAudio (sessionLog);

    if (! audio.existsAsFile())
    {
        setStatus ("No session audio for " + sessionLog.getFileName()
                   + " (its rec-start event names a file that is not next to it).");
        return;
    }

    if (auto result = replayer.start (deviceManager, outputDeviceTypes[deviceIndex],
                                      outputDeviceNames[deviceIndex], audio);
        result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    if (auto result = openStageControl.startReplay (findResourceRoot(), sessionLog);
        result.failed())
    {
        replayer.stop();
        setStatus ("Replay GUI failed: " + result.getErrorMessage());
        return;
    }

    vuMeter.setPeakLevels (replayer.peaks.data());
    positionSlider.setRange (0.0, juce::jmax (1.0, replayer.getLengthMs()), 1.0);
    positionSlider.setValue (0.0, juce::dontSendNotification);
    playPauseButton.setButtonText ("Play");

    for (auto* c : { (juce::Component*) &playPauseButton, (juce::Component*) &positionSlider,
                     (juce::Component*) &replayTimeLabel })
        c->setVisible (true);

    audioDeviceBox.setEnabled (false);
    channelsBox.setEnabled (false);
    sampleRateBox.setEnabled (false);

    updateAddressPanel (true);
    updateButtonStates();
    resized();
    replayTick.startTimerHz (20);

    setStatus ("Replaying " + sessionLog.getFileName() + " with " + audio.getFileName()
               + ". Open the GUI in a browser, then press Play.");
}

void MainComponent::stopReplay()
{
    if (! replayer.isActive())
        return;

    replayTick.stopTimer();
    openStageControl.stop();
    replayer.stop();
    vuMeter.setPeakLevels (engine.peakLevels.data());

    for (auto* c : { (juce::Component*) &playPauseButton, (juce::Component*) &positionSlider,
                     (juce::Component*) &replayTimeLabel })
        c->setVisible (false);

    audioDeviceBox.setEnabled (true);
    channelsBox.setEnabled (true);
    sampleRateBox.setEnabled (true);

    updateAddressPanel (false);
    updateButtonStates();
    resized();
    setStatus ("Replay stopped.");
}

void MainComponent::replayTimerCallback()
{
    const auto pos = replayer.getPositionMs();

    // the slider follows playback, except while it is being dragged
    if (! positionSlider.isMouseButtonDown())
        positionSlider.setValue (pos, juce::dontSendNotification);

    const auto clock = [] (double ms)
    {
        const auto s = (int) (ms / 1000.0);
        return juce::String (s / 3600) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2)
             + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    };
    replayTimeLabel.setText (clock (pos) + " / " + clock (replayer.getLengthMs()),
                             juce::dontSendNotification);

    if (! replayer.isPlaying() && playPauseButton.getButtonText() == "Pause")
        playPauseButton.setButtonText ("Play");   // reached the end

    // The audio position is the replay's clock: the module sends the GUI
    // everything logged up to it, and treats a jump as a seek.
    sendOscToOsc ("/formuls/replay/pos", juce::String ((juce::int64) pos));
}

} // namespace formuls
