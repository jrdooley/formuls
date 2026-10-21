/*
 * SharedEngine.cpp -- see SharedEngine.h.
 */

#include "SharedEngine.h"
#include "OpenStageControlProcess.h"   // patchOscPort, getBrowserAddresses
#include "ResourceLocator.h"

#if JUCE_MAC || JUCE_LINUX
 #include <dlfcn.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
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
    // Windows resolves a DLL's imports from the host's folder, not the
    // plugin's, so the plugin delay-loads libpd.dll (/DELAYLOAD in the .jucer)
    // and loads its own copy, next to formuls.vst3's binary, by full path
    // before anything calls libpd. LOAD_WITH_ALTERED_SEARCH_PATH finds
    // pthreadVC3.dll beside it. The externals import "libpd.dll" by name, and
    // Windows hands them the module of that name already loaded: this one.
    // Returns an error to log once the logger is up, or an empty string.
    juce::String loadBundledLibpd()
    {
       #if JUCE_WINDOWS
        if (GetModuleHandleW (L"libpd.dll") != nullptr)
            return {};

        const auto dll = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getSiblingFile ("libpd.dll");
        if (LoadLibraryExW (dll.getFullPathName().toWideCharPointer(), nullptr,
                            LOAD_WITH_ALTERED_SEARCH_PATH) == nullptr)
            return "could not load " + dll.getFullPathName()
                   + " (Windows error " + juce::String ((int) GetLastError()) + ")";
       #endif
        return {};
    }

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
        // Before the engine: its pd::PdBase member calls libpd_init().
        const auto libpdError = loadBundledLibpd();
        engine.reset (new SharedEngine());
        if (libpdError.isNotEmpty())
            log (libpdError);
        sharedInstance = engine;
    }
    return engine;
}

SharedEngine::SharedEngine()
{
    if (juce::Logger::getCurrentLogger() == nullptr)
    {
       #if JUCE_MAC
        auto file = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Logs/formuls/plugin.log");
       #else
        // ~/.config/formuls on Linux, %APPDATA%\formuls on Windows.
        auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("formuls/plugin.log");
       #endif
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

    gui.setStateCapture (saveProjectState);
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
    if (saveProjectState)
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
bool SharedEngine::render (const void* instance, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
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
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())   // velocity 0 counts as a note-off
                engine.queueNoteOn (message.getChannel() - 1, message.getNoteNumber(), message.getVelocity());
        }
        engine.processHosted (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
    }
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
    if (! saveProjectState)
        return;   // nothing stored in the project

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
    if (! saveProjectState)
    {
        log ("ignoring the state stored in the project (project state is switched off)");
        return;
    }

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
