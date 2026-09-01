/*
 * Engine.h -- the formuls audio engine, à la carte edition.
 *
 * The same design as the JUCE app's FormulsEngine (libpd runs in-process,
 * queued messages, the patch can quit the app via the "formuls-quit"
 * receive symbol) but with RtAudio doing the device I/O instead of
 * juce::AudioDeviceManager. RtAudio is the library the original standalone
 * formulsengine used, restored from this repository's git history.
 *
 * Differences from the JUCE engine worth knowing about:
 *
 *   - RtAudio delivers interleaved buffers, which is exactly what libpd
 *     wants, so there is no de-interleaving step at all.
 *   - Devices are addressed by RtAudio device index (the UI keeps the
 *     index/name pairs from listOutputDevices()).
 *   - If the device does not support the requested sample rate, the nearest
 *     supported rate is chosen before the stream opens;
 *     actualSampleRate() reports what was really used, and libpd is always
 *     initialised at that rate.
 *
 * Threading: DSP runs on RtAudio's callback thread; messages out of the
 * patch land in libpd's lock-free ring buffer and are delivered on the main
 * thread by receiveMessages(), which main.mm calls from an NSTimer. All
 * other libpd calls happen while the stream is closed.
 */

#pragma once

#include "PdBase.hpp"
#include "PerformanceMeter.h"
#include "RtAudio.h"

#include <chrono>
#include <filesystem>
#include <set>
#include <functional>
#include <string>
#include <vector>

namespace formuls
{

class Engine : private pd::PdReceiver
{
public:
    Engine() = default;
    ~Engine() override;

    /** Receive symbol the Pd patch sends to when it wants the app to quit. */
    static constexpr const char* quitReceiverName = "formuls-quit";

    /** Called (from receiveMessages, i.e. the main thread) when the patch
        sends to "formuls-quit". */
    std::function<void()> onQuitRequested;

    struct Device
    {
        unsigned int rtIndex;        // RtAudio device index
        std::string name;
        unsigned int maxOutputChannels;
    };

    /** Output-capable audio devices, for populating the device menu. */
    static std::vector<Device> listOutputDevices();

    /** Opens the device, boots libpd and loads pd/_main.pd from
        resourceRoot. Returns "" on success or a user-displayable error. */
    std::string start (unsigned int rtDeviceIndex,
                       int numOutputChannels,
                       int sampleRate,
                       const std::filesystem::path& resourceRoot);

    /** Stops audio, closes the patch and releases the device.
        Safe to call when not running. */
    void stop();

    bool isRunning() const noexcept       { return running; }

    /** The rate the stream really opened at (0 when stopped); may differ
        from the rate passed to start() if the device can't provide it. */
    int actualSampleRate() const noexcept { return streamSampleRate; }

    /** Live performance figures. The meter is shared, identical code with
        the JUCE build (src/shared/PerformanceMeter.h) so the two apps'
        numbers can be compared directly. */
    PerformanceMeter& getMeter() noexcept { return meter; }

    /** Drains queued patch->app messages; call regularly on the main
        thread (main.mm uses a 30 ms NSTimer). A no-op while stopped: libpd's
        queued ring buffer only exists between start() and stop(), and
        PdBase::receiveMessages() crashes -- it warns but does not return --
        if called before the queued interface is initialised. */
    void receiveMessages()                { if (running) pd.receiveMessages(); }

private:
    static int audioCallback (void* outputBuffer, void* inputBuffer,
                              unsigned int numFrames, double streamTime,
                              RtAudioStreamStatus status, void* userData);
    void render (float* output, unsigned int numFrames);

    // pd::PdReceiver
    void print (const std::string& message) override;
    void receiveBang (const std::string& dest) override;
    void receiveFloat (const std::string& dest, float value) override;
    void receiveSymbol (const std::string& dest, const std::string& symbol) override;
    void receiveList (const std::string& dest, const pd::List& list) override;
    void receiveMessage (const std::string& dest, const std::string& msg,
                         const pd::List& list) override;

    void handleQuitRequest();
    void logTrace (const std::string& dest, const std::string& value);

    pd::PdBase pd;
    pd::Patch patch;
    RtAudio audio;

    int pdOutputChannels = 0;
    int streamSampleRate = 0;
    std::vector<float> pdInputBuffer;    // dummy (0 input channels)
    std::vector<float> pdOutputBuffer;   // one Pd tick, interleaved
    unsigned int samplesLeftInTick = 0;
    PerformanceMeter meter;

    // FORMULS_TRACE diagnostics (see Engine.cpp)
    std::set<std::string> tracedSymbols;
    std::chrono::steady_clock::time_point traceStart;

    bool running = false;
};

} // namespace formuls
