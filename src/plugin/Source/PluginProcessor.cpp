/*
 * PluginProcessor.cpp -- see PluginProcessor.h.
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "OpenStageControlProcess.h"   // patchOscPort, getBrowserAddresses
#include "ResourceLocator.h"

#if JUCE_MAC || JUCE_LINUX
 #include <dlfcn.h>
#endif

namespace formuls
{

namespace
{
    // One formuls per process: libpd keeps its state in globals.
    std::atomic<bool> instanceSlotTaken { false };

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
        {
            if (dlopen (info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL) == nullptr)
                juce::Logger::writeToLog ("formuls plugin: could not make libpd global: "
                                          + juce::String (dlerror()));
        }
       #endif
    }
}

FormulsPluginProcessor::FormulsPluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    bool expected = false;
    ownsTheInstanceSlot = instanceSlotTaken.compare_exchange_strong (expected, true);

    if (ownsTheInstanceSlot)
    {
        makeLibpdSymbolsGlobal();
        status = "Waiting for the host to start audio.";
    }
    else
    {
        status = "Another formuls is already running in this session. "
                 "Only one instance is supported for now; this one stays silent.";
    }
}

FormulsPluginProcessor::~FormulsPluginProcessor()
{
    stopEngine();

    if (ownsTheInstanceSlot)
        instanceSlotTaken = false;
}

bool FormulsPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void FormulsPluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (! ownsTheInstanceSlot)
        return;

    // libpd runs at one rate for its whole life; a host rate change restarts it.
    if (engineRunning && sampleRate != runningSampleRate)
        stopEngine();

    if (! engineRunning)
        if (auto result = startEngine (sampleRate, maximumExpectedSamplesPerBlock); result.failed())
            status = result.getErrorMessage();
}

juce::Result FormulsPluginProcessor::startEngine (double sampleRate, int maxBlockSize)
{
    const auto resourceRoot = findResourceRoot();
    if (! resourceRoot.isDirectory())
        return juce::Result::fail ("formuls could not find its pd/ and gui/ resources "
                                   "inside the plugin bundle.");

    gui.setStateCapture (true);
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

    {
        const juce::ScopedLock sl (stateLock);
        if (guiResult.wasOk() && (pendingGuiState.isNotEmpty() || pendingPatchState.isNotEmpty()))
            gui.setState (pendingGuiState, pendingPatchState);
    }

    status = guiResult.wasOk()
                 ? "Running at " + juce::String ((int) sampleRate) + " Hz."
                 : "Running, but the control GUI could not start: " + guiResult.getErrorMessage();
    return juce::Result::ok();
}

void FormulsPluginProcessor::stopEngine()
{
    // Not while processBlock() can run: hosts call prepareToPlay() and the
    // destructor with processing stopped.
    engineRunning = false;
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

void FormulsPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (engineRunning)
        engine.processHosted (buffer.getArrayOfWritePointers(),
                              buffer.getNumChannels(), buffer.getNumSamples());
}

//==============================================================================
void FormulsPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::String guiJson, patchJson;
    {
        const juce::ScopedLock sl (stateLock);
        guiJson = gui.getStateJson();
        patchJson = gui.getPatchStateJson();
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

void FormulsPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    if (engineRunning && gui.isRunning())
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            gui.setState (guiJson, patchJson);
        else
            juce::MessageManager::callAsync ([this, guiJson, patchJson] { gui.setState (guiJson, patchJson); });
    }
}

//==============================================================================
juce::String FormulsPluginProcessor::getStatus() const
{
    return status;
}

juce::StringArray FormulsPluginProcessor::getGuiAddresses() const
{
    if (guiPort == 0 || ! gui.isRunning())
        return {};
    return OpenStageControlProcess::getBrowserAddresses (guiPort);
}

juce::AudioProcessorEditor* FormulsPluginProcessor::createEditor()
{
    return new FormulsPluginEditor (*this);
}

} // namespace formuls

// JUCE's plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new formuls::FormulsPluginProcessor();
}
