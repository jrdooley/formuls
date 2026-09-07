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
 *   - a Record button to the right of Start/Stop, enabled while the engine
 *     is running. The first press starts recording the engine's output to a
 *     WAV file at the rate the device is running at, with one channel per
 *     selected output channel (2 -> stereo, 14 -> a 14-channel file). The
 *     next press stops the take and opens a "save as" dialog. See
 *     AudioRecorder.h for where the audio is written while recording, and
 *     what happens if the dialog is cancelled,
 *   - a Take Screenshot button (top right) that captures each visible page
 *     of the control GUI as a PNG using Playwright, saving to
 *     ~/Desktop/formuls-gui/ while the engine is running,
 *   - Save Preset and Load Preset buttons to the left of it, which ask Open
 *     Stage Control to write or read a .state file (~/Documents/formuls/
 *     presets by default). Both are carried out by the browsers connected to
 *     the GUI rather than by the server, so neither does anything until the
 *     GUI is open in a browser somewhere -- a save that goes nowhere is
 *     reported as such, a load cannot be (see sendPresetLoad),
 *   - horizontal stereo VU meters monitoring the first two output channels,
 *     with peak hold and a green-yellow-red gradient,
 *   - a read-only panel listing the web addresses the control GUI can be
 *     opened at once it is running: the loopback address for a browser on
 *     this machine, plus this machine's address on each network it is
 *     attached to, for a tablet or phone on the same network. The text can
 *     be selected and copied, but not edited.
 *   - a status line reporting what is going on.
 *
 * Stopping keeps the app (and this window) alive so a different device or
 * channel count can be chosen and the engine started again.
 *
 * Layout metrics and all colours/fonts live in FormulsLookAndFeel.h.
 */

#pragma once

#include <JuceHeader.h>
#include "AudioRecorder.h"
#include "FormulsEngine.h"
#include "OpenStageControlProcess.h"
#include "StereoMeter.h"

namespace formuls
{

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void populateDeviceList();
    void startStopClicked();
    void startEverything();

    /** Stops the o-s-c server and the engine, finishing any recording first.
        @param offerToSaveRecording  true from the Stop button (a take in
                                     progress is finished and the save dialog
                                     opened); false when the app is shutting
                                     down, where a modal dialog is not an
                                     option -- the finished file is simply
                                     left where it was recorded. */
    void stopEverything (bool offerToSaveRecording);

    void recordClicked();
    void startRecording();

    /** Finishes the take, then (unless the app is closing) asks where to
        keep the file and moves it there. */
    void finishRecording (bool offerToSaveRecording);

    /** Enables/disables and re-labels every button whose availability depends
        on the engine or the recorder: record, screenshot and the two preset
        buttons. */
    void updateButtonStates();

    void screenshotClicked();

    void savePresetClicked();
    void loadPresetClicked();

    /** Asks before replacing an existing preset, in the one case the file
        dialog's own warning cannot cover -- see savePresetClicked(). */
    void confirmOverwriteThenSave (const juce::File& file);

    /** Sends /STATE/SAVE, then hands over to awaitPresetSave(). */
    void sendPresetSave (const juce::File& file);

    /** Polls for the preset file to be written and reports what actually
        happened, retrying every kPresetPollIntervalMs until attemptsLeft runs
        out. Open Stage Control sends back nothing at all, so a file newer than
        `before` is the only evidence a save worked. */
    void awaitPresetSave (const juce::File& file, juce::Time before, int attemptsLeft);

    /** Checks the file is present and parses, then sends /STATE/OPEN. */
    void sendPresetLoad (const juce::File& file);

    /** ~/Documents/formuls/presets -- where both preset dialogs start. */
    static juce::File presetDirectory();

    /** Sends a one-string OSC message to the Open Stage Control server.
        @returns false if the socket could not be bound or the whole message
                 could not be written. A true return means the datagram left
                 this machine, not that anything acted on it. */
    bool sendOscToOsc (const juce::String& address, const juce::String& value);

    void setStatus (const juce::String& message);

    /** Fills the address panel: the GUI's web addresses when the server is
        running, or a short hint when it is not. */
    void updateAddressPanel (bool guiIsRunning);

    juce::AudioDeviceManager deviceManager;

    // Declared before the engine so it outlives the engine's recording tap.
    AudioRecorder recorder;
    FormulsEngine engine;
    OpenStageControlProcess openStageControl;

    juce::ComboBox audioDeviceBox;
    juce::ComboBox channelsBox;
    juce::ComboBox sampleRateBox;
    juce::TextButton startStopButton;
    juce::TextButton recordButton;
    juce::TextButton screenshotButton;
    juce::TextButton savePresetButton;
    juce::TextButton loadPresetButton;
    juce::TextEditor addressPanel;
    StereoMeter vuMeter;
    juce::Label statusLabel;

    // Index-aligned: outputDeviceTypes[i] is the AudioIODeviceType that
    // reported outputDeviceNames[i]. Both are rebuilt by populateDeviceList().
    juce::StringArray outputDeviceNames;
    juce::StringArray outputDeviceTypes;

    // Kept alive while the (asynchronous, non-modal) save dialog is open.
    std::unique_ptr<juce::FileChooser> saveChooser;
    std::unique_ptr<juce::FileChooser> screenshotChooser;
    std::unique_ptr<juce::FileChooser> presetSaveChooser;
    std::unique_ptr<juce::FileChooser> presetLoadChooser;
    std::unique_ptr<juce::ChildProcess> screenshotProc;

    // True while either preset dialog is open. Both buttons check it, because
    // replacing a chooser while its dialog is showing destroys a live one.
    bool presetDialogOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace formuls
