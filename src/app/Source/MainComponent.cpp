// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

/*
 * MainComponent.cpp -- see MainComponent.h for an overview.
 */

#include "MainComponent.h"
#include "FormulsLookAndFeel.h"
#include "ResourceLocator.h"
#include "PowerOff.h"
#include "RecordingLocation.h"

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

    // The GUI's RESET tab can ask the machine to power down. The appliance
    // had no way to do this short of a keyboard and a shell, which is the
    // whole reason this exists.
    //
    // The app cannot power the machine off itself: it runs as the
    // unprivileged "formuls" user, the image has no polkit, and logind
    // refuses a non-root request from a session with no seat (it runs under
    // Xvfb). So it writes a file that a root-side systemd path unit is
    // watching, which runs "systemctl poweroff" -- see
    // formuls-poweroff.path in the formuls-embedded overlay. The app needs
    // no new privilege beyond writing into a directory it already owns.
    //
    // Nothing here flushes the recording or the logs. Writing the file makes
    // systemd stop formuls.service as part of the normal shutdown, which
    // sends SIGINT to this process and takes the existing clean-stop path --
    // the one KillMode=control-group was fixed for. Duplicating it here
    // would be a second, less-tested way to do the same thing. The quit
    // below is belt and braces for the narrower case where the directory is
    // there but the path unit is not running: the app still goes away, as the
    // button implies it should.
    //
    // Only wired where something can act on it -- see PowerOff.h. On a
    // desktop build the trigger directory does not exist, so this could only
    // quit the app, which is not what a button labelled POWER OFF promises
    // and not something the user needs a button for: they start and stop
    // formuls from their operating system. The GUI server hides the control
    // in the same case, so this is the second of the two gates rather than
    // the only one.
    if (poweroff::isAvailable())
    {
        engine.onPowerOffRequested = []
        {
            const auto trigger = poweroff::triggerFile();

            auto result = trigger.create();

            if (result.wasOk())
                juce::Logger::writeToLog ("power off: requested via " + trigger.getFullPathName());
            else
                juce::Logger::writeToLog ("power off: could NOT write " + trigger.getFullPathName()
                                          + " (" + result.getErrorMessage().trim() + ") -- the "
                                          "machine will stay up; quitting anyway");

            if (auto* app = juce::JUCEApplication::getInstance())
                app->systemRequestedQuit();
        };
    }
    else
    {
        juce::Logger::writeToLog ("power off: not available ("
                                  + poweroff::triggerFile().getParentDirectory().getFullPathName()
                                  + " does not exist) -- leaving the request unwired");
    }

    // The GUI's RESET tab can also start and stop a recording. On the
    // appliance this is the only way to record at all: it runs headless, so
    // the Record button in this window is on a screen nobody is looking at.
    //
    // This deliberately reuses startRecording()/finishRecording() rather than
    // driving the recorder directly, so a take started from the tablet is the
    // same take in every respect -- same destination, same channel count,
    // same clean finish -- and there is one place where recording can go
    // wrong instead of two.
    if (recording::overriddenUnavailable())
    {
        juce::Logger::writeToLog ("recording: FORMULS_RECORDING_AVAILABLE=0 -- leaving the "
                                  "request unwired, as the plugin does");
    }
    else
    {
        engine.onRecordRequested = [this] (bool shouldRecord)
        {
            if (shouldRecord == recorder.isRecording())
            {
                // Already in the state being asked for. Happens when the
                // desktop Record button and the tablet disagree for a moment;
                // re-publish so the tablet catches up rather than acting twice.
                publishRecordingStateToGui();
                return;
            }

            if (shouldRecord)
            {
                startRecording();
            }
            else
            {
                // false: no "save as" dialog. On the appliance there is no one
                // at a screen to answer it, and the take is already where the
                // user wants it -- see RecordingLocation.h.
                finishRecording (false);
            }

            // startRecording() can refuse (the engine is not running, the disk
            // is nearly full, or the file could not be opened) and says so in
            // the status line. The tablet cannot see that line, so tell it what
            // actually happened rather than what was asked for: a button left
            // lit over a take that never started is the worst outcome here.
            updateButtonStates();
            publishRecordingStateToGui();
        };
    }

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

    auto buttonRow = area.removeFromTop (style::buttonHeight);
    startStopButton.setBounds (buttonRow.removeFromLeft (style::buttonWidth));
    recordButton.setBounds (buttonRow.removeFromRight (style::recordButtonWidth));
    area.removeFromTop (style::controlSpacing);

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
    //
    // The app records, so it serves the record button. Set before start():
    // the session is parsed and gated there, and a flag arriving afterwards
    // would be read by nobody.
    embeddedGui.setRecordingAvailable (true);

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
}

