/*
 * SharedEngine.cpp -- see SharedEngine.h.
 */

#include "SharedEngine.h"
#include "OpenStageControlProcess.h"   // patchOscPort, getBrowserAddresses
#include "ResourceLocator.h"

#if JUCE_MAC || JUCE_LINUX
 #include <dlfcn.h>
#endif

namespace formuls
{

namespace
{
    std::mutex sharedLock;
    std::weak_ptr<SharedEngine> sharedInstance;

    // The diagnostics log. Hosts load a plugin into their own process, where
    // nothing else of ours can be read. Installed as JUCE's logger only if the
    // host has not set one, so the engine's and Pd's own messages (externals
    // loading, Link, errors) land there too.
    std::unique_ptr<juce::FileLogger> fileLogger;

    // A host loads the plugin with RTLD_LOCAL, so libpd -- loaded as the
    // plugin's dependency -- is not in the global symbol scope. The Faust and
    // abl_link~ externals are built against Pd's symbols with
    // `-undefined dynamic_lookup` and expect to find them there, as they do
    // in the app (where libpd is linked at launch). Re-opening the already
    // loaded libpd with RTLD_GLOBAL promotes it. Harmless if already global.
    void makeLibpdSymbolsGlobal()
    {
       #if JUCE_MAC || JUCE_LINUX
        Dl_info info {};
        if (dladdr ((const void*) &libpd_init, &info) != 0 && info.dli_fname != nullptr)
            if (dlopen (info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL) == nullptr)
                SharedEngine::log ("could not make libpd global: " + juce::String (dlerror()));
       #endif
    }
}

std::shared_ptr<SharedEngine> SharedEngine::acquire()
{
    const std::lock_guard<std::mutex> lock (sharedLock);
    auto engine = sharedInstance.lock();
    if (engine == nullptr)
    {
        engine.reset (new SharedEngine());
        sharedInstance = engine;
    }
    return engine;
}

SharedEngine::SharedEngine()
{
    if (juce::Logger::getCurrentLogger() == nullptr)
    {
        auto file = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Logs/formuls/plugin.log");
        fileLogger = std::make_unique<juce::FileLogger> (file, "formuls plugin log", 1024 * 1024);
        juce::Logger::setCurrentLogger (fileLogger.get());
    }

    makeLibpdSymbolsGlobal();
    status = "Waiting for the host to start audio.";
}

SharedEngine::~SharedEngine()
{
    stop();
    log ("engine released (no formuls instance left)");

    if (fileLogger != nullptr && juce::Logger::getCurrentLogger() == fileLogger.get())
        juce::Logger::setCurrentLogger (nullptr);
    fileLogger.reset();
}

void SharedEngine::log (const juce::String& message)
{
    juce::Logger::writeToLog (message);
}

//==============================================================================
void SharedEngine::prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::ScopedLock sl (lifecycleLock);

    // libpd runs at one rate for its whole life; a host rate change restarts it.
    if (engineRunning && sampleRate != runningSampleRate)
        stop();

    if (! engineRunning)
        if (auto result = start (sampleRate, maximumExpectedSamplesPerBlock); result.failed())
            status = result.getErrorMessage();
}

juce::Result SharedEngine::start (double sampleRate, int maxBlockSize)
{
    const auto resourceRoot = findResourceRoot();
    if (! resourceRoot.isDirectory())
        return juce::Result::fail ("formuls could not find its pd/ and gui/ resources "
                                   "inside the plugin bundle.");

    gui.setStateCapture (true);
    patchReady = false;
    engine.onGlobalReset = [this] (float value)
    {
        if (value == 0.0f && ! patchReady)   // the end of the start-up pulse
            patchIsReady();
    };
    engine.setDirectGui ([this] (const juce::uint8* data, int size)
                         { gui.receivePatchPacket (data, size); });

    if (auto result = engine.startHosted (numOutputChannels, sampleRate, maxBlockSize, resourceRoot);
        result.failed())
        return result;

    // The first free port from 9001 up, so formuls in two hosts at once (or
    // next to the app) still each get a GUI.
    const auto patchTarget = "127.0.0.1:" + juce::String (OpenStageControlProcess::patchOscPort);
    juce::Result guiResult = juce::Result::fail ("no port");

    for (int port = firstGuiPort; port <= lastGuiPort; ++port)
    {
        guiResult = gui.start (resourceRoot, port, patchTarget,
                               [this] (const juce::OSCMessage& m) { engine.sendToPatch (m); });
        if (guiResult.wasOk())
        {
            guiPort = port;
            break;
        }
    }

    runningSampleRate = sampleRate;
    engineRunning = true;
    startTimer (patchReadyFallbackMs);

    status = guiResult.wasOk()
                 ? "Running at " + juce::String ((int) sampleRate) + " Hz."
                 : "Running, but the control GUI could not start: " + guiResult.getErrorMessage();
    log ("engine " + status);
    return juce::Result::ok();
}

