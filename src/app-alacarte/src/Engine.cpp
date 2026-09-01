/*
 * Engine.cpp -- see Engine.h for an overview.
 */

#include "Engine.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace formuls
{

static constexpr unsigned int preferredBufferFrames = 512;

Engine::~Engine()
{
    stop();
}

//==============================================================================
std::vector<Engine::Device> Engine::listOutputDevices()
{
    std::vector<Device> devices;
    RtAudio probe;

    for (unsigned int i = 0; i < probe.getDeviceCount(); ++i)
    {
        try
        {
            auto info = probe.getDeviceInfo (i);

            if (info.probed && info.outputChannels > 0)
                devices.push_back ({ i, info.name, info.outputChannels });
        }
        catch (RtAudioError&) {}   // skip devices that fail to probe
    }

    return devices;
}

std::string Engine::start (unsigned int rtDeviceIndex,
                           int numOutputChannels,
                           int sampleRate,
                           const std::filesystem::path& resourceRoot)
{
    if (running)
        return {};

    const auto patchDir = resourceRoot / "pd";

    if (! std::filesystem::is_regular_file (patchDir / "_main.pd"))
        return "Could not find pd/_main.pd in " + resourceRoot.string();

    // ------------------------------------------------- choose the sample rate
    // If the device can't do the requested rate, use the nearest one it can,
    // and always initialise libpd at whatever the stream really runs at.
    RtAudio::DeviceInfo info;

    try
    {
        info = audio.getDeviceInfo (rtDeviceIndex);
    }
    catch (RtAudioError& e)
    {
        return "Could not query audio device: " + e.getMessage();
    }

    if ((int) info.outputChannels < numOutputChannels)
        return "\"" + info.name + "\" has only "
             + std::to_string (info.outputChannels) + " output channels ("
             + std::to_string (numOutputChannels) + " requested)";

    int chosenRate = sampleRate;

    if (! info.sampleRates.empty()
        && std::find (info.sampleRates.begin(), info.sampleRates.end(),
                      (unsigned int) sampleRate) == info.sampleRates.end())
    {
        chosenRate = (int) info.sampleRates.front();

        for (auto candidate : info.sampleRates)
            if (std::abs ((int) candidate - sampleRate)
                    < std::abs (chosenRate - sampleRate))
                chosenRate = (int) candidate;
    }

    // ------------------------------------------------------------- boot libpd
    // queued = true: patch -> app messages go through a lock-free ring
    // buffer, drained on the main thread by receiveMessages().
    if (! pd.init (0, numOutputChannels, chosenRate, true))
        return "Could not initialise libpd";

    pdOutputChannels = numOutputChannels;
    streamSampleRate = chosenRate;

    pd.setReceiver (this);
    pd.subscribe (quitReceiverName);   // lets the patch quit the app

    // Diagnostics: FORMULS_TRACE=sym1,sym2 subscribes to those Pd send
    // symbols and timestamps everything they carry, so a value that is
    // being fought over between the patch and the GUI shows up as an
    // obvious alternation in the log. See src/tools/README.md.
    traceStart = std::chrono::steady_clock::now();
    if (const char* trace = std::getenv ("FORMULS_TRACE"))
    {
        std::string list (trace), sym;
        std::istringstream stream (list);

        while (std::getline (stream, sym, ','))
        {
            if (sym.empty())
                continue;

            pd.subscribe (sym);
            tracedSymbols.insert (sym);
            std::fprintf (stderr, "trace: watching \"%s\"\n", sym.c_str());
        }
    }
    pd.computeAudio (true);

    patch = pd.openPatch ("_main.pd", patchDir.string());

    if (! patch.isValid())
    {
        pd.clear();
        return "libpd could not open " + (patchDir / "_main.pd").string();
    }

    // -------------------------------------------------------- open the stream
    RtAudio::StreamParameters params;
    params.deviceId = rtDeviceIndex;
    params.nChannels = (unsigned int) numOutputChannels;

    RtAudio::StreamOptions options;
    options.streamName = "formuls";
    options.flags = RTAUDIO_SCHEDULE_REALTIME;

    unsigned int bufferFrames = preferredBufferFrames;

    const auto blockSize = (size_t) libpd_blocksize();
    pdInputBuffer.assign (blockSize, 0.0f);
    pdOutputBuffer.assign (blockSize * (size_t) numOutputChannels, 0.0f);
    samplesLeftInTick = 0;

    try
    {
        audio.openStream (&params, nullptr, RTAUDIO_FLOAT32,
                          (unsigned int) chosenRate, &bufferFrames,
                          &Engine::audioCallback, this, &options);
        audio.startStream();
    }
    catch (RtAudioError& e)
    {
        if (audio.isStreamOpen())
            audio.closeStream();

        pd.closePatch (patch);
        patch.clear();
        pd.clear();
        return "Could not open audio stream: " + e.getMessage();
    }

    running = true;

    std::fprintf (stderr, "formuls engine started: \"%s\", %d channels @ %d Hz\n",
                  info.name.c_str(), numOutputChannels, chosenRate);
    return {};
}

void Engine::stop()
{
    if (! running)
        return;

    running = false;

    try
    {
        if (audio.isStreamRunning())
            audio.stopStream();
    }
    catch (RtAudioError&) {}

    if (audio.isStreamOpen())
        audio.closeStream();

    if (patch.isValid())
    {
        pd.closePatch (patch);
        patch.clear();
    }

    pd.clear();   // stops DSP, detaches hooks, unsubscribes all receivers
    streamSampleRate = 0;
    meter.reset();

    std::fprintf (stderr, "formuls engine stopped\n");
}

//==============================================================================
int Engine::audioCallback (void* outputBuffer, void*, unsigned int numFrames,
                           double, RtAudioStreamStatus status, void* userData)
{
    auto* self = static_cast<Engine*> (userData);

    // RtAudio tells us when the device actually ran dry. The UI's "missed"
    // counter deliberately uses the same self-timed definition as the JUCE
    // build so the two apps' readouts mean the same thing, so report the
    // hardware's own view on the console instead of mixing it in.
    if ((status & RTAUDIO_OUTPUT_UNDERFLOW) != 0)
        fprintf (stderr, "audio: output underflow (device ran dry)\n");

    const auto renderStarted = self->meter.beginRender();
    self->render (static_cast<float*> (outputBuffer), numFrames);
    self->meter.endRender (renderStarted, (int) numFrames,
                           (double) self->streamSampleRate);
    return 0;
}

void Engine::render (float* output, unsigned int numFrames)
{
    const auto blockSize = (unsigned int) libpd_blocksize();   // 64 frames
    const auto channels  = (unsigned int) pdOutputChannels;

    // Fast path: RtAudio buffers are interleaved, exactly what libpd wants,
    // so when the buffer is a whole number of Pd ticks, render straight in.
    if (samplesLeftInTick == 0 && numFrames % blockSize == 0)
    {
        pd.processFloat ((int) (numFrames / blockSize),
                         pdInputBuffer.data(), output);
        return;
    }

    // General path: render one 64-frame tick at a time and copy interleaved
    // chunks, carrying leftover frames across callbacks.
    unsigned int frameIndex = 0;

    while (frameIndex < numFrames)
    {
        if (samplesLeftInTick == 0)
        {
            pd.processFloat (1, pdInputBuffer.data(), pdOutputBuffer.data());
            samplesLeftInTick = blockSize;
        }

        const auto tickOffset = blockSize - samplesLeftInTick;
        const auto numToCopy  = std::min (samplesLeftInTick, numFrames - frameIndex);

        std::memcpy (output + (size_t) frameIndex * channels,
                     pdOutputBuffer.data() + (size_t) tickOffset * channels,
                     (size_t) numToCopy * channels * sizeof (float));

        samplesLeftInTick -= numToCopy;
        frameIndex += numToCopy;
    }
}

//==============================================================================
void Engine::logTrace (const std::string& dest, const std::string& value)
{
    if (tracedSymbols.count (dest) == 0)
        return;

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now() - traceStart).count();
    std::fprintf (stderr, "trace %8lld ms  %-24s %s\n",
                  (long long) ms, dest.c_str(), value.c_str());
}

void Engine::handleQuitRequest()
{
    std::fprintf (stderr, "quit requested by the Pd patch\n");

    if (onQuitRequested)
        onQuitRequested();
}

void Engine::print (const std::string& message)
{
    std::fprintf (stderr, "pd: %s\n", message.c_str());
}

void Engine::receiveBang (const std::string& dest)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void Engine::receiveFloat (const std::string& dest, float value)
{
    logTrace (dest, std::to_string (value));

    if (dest == quitReceiverName)
        handleQuitRequest();
}

void Engine::receiveSymbol (const std::string& dest, const std::string&)
{
    if (dest == quitReceiverName)
        handleQuitRequest();
}

void Engine::receiveList (const std::string& dest, const pd::List& list)
{
    logTrace (dest, list.toString());

    if (dest == quitReceiverName)
        handleQuitRequest();
}

void Engine::receiveMessage (const std::string& dest, const std::string& msg,
                             const pd::List& list)
{
    logTrace (dest, msg + " " + list.toString());

    if (dest == quitReceiverName)
        handleQuitRequest();
}

} // namespace formuls
