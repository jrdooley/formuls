/*
 * SessionReplayer.h
 *
 * Replay mode for UX-logging builds (src/tools/uxlog/README.md): plays a
 * session's uxaudio-*.ogg through the chosen output device, with no libpd
 * engine running, while formuls-replay-module.js moves the GUI's widgets in
 * step with it. The audio is the clock: MainComponent reads getPositionMs()
 * and sends it to the replay module, which sends out whatever the log holds
 * up to that moment.
 *
 * Threading: audioDeviceIOCallbackWithContext() runs on the device thread
 * and only pulls from the AudioTransportSource, which reads ahead on
 * readAheadThread so the device thread never touches the disk. Everything
 * else is message-thread only.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{

class SessionReplayer : private juce::AudioIODeviceCallback
{
public:
    SessionReplayer() { formats.registerBasicFormats(); }
    ~SessionReplayer() override { stop(); }

    /** Opens `audioFile` and starts the device, paused at the beginning. */
    juce::Result start (juce::AudioDeviceManager& deviceManager,
                        const juce::String& deviceType, const juce::String& deviceName,
                        const juce::File& audioFile)
    {
        stop();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (audioFile));
        if (reader == nullptr)
            return juce::Result::fail ("Could not read " + audioFile.getFullPathName());

        const auto rate = reader->sampleRate;

        if (deviceType.isNotEmpty() && deviceManager.getCurrentAudioDeviceType() != deviceType)
            deviceManager.setCurrentAudioDeviceType (deviceType, true);

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.outputDeviceName = deviceName;
        setup.inputDeviceName.clear();
        setup.sampleRate = rate;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
        setup.useDefaultInputChannels = false;

        if (auto error = deviceManager.initialise (0, 2, nullptr, true, deviceName, &setup);
            error.isNotEmpty())
            return juce::Result::fail ("Could not open audio device: " + error);

        readAheadThread.startThread (juce::Thread::Priority::normal);
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        // passing the file's rate makes the transport resample if the device
        // could not open at it
        transport.setSource (readerSource.get(), 1 << 16, &readAheadThread, rate, 2);

        manager = &deviceManager;
        deviceManager.addAudioCallback (this);
        lengthMs = transport.getLengthInSeconds() * 1000.0;
        return juce::Result::ok();
    }

    void stop()
    {
        if (manager != nullptr)
        {
            manager->removeAudioCallback (this);
            manager->closeAudioDevice();
            manager = nullptr;
        }
        transport.stop();
        transport.setSource (nullptr);
        readerSource.reset();
        readAheadThread.stopThread (2000);
        peaks[0] = peaks[1] = 0.0f;
    }

    bool isActive() const noexcept           { return manager != nullptr; }
    bool isPlaying() const                   { return transport.isPlaying(); }
    void setPlaying (bool shouldPlay)        { shouldPlay ? transport.start() : transport.stop(); }
    double getLengthMs() const noexcept      { return lengthMs; }
    double getPositionMs() const             { return transport.getCurrentPosition() * 1000.0; }
    void setPositionMs (double ms)           { transport.setPosition (juce::jmax (0.0, ms) / 1000.0); }

    /** Peak levels for StereoMeter, as FormulsEngine::peakLevels. */
    std::array<std::atomic<float>, 2> peaks {};

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        transport.prepareToPlay (device->getCurrentBufferSizeSamples(),
                                 device->getCurrentSampleRate());
    }

    void audioDeviceStopped() override { transport.releaseResources(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        juce::AudioBuffer<float> buffer (outputs, numOutputs, numSamples);
        transport.getNextAudioBlock (juce::AudioSourceChannelInfo (buffer));

        for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
            peaks[(size_t) ch] = juce::jmax (buffer.getMagnitude (ch, 0, numSamples),
                                             peaks[(size_t) ch].load() * 0.9f);
    }

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAheadThread { "formuls replay reader" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioDeviceManager* manager = nullptr;
    double lengthMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionReplayer)
};

} // namespace formuls