void SharedEngine::stop()
{
    if (! engineRunning)
        return;

    // Another instance may be inside render() on the audio thread: close the
    // door, then wait for it to leave before the engine goes away.
    engineRunning = false;
    for (int i = 0; i < 200 && rendersInFlight.load() > 0; ++i)
        juce::Thread::sleep (1);

    stopTimer();
    patchReady = false;
    engine.onGlobalReset = nullptr;
    {
        // Keep what the tablets did, so a restart (sample rate change) restores it.
        const juce::ScopedLock sl (stateLock);
        if (auto current = gui.getStateJson(); current.isNotEmpty())
            pendingGuiState = current;
        if (auto current = gui.getPatchStateJson(); current.isNotEmpty())
            pendingPatchState = current;
    }
    gui.stop();
    engine.stop();
    guiPort = 0;
}

//==============================================================================
bool SharedEngine::render (const void* instance, juce::AudioBuffer<float>& buffer) noexcept
{
    const auto now = juce::Time::getMillisecondCounter();
    auto current = renderer.load();

    if (current != instance)
    {
        // Someone else is playing: stay silent. Gone quiet: take over.
        if (current != nullptr && now - lastRenderMs.load() < handoverMs)
            return false;
        if (! renderer.compare_exchange_strong (current, instance))
            return false;
    }

    lastRenderMs = now;

    ++rendersInFlight;
    const bool rendered = engineRunning.load();
    if (rendered)
        engine.processHosted (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
    --rendersInFlight;
    return rendered;
}

void SharedEngine::forget (const void* instance) noexcept
{
    auto expected = instance;
    renderer.compare_exchange_strong (expected, nullptr);
}

bool SharedEngine::isAnotherInstancePlaying (const void* instance) const noexcept
{
    const auto current = renderer.load();
    return current != nullptr && current != instance
        && juce::Time::getMillisecondCounter() - lastRenderMs.load() < handoverMs;
}

//==============================================================================
void SharedEngine::patchIsReady()
{
    stopTimer();
    if (patchReady)
        return;
    patchReady = true;
    log ("patch ready (start-up reset done)");

    const juce::ScopedLock sl (stateLock);
    if (gui.isRunning() && (pendingGuiState.isNotEmpty() || pendingPatchState.isNotEmpty()))
        gui.setState (pendingGuiState, pendingPatchState);
}

void SharedEngine::getState (juce::MemoryBlock& destData)
{
    juce::String guiJson, patchJson;
    {
        const juce::ScopedLock sl (stateLock);
        // Until a restored project has been applied, it is still the state.
        const bool restorePending = ! patchReady && (pendingGuiState.isNotEmpty() || pendingPatchState.isNotEmpty());
        guiJson   = restorePending ? pendingGuiState   : gui.getStateJson();
        patchJson = restorePending ? pendingPatchState : gui.getPatchStateJson();
        if (guiJson.isEmpty())   guiJson = pendingGuiState;
        if (patchJson.isEmpty()) patchJson = pendingPatchState;
    }

    juce::ValueTree tree ("formuls");
    tree.setProperty ("version", 1, nullptr);
    tree.setProperty ("guiState", guiJson, nullptr);
    tree.setProperty ("patchState", patchJson, nullptr);

    juce::MemoryOutputStream out (destData, false);
    tree.writeToStream (out);
}

void SharedEngine::setState (const void* data, int sizeInBytes)
{
    const auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! tree.hasType ("formuls"))
        return;

    const auto guiJson = tree["guiState"].toString();
    const auto patchJson = tree["patchState"].toString();
    if (guiJson.isEmpty() && patchJson.isEmpty())
        return;

    const juce::ScopedLock sl (stateLock);
    pendingGuiState = guiJson;
    pendingPatchState = patchJson;

    // Hosts call this on the message thread; the server must be told there.
    // Before the patch is ready it waits for patchIsReady().
    if (engineRunning && gui.isRunning() && patchReady)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            gui.setState (guiJson, patchJson);
        else
            juce::MessageManager::callAsync ([weak = std::weak_ptr<SharedEngine> (sharedInstance), guiJson, patchJson]
            {
                if (auto engine = weak.lock())
                    engine->gui.setState (guiJson, patchJson);
            });
    }
}

juce::String SharedEngine::getStatus() const
{
    return status;
}

juce::StringArray SharedEngine::getGuiAddresses() const
{
    if (guiPort == 0 || ! gui.isRunning())
        return {};
    return OpenStageControlProcess::getBrowserAddresses (guiPort);
}

} // namespace formuls
