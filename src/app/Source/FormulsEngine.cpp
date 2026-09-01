/*
 * FormulsEngine.cpp -- see FormulsEngine.h for an overview.
 */

#include "FormulsEngine.h"

namespace formuls
{

// How often (ms) queued messages from the patch are pumped to the GUI thread.
static constexpr int messagePumpIntervalMs = 30;

// Buffer size requested from the device. If the device cannot do it,
// whatever it *can* do is used instead (the sample rate is chosen by the
// user and passed into start(); libpd is initialised to match whatever the
// device actually provides).
static constexpr int preferredBufferSize = 512;

FormulsEngine::~FormulsEngine()
{
    stop();
}

//==============================================================================
juce::Result FormulsEngine::start (juce::AudioDeviceManager& deviceManager,
                                   const juce::String& outputDeviceName,
                                   int numOutputChannels,
                                   double sampleRate,
                                   const juce::File& resourceRoot)
{
    if (running)
        return juce::Result::ok();

    auto patchDir = resourceRoot.getChildFile ("pd");

    if (! patchDir.getChildFile ("_main.pd").existsAsFile())
        return juce::Result::fail ("Could not find pd/_main.pd in "
                                   + resourceRoot.getFullPathName());

    // ----------------------------------------------------------- open device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = outputDeviceName;
    setup.inputDeviceName.clear();                       // output only
    setup.sampleRate = sampleRate;
    setup.bufferSize = preferredBufferSize;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange (0, numOutputChannels, true);
    setup.useDefaultInputChannels = false;

    auto error = deviceManager.initialise (0, numOutputChannels, nullptr,
                                           true, outputDeviceName, &setup);
    if (error.isNotEmpty())
        return juce::Result::fail ("Could not open audio device: " + error);

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return juce::Result::fail ("Could not open audio device \""
                                   + outputDeviceName + "\"");

    // ------------------------------------------------------------- boot libpd
    // queued = true: patch -> app messages go through a lock-free ring buffer
    // and are delivered on the message thread by timerCallback().
    // libpd runs at the rate the device *actually* opened at, which may
    // differ from the rate requested above if the device can't provide it.
    actualSampleRate = device->getCurrentSampleRate();

    if (! pd.init (0, numOutputChannels, (int) actualSampleRate, true))
    {
        deviceManager.closeAudioDevice();
        return juce::Result::fail ("Could not initialise libpd");
    }

    pdOutputChannels = numOutputChannels;

    pd.setReceiver (this);
    pd.subscribe (quitReceiverName);   // lets the patch quit the app
    pd.computeAudio (true);

    patch = pd.openPatch ("_main.pd", patchDir.getFullPathName().toStdString());

    if (! patch.isValid())
    {
        pd.clear();
        deviceManager.closeAudioDevice();
        return juce::Result::fail ("libpd could not open "
                                   + patchDir.getChildFile ("_main.pd").getFullPathName());
    }

    // -------------------------------------------------------------- go live
    startTimer (messagePumpIntervalMs);
    deviceManager.addAudioCallback (this);
    activeDeviceManager = &deviceManager;
    running = true;

    juce::Logger::writeToLog ("formuls engine started: \"" + device->getName()
                              + "\", " + juce::String (numOutputChannels)
                              + " channels @ " + juce::String ((int) actualSampleRate)
                              + " Hz");
    return juce::Result::ok();
}

void FormulsEngine::stop()
{
    if (! running)
        return;

    running = false;

    if (activeDeviceManager != nullptr)
        activeDeviceManager->removeAudioCallback (this);

    stopTimer();

    if (patch.isValid())
    {
        pd.closePatch (patch);
        patch.clear();
    }

    pd.clear();   // stops DSP, detaches hooks, unsubscribes all receivers

    if (activeDeviceManager != nullptr)
    {
        activeDeviceManager->closeAudioDevice();
        activeDeviceManager = nullptr;
    }

    actualSampleRate = 0;

    juce::Logger::writeToLog ("formuls engine stopped");
}

//==============================================================================
void FormulsEngine::audioDeviceAboutToStart (juce::AudioIODevice*)
{
    const auto blockSize = libpd_blocksize();   // always 64 frames

    pdInputBuffer.assign ((size_t) blockSize, 0.0f);   // no input channels used
    pdOutputBuffer.assign ((size_t) (blockSize * juce::jmax (1, pdOutputChannels)), 0.0f);
    samplesLeftInTick = 0;
}

void FormulsEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    const auto blockSize = libpd_blocksize();

    // libpd renders in fixed 64-frame "ticks" of interleaved audio, while the
    // device may ask for any buffer size. Render one tick at a time into
    // pdOutputBuffer and de-interleave it into JUCE's per-channel buffers,
    // carrying leftover frames across callbacks when sizes don't line up.
    int sampleIndex = 0;

    while (sampleIndex < numSamples)
    {
        if (samplesLeftInTick == 0)
        {
            pd.processFloat (1, pdInputBuffer.data(), pdOutputBuffer.data());
            samplesLeftInTick = blockSize;
        }

        const int tickOffset = blockSize - samplesLeftInTick;
        const int numToCopy  = juce::jmin (samplesLeftInTick, numSamples - sampleIndex);

        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            auto* dest = outputChannelData[ch];

            if (dest == nullptr)
                continue;

            if (ch < pdOutputChannels)
            {
                const auto* src = pdOutputBuffer.data() + tickOffset * pdOutputChannels + ch;

                for (int i = 0; i < numToCopy; ++i)
                    dest[sampleIndex + i] = src[i * pdOutputChannels];
            }
            else
            {
                // device channel with no matching Pd channel: silence
                juce::FloatVectorOperations::clear (dest + sampleIndex, numToCopy);
            }
        }

        samplesLeftInTick -= numToCopy;
        sampleIndex += numToCopy;
    }
}

void FormulsEngine::audioDeviceStopped()
{
    samplesLeftInTick = 0;
}

//==============================================================================
void FormulsEngine::timerCallback()
{
    // Runs on the message thread: moves queued patch messages out of libpd's
    // ring buffer and into the receive* callbacks below.
    pd.receiveMessages();
}

void FormulsEngine::handleQuitRequest()
{
    juce::Logger::writeToLog ("quit requested by the Pd patch");

    if (onQuitRequested != nullptr)
        onQuitRequested();
}

void FormulsEngine::print (const std::string& message)
{
    // Pd [print] output ends up in the app's log / console.
    juce::Logger::writeToLog ("pd: " + juce::String (message));
}

void FormulsEngine::receiveBang (const std::string& dest)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void FormulsEngine::receiveFloat (const std::string& dest, float)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void FormulsEngine::receiveSymbol (const std::string& dest, const std::string&)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void FormulsEngine::receiveList (const std::string& dest, const pd::List&)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void FormulsEngine::receiveMessage (const std::string& dest, const std::string&,
                                    const pd::List&)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

} // namespace formuls
