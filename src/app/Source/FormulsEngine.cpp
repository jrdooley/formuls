/*
 * FormulsEngine.cpp -- see FormulsEngine.h for an overview.
 */

#include "FormulsEngine.h"
#include "OpenStageControlProcess.h"   // patchOscPort
#include "Diagnostics.h"               // TEMPORARY timing diagnostics

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
                                   const juce::String& outputDeviceType,
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
    // A device name is only ever matched against the CURRENT device type, so
    // select the type the name came from first. Without this, a name from a
    // different type cannot be found, and because initialise() is called with
    // selectDefaultDeviceOnFailure it would silently open the default device
    // and still report success. Only matters where more than one type exists
    // (Linux: ALSA and JACK); macOS has CoreAudio alone.
    if (outputDeviceType.isNotEmpty()
        && deviceManager.getCurrentAudioDeviceType() != outputDeviceType)
        deviceManager.setCurrentAudioDeviceType (outputDeviceType, true);

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

    // Bind the GUI sender before the patch opens; the patch switches over to it
    // once told to, below. If it can't start, the patch keeps its [netsend].
    const bool appSendsOsc = oscOut.start (OpenStageControlProcess::guiPort);
    if (! appSendsOsc)
        juce::Logger::writeToLog ("OSC out: could not open a UDP socket; the patch sends to the GUI itself");
    pd.subscribe (quitReceiverName);   // lets the patch quit the app
    pd.computeAudio (true);

    patch = pd.openPatch ("_main.pd", patchDir.getFullPathName().toStdString());

    if (! patch.isValid())
    {
        oscOut.stop();   // its receiver must not outlive the Pd it is bound in
        pd.clear();
        deviceManager.closeAudioDevice();
        return juce::Result::fail ("libpd could not open "
                                   + patchDir.getChildFile ("_main.pd").getFullPathName());
    }

    // From here on the patch hands its GUI-bound OSC to oscOut rather than
    // sending it from inside Pd (_main.pd, O-S-C_&_FORMULS_SEND).
    if (appSendsOsc)
        pd.sendFloat ("formuls-app-osc", 1.0f);

    // Listen for the GUI's OSC before going live. A failure here is not
    // fatal -- the engine still makes sound, it just cannot be driven from
    // the GUI -- so it is reported rather than aborting the start.
    if (auto oscResult = oscBridge.start (pd, OpenStageControlProcess::patchOscPort);
        oscResult.failed())
        juce::Logger::writeToLog (oscResult.getErrorMessage());

    // -------------------------------------------------------------- go live
    startTimer (messagePumpIntervalMs);
    deviceManager.addAudioCallback (this);
    activeDeviceManager = &deviceManager;
    running = true;

    Diagnostics::log ("engine started: \"" + device->getName() + "\", "
                      + juce::String (device->getCurrentBufferSizeSamples()) + " samples @ "
                      + juce::String ((int) actualSampleRate) + " Hz, budget "
                      + juce::String (device->getCurrentBufferSizeSamples() * 1000.0 / actualSampleRate, 2)
                      + " ms per callback");   // TEMPORARY

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

    // Before the patch closes: the bridge sends into libpd, so it must not
    // be able to fire once there is nothing left to send to.
    oscBridge.stop();
    oscOut.stop();   // unbinds its receiver while Pd is still there

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
void FormulsEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto blockSize = libpd_blocksize();   // always 64 frames

    pdInputBuffer.assign ((size_t) blockSize, 0.0f);   // no input channels used
    pdOutputBuffer.assign ((size_t) (blockSize * juce::jmax (1, pdOutputChannels)), 0.0f);
    samplesLeftInTick = 0;

    // Allocate the recording tap here, on the message thread, generously
    // enough that a device buffer size change still fits without the audio
    // callback ever having to allocate.
    const auto deviceBufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    recordChannels.assign ((size_t) juce::jmax (1, pdOutputChannels), nullptr);
    recordSilence.assign ((size_t) juce::jmax (8192, deviceBufferSize * 4), 0.0f);
}

void FormulsEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    const auto blockSize = libpd_blocksize();
    const auto diagStartMs = juce::Time::getMillisecondCounterHiRes();   // TEMPORARY diagnostics
    float diagWorstTick = 0.0f;
    int diagTicks = 0, diagWorstTickIndex = -1;

    // libpd renders in fixed 64-frame "ticks" of interleaved audio, while the
    // device may ask for any buffer size. Render one tick at a time into
    // pdOutputBuffer and de-interleave it into JUCE's per-channel buffers,
    // carrying leftover frames across callbacks when sizes don't line up.
    int sampleIndex = 0;

    while (sampleIndex < numSamples)
    {
        if (samplesLeftInTick == 0)
        {
            // processFloat takes sys_lock: this includes any wait for the
            // message thread, as well as DSP and Pd clocks firing in the tick
            const auto tickStart = juce::Time::getMillisecondCounterHiRes();
            pd.processFloat (1, pdInputBuffer.data(), pdOutputBuffer.data());
            const auto tickMs = (float) (juce::Time::getMillisecondCounterHiRes() - tickStart);

            if (tickMs > diagWorstTick) { diagWorstTick = tickMs; diagWorstTickIndex = diagTicks; }
            ++diagTicks;
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

    // Compute peak levels for the first two channels (VU meter source).
    for (int ch = 0; ch < 2 && ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
        {
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (outputChannelData[ch][i]));
            peakLevels[(size_t) ch].store (peak, std::memory_order_relaxed);
        }
    }

    // The whole block is now rendered, so the recorder can take it in one
    // go. Device channels JUCE gave us as null are recorded as silence, so
    // the file always has exactly the engine's channel count.
    if (recorder != nullptr
        && pdOutputChannels > 0
        && (int) recordChannels.size() >= pdOutputChannels
        && (size_t) numSamples <= recordSilence.size())
    {
        for (int ch = 0; ch < pdOutputChannels; ++ch)
            recordChannels[(size_t) ch] = (ch < numOutputChannels && outputChannelData[ch] != nullptr)
                                            ? outputChannelData[ch]
                                            : recordSilence.data();

        recorder->write (recordChannels.data(), pdOutputChannels, numSamples);
    }

    // TEMPORARY diagnostics: no allocation, no locks, no I/O here.
    if (actualSampleRate > 0)
    {
        const auto tookMs = (float) (juce::Time::getMillisecondCounterHiRes() - diagStartMs);
        const auto budgetMs = (float) (numSamples * 1000.0 / actualSampleRate);

        diagCallbacks.fetch_add (1, std::memory_order_relaxed);
        if (tookMs > diagWorstMs.load (std::memory_order_relaxed)) diagWorstMs.store (tookMs, std::memory_order_relaxed);
        if (diagWorstTick > diagWorstTickMs.load (std::memory_order_relaxed)) diagWorstTickMs.store (diagWorstTick, std::memory_order_relaxed);

        if (tookMs > 0.5f * budgetMs)
        {
            diagOverHalf.fetch_add (1, std::memory_order_relaxed);
            if (tookMs >= budgetMs) diagMissed.fetch_add (1, std::memory_order_relaxed);

            const auto scope = slowFifo.write (1);
            if (scope.blockSize1 > 0)
                slowBlocks[(size_t) scope.startIndex1] = { diagStartMs, tookMs, budgetMs, diagWorstTick, diagTicks, diagWorstTickIndex };
            else
                diagFifoFull.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

//==============================================================================
void FormulsEngine::writeDiagnostics()
{
    // TEMPORARY: message thread. Slow blocks as they come, a summary every 10 s.
    const auto scope = slowFifo.read (slowFifo.getNumReady());

    auto write = [this] (int start, int count)
    {
        for (int i = start; i < start + count; ++i)
        {
            const auto& b = slowBlocks[(size_t) i];
            Diagnostics::log ("SLOW BLOCK  " + juce::String (b.tookMs, 2) + " ms of " + juce::String (b.budgetMs, 2)
                              + " ms (" + juce::String (juce::roundToInt (100.0f * b.tookMs / b.budgetMs)) + "%)"
                              + (b.tookMs >= b.budgetMs ? "  MISSED" : "")
                              + ", worst tick " + juce::String (b.worstTickMs, 2) + " ms (tick "
                              + juce::String (b.worstTickIndex + 1) + " of " + juce::String (b.ticks) + ")", b.atMs);
        }
    };
    write (scope.startIndex1, scope.blockSize1);
    write (scope.startIndex2, scope.blockSize2);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - diagLastSummaryMs >= 10000.0)
    {
        if (diagLastSummaryMs > 0.0)
            Diagnostics::log ("SUMMARY     " + juce::String (diagCallbacks.exchange (0)) + " callbacks, "
                              + juce::String (diagOverHalf.exchange (0)) + " over half budget, "
                              + juce::String (diagMissed.exchange (0)) + " missed, worst "
                              + juce::String (diagWorstMs.exchange (0.0f), 2) + " ms, worst tick "
                              + juce::String (diagWorstTickMs.exchange (0.0f), 2) + " ms"
                              + (diagFifoFull.load() ? ", " + juce::String (diagFifoFull.exchange (0)) + " slow blocks not logged" : juce::String())
                              + OscBridge::takeDiagnosticSummary()
                              + ", " + juce::String (oscOut.takeSentCount()) + " OSC packets sent by the app"
                              + [this] { const auto d = oscOut.takeDroppedCount(); return d ? ", " + juce::String (d) + " DROPPED" : juce::String(); }());
        diagLastSummaryMs = now;
    }
}

void FormulsEngine::audioDeviceStopped()
{
    samplesLeftInTick = 0;
    peakLevels[0].store (0.0f, std::memory_order_relaxed);
    peakLevels[1].store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void FormulsEngine::timerCallback()
{
    // Runs on the message thread: moves queued patch messages out of libpd's
    // ring buffer and into the receive* callbacks below.
    pd.receiveMessages();
    writeDiagnostics();   // TEMPORARY
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