void MainComponent::stopEverything (bool offerToSaveRecording)
{
    // Finish the take before the audio device goes away, so the tail of the
    // recording isn't cut off by the engine shutting down.
    const bool wasRecording = recorder.isRecording();

    if (wasRecording)
        finishRecording (offerToSaveRecording);

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

    // A take started here is a take the tablet should show too, so the two
    // controls never disagree about whether something is being recorded.
    publishRecordingStateToGui();
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

    // Refuse rather than fill the disk. On the appliance the fallback
    // destination is the root filesystem, so a take that runs it out of space
    // breaks the machine and not just the recording -- see
    // RecordingLocation.h.
    if (! recording::hasRoomFor (destination))
    {
        const auto message = "Not enough room to record on "
                           + destination.getParentDirectory().getFullPathName()
                           + " (" + juce::File::descriptionOfSizeInBytes (
                                        recording::freeBytesFor (destination))
                           + " free, " + juce::File::descriptionOfSizeInBytes (
                                             recording::minimumFreeBytes())
                           + " needed).";

        setStatus (message);
        juce::Logger::writeToLog ("recording refused: " + message);
        return;
    }

    if (auto result = recorder.start (destination, numChannels, sampleRate);
        result.failed())
    {
        setStatus (result.getErrorMessage());
        return;
    }

    // Watch the destination while the take runs. Checking only at the start
    // would leave the long takes -- the ones most likely to fill a disk --
    // entirely unguarded.
    diskGuard.startTimer (5000);

    setStatus ("Recording " + juce::String (numChannels) + " channels @ "
               + juce::String ((int) sampleRate) + " Hz to "
               + destination.getFileName() + "...");
}

void MainComponent::checkRecordingSpace()
{
    if (! recorder.isRecording())
    {
        diskGuard.stopTimer();
        return;
    }

    const auto file = recorder.getFile();

    if (recording::hasRoomFor (file))
        return;

    // Stop cleanly while there is still room to write the header, rather
    // than letting the write fail somewhere in the middle of the take. false:
    // no "save as" dialog -- this can fire on the appliance, where nobody is
    // there to answer one.
    const auto free = recording::freeBytesFor (file);

    juce::Logger::writeToLog ("recording stopped early: only "
                              + juce::File::descriptionOfSizeInBytes (free)
                              + " left on "
                              + file.getParentDirectory().getFullPathName()
                              + " -- finishing the take while the file can still be closed");

    finishRecording (false);
    updateButtonStates();
    publishRecordingStateToGui();

    setStatus ("Recording stopped: the disk is nearly full ("
               + juce::File::descriptionOfSizeInBytes (free) + " left).");
}

void MainComponent::finishRecording (bool offerToSaveRecording)
{
    // Whatever ended the take, the space poll has nothing left to watch.
    diskGuard.stopTimer();

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
                                                       recorded, "*.wav");

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

            if (! chosen.hasFileExtension ("wav"))
                chosen = chosen.withFileExtension ("wav");

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

    recordButton.setButtonText (isRecording ? kStopRecordText : kRecordText);
    recordButton.setEnabled (engine.isRunning());
    if (isRecording)
        recordButton.setColour (juce::TextButton::buttonColourId, style::recordActive);
    else
        recordButton.removeColour (juce::TextButton::buttonColourId);   // back to the look and feel's

    // Not shown (see the constructor); kept disabled as well.
    screenshotButton.setEnabled (false);
    savePresetButton.setEnabled (false);
    loadPresetButton.setEnabled (false);
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

void MainComponent::publishRecordingStateToGui()
{
    sendOscFloatToOsc (recording::oscAddress, recorder.isRecording() ? 1.0f : 0.0f);
}

bool MainComponent::sendOscFloatToOsc (const juce::String& address, float value)
{
    // Deliberately a separate name rather than an overload: juce::String has
    // a non-explicit constructor from int, so sendOscToOsc (addr, 1) would be
    // ambiguous and the compiler would pick for us.
    if (! useNodeGui)
    {
        if (! embeddedGui.isRunning())
            return false;
        embeddedGui.receiveFromApp (juce::OSCMessage (juce::OSCAddressPattern (address), value));
        return true;
    }

    // Same minimal encoder as the string version below, with a ",f" tag. A
    // widget's value has to arrive as a number: o-s-c compares it against the
    // button's on/off values, and the string "1" is not the number 1, so a
    // string here would leave the button dark while recording.
    juce::MemoryBlock msg;

    auto addrBytes = address.toRawUTF8();
    auto addrLen   = (int) strlen (addrBytes);
    msg.append (addrBytes, (size_t) addrLen);
    int addrPad = 4 - (addrLen % 4);
    for (int i = 0; i < addrPad; ++i)
        msg.append ("\0", 1);

    msg.append (",f\0\0", 4);

    // OSC floats are big-endian IEEE-754.
    juce::uint32 bits;
    static_assert (sizeof (bits) == sizeof (value), "float is not 32 bits");
    std::memcpy (&bits, &value, sizeof (bits));
    const auto bigEndian = juce::ByteOrder::swapIfLittleEndian (bits);
    msg.append (&bigEndian, sizeof (bigEndian));

    juce::DatagramSocket socket (false);

    if (! socket.bindToPort (0))
        return false;

    return socket.write ("127.0.0.1", OpenStageControlProcess::guiPort,
                         msg.getData(), (int) msg.getSize()) == (int) msg.getSize();
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

} // namespace formuls
