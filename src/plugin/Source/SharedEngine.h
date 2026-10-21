/*
 * SharedEngine.h
 *
 * The one formuls engine of a host process, shared by every plugin instance
 * in it.
 *
 * libpd keeps its state in globals, so a process can run one Pd. The first
 * version gave it to whichever instance was created first and left any other
 * silent. That breaks in hosts that create an instance, keep it alive, and
 * then create the one they actually play: auval does exactly that, and so
 * (judging by the symptoms) does SoundDesk. The instance the host played was
 * silent, and the tablet's GUI belonged to the hidden one.
 *
 * Now the engine and its GUI server belong to the process. Each block, the
 * instance the host is running renders it: if the instance that rendered last
 * has not been called for `handoverMs`, the next one to be called takes over.
 * An instance the host keeps alive but never processes costs nothing. Two
 * instances that really are both playing cannot each have a formuls; the
 * second one stays silent and says so.
 *
 * The project state (GUI + patch, see EmbeddedGuiServer) is the engine's, so
 * any instance can save or restore it. It is switched off for now
 * (saveProjectState): formuls always starts from its defaults.
 */

#pragma once

#include <JuceHeader.h>
#include "FormulsEngine.h"
#include "EmbeddedGuiServer.h"

namespace formuls
{

class SharedEngine : private juce::Timer
{
public:
    /** The process's engine, created by the first caller. Message thread. */
    static std::shared_ptr<SharedEngine> acquire();
    ~SharedEngine() override;

    /** Message thread (prepareToPlay). Starts the engine, or restarts it if
        the sample rate changed. */
    void prepare (double sampleRate, int maximumExpectedSamplesPerBlock);

    /** Audio thread. Renders into `buffer` if `instance` is the one playing,
        taking over from an instance that has gone quiet; leaves the buffer
        untouched (silent, cleared by the caller) otherwise. The playing
        instance's MIDI note-ons go to the patch (FormulsEngine::queueNoteOn). */
    bool render (const void* instance, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

    /** An instance going away: if it was the one playing, nobody is. */
    void forget (const void* instance) noexcept;

    /** True if another instance than `instance` rendered in the last `handoverMs`. */
    bool isAnotherInstancePlaying (const void* instance) const noexcept;

    void getState (juce::MemoryBlock&);
    void setState (const void* data, int sizeInBytes);

    juce::String getStatus() const;
    juce::StringArray getGuiAddresses() const;
    int getGuiPort() const noexcept   { return guiPort; }
    bool isRunning() const noexcept   { return engineRunning; }
    bool isPatchReady() const noexcept   { return patchReady; }

    /** Appends to plugin.log (via JUCE's logger): ~/Library/Logs/formuls on macOS,
        ~/.config/formuls on Linux, %APPDATA%\formuls on Windows. */
    static void log (const juce::String& message);

    static constexpr int numOutputChannels = 2;
    static constexpr int firstGuiPort = 9001, lastGuiPort = 9020;
    static constexpr juce::uint32 handoverMs = 250;

    /** Saving the GUI and patch state with the host project, and restoring it.
        Off for now: it does not restore reliably yet. Off, formuls stores
        nothing in the project, ignores anything an older version stored, and
        every load starts from the defaults the patch's start-up reset sets. */
    static constexpr bool saveProjectState = false;

private:
    SharedEngine();

    juce::Result start (double sampleRate, int maxBlockSize);
    void stop();

    // A restored project is applied once the patch has finished its own
    // start-up reset (FormulsEngine::resetReceiverName), or it would be reset
    // straight away. timerCallback is the fallback if that never comes.
    void patchIsReady();
    void timerCallback() override   { patchIsReady(); }
    static constexpr int patchReadyFallbackMs = 10000;

    // Declared before the engine so it outlives it (the engine's PdOscOut
    // thread hands it packets until the engine stops).
    EmbeddedGuiServer gui;
    FormulsEngine engine;

    juce::CriticalSection lifecycleLock;   // prepare() from several instances
    std::atomic<bool> engineRunning { false };
    std::atomic<int> rendersInFlight { 0 };
    bool patchReady = false;               // message thread
    double runningSampleRate = 0;
    int guiPort = 0;
    juce::String status;

    std::atomic<const void*> renderer { nullptr };
    std::atomic<juce::uint32> lastRenderMs { 0 };

    juce::String pendingGuiState, pendingPatchState;
    juce::CriticalSection stateLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedEngine)
};

} // namespace formuls
