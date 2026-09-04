/*
 * FormulsEngine.h
 *
 * The formuls audio engine: an instance of libpd running the _main.pd patch,
 * wired directly into JUCE's audio device system.
 *
 * This replaces the old standalone "formulsengine" binary (RtAudio + libpd,
 * launched as a child process by the Python front end). Running libpd inside
 * the app means:
 *
 *   - the audio output device / channel count is chosen with JUCE's
 *     AudioDeviceManager (by name, instead of the old fragile device index),
 *   - the Pd patch can talk to the application directly. In particular the
 *     patch can QUIT THE APP by sending a message to the receive symbol
 *     "formuls-quit", e.g. with a message box containing:
 *
 *         ; formuls-quit bang
 *
 *     (see quitReceiverName below and _main.pd, which does exactly this when
 *     the Open Stage Control GUI requests a shutdown).
 *
 * Threading model
 * ---------------
 * libpd's DSP runs on the audio device thread (audioDeviceIOCallbackWithContext).
 * Messages coming *out* of the patch are queued by libpd in a lock-free ring
 * buffer (PdBase::init(..., queued = true)); a juce::Timer on the message
 * thread drains that queue (pd.receiveMessages()), so PdReceiver callbacks --
 * including the quit request -- arrive safely on the GUI thread.
 * All other libpd calls (init, openPatch, closePatch) are made from the
 * message thread while the audio callback is *not* attached, so no extra
 * locking is required.
 *
 * If a recorder has been attached with setRecorder(), every block the engine
 * renders is also handed to it, straight from the audio callback (see
 * AudioRecorder.h -- the recorder ignores blocks until the user starts a
 * take, so this costs nothing while not recording).
 */

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "PdBase.hpp"
#include "AudioRecorder.h"

namespace formuls
{

class FormulsEngine : private juce::AudioIODeviceCallback,
                      private juce::Timer,
                      private pd::PdReceiver
{
public:
    FormulsEngine() = default;
    ~FormulsEngine() override;

    /** Receive symbol the Pd patch sends to when it wants the app to quit. */
    static constexpr const char* quitReceiverName = "formuls-quit";

    /** Called on the message thread when the patch sends to "formuls-quit". */
    std::function<void()> onQuitRequested;

    /** Opens the requested output device, boots libpd and loads pd/_main.pd
        from the given resource root. Returns Result::ok() or a failure with a
        user-displayable message.

        @param deviceManager     the app's (shared) AudioDeviceManager
        @param outputDeviceName  device name as reported by JUCE
        @param numOutputChannels number of output channels to open (2 or 14)
        @param sampleRate        requested sample rate in Hz. If the device
                                 cannot run at this rate, whatever rate it
                                 does open at is used instead, and libpd is
                                 always initialised at the device's actual
                                 rate so the two never disagree.
        @param resourceRoot      folder containing pd/_main.pd (see ResourceLocator.h)
    */
    juce::Result start (juce::AudioDeviceManager& deviceManager,
                        const juce::String& outputDeviceName,
                        int numOutputChannels,
                        double sampleRate,
                        const juce::File& resourceRoot);

    /** Attaches the recorder that each rendered block is copied to, or
        nullptr to detach. Call this from the message thread while the engine
        is stopped (MainComponent does it once, at construction); the
        recorder itself decides when a block is actually written. */
    void setRecorder (AudioRecorder* recorderToUse) noexcept  { recorder = recorderToUse; }

    /** The number of output channels the engine is running with (0 when
        stopped). This is what a recording should have. */
    int getNumOutputChannels() const noexcept   { return running ? pdOutputChannels : 0; }

    /** The sample rate the audio device actually opened at (0 when stopped).
        May differ from the rate passed to start() if the device could not
        provide it. */
    double getActualSampleRate() const noexcept   { return actualSampleRate; }

    /** Stops audio, closes the patch and releases the audio device.
        Safe to call when not running. */
    void stop();

    bool isRunning() const noexcept   { return running; }

    /** Peak levels for the first two output channels, updated on the audio
        thread. Read from the GUI thread to drive VU meters. */
    std::array<std::atomic<float>, 2> peakLevels {{ 0.0f, 0.0f }};

private:
    // juce::AudioIODeviceCallback
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceStopped() override;

    // juce::Timer -- drains libpd's queued messages on the message thread
    void timerCallback() override;

    // pd::PdReceiver -- callbacks from the patch (via the queued ring buffer)
    void print (const std::string& message) override;
    void receiveBang (const std::string& dest) override;
    void receiveFloat (const std::string& dest, float value) override;
    void receiveSymbol (const std::string& dest, const std::string& symbol) override;
    void receiveList (const std::string& dest, const pd::List& list) override;
    void receiveMessage (const std::string& dest, const std::string& msg,
                         const pd::List& list) override;

    void handleQuitRequest();

    pd::PdBase pd;
    pd::Patch patch;
    juce::AudioDeviceManager* activeDeviceManager = nullptr;

    int pdOutputChannels = 0;               // channels libpd was initialised with
    double actualSampleRate = 0;            // rate the device really opened at
    std::vector<float> pdInputBuffer;       // dummy interleaved input (0 channels)
    std::vector<float> pdOutputBuffer;      // one Pd tick of interleaved output
    int samplesLeftInTick = 0;              // unconsumed frames in pdOutputBuffer

    // Recording tap. recordChannels/recordSilence are sized in
    // audioDeviceAboutToStart() so the audio callback never allocates;
    // recordSilence stands in for any device channel JUCE hands us as null.
    AudioRecorder* recorder = nullptr;
    std::vector<const float*> recordChannels;
    std::vector<float> recordSilence;

    bool running = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormulsEngine)
};

} // namespace formuls
