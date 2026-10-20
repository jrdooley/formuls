/*
 * PluginProcessor.h
 *
 * formuls as an instrument plugin (VST3 / AU). The plugin runs the same
 * FormulsEngine (libpd running pd/_main.pd) and EmbeddedGuiServer as the
 * standalone app, but the host drives the audio: there is no device, no
 * recorder and no node process. The control GUI is served by the plugin
 * itself and opened in a browser or on a tablet, exactly as with the app;
 * the plugin's own window just says where.
 *
 * The engine belongs to the host process, not to an instance (SharedEngine):
 * whichever instance the host actually plays renders it.
 *
 * Limits (see docs/plugin/README.md):
 *   - one formuls per host process: two instances that are both playing
 *     cannot each have one; the second stays silent and says so;
 *   - the engine always runs 2 output channels;
 *   - no MIDI or host automation yet.
 */

#pragma once

#include <JuceHeader.h>
#include "SharedEngine.h"

namespace formuls
{

class FormulsPluginProcessor : public juce::AudioProcessor
{
public:
    FormulsPluginProcessor();
    ~FormulsPluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override   { return true; }

    const juce::String getName() const override   { return JucePlugin_Name; }
    bool acceptsMidi() const override    { return true; }
    bool producesMidi() const override   { return false; }
    bool isMidiEffect() const override   { return false; }
    double getTailLengthSeconds() const override   { return 0.0; }

    int getNumPrograms() override   { return 1; }
    int getCurrentProgram() override   { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // The GUI and patch state (see EmbeddedGuiServer) are saved with the host
    // project and restored on load, tablet or not.
    void getStateInformation (juce::MemoryBlock& destData) override   { engine->getState (destData); }
    void setStateInformation (const void* data, int size) override    { engine->setState (data, size); }

    // For the editor (message thread).
    juce::String getStatus() const;
    juce::StringArray getGuiAddresses() const   { return engine->getGuiAddresses(); }

private:
    std::shared_ptr<SharedEngine> engine;

    // Diagnostics for this instance, in ~/Library/Logs/formuls/plugin.log.
    void log (const juce::String& message) const;
    void logStats();
    const int instanceNumber;
    std::atomic<juce::int64> blocksProcessed { 0 }, blocksRendered { 0 };
    std::atomic<int> lastNumChannels { 0 }, lastNumSamples { 0 };
    std::atomic<float> peakSinceLastLog { 0.0f };
    juce::int64 processedAtLastLog = 0, renderedAtLastLog = 0;
    struct StatsTimer : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override   { tick(); }
    } statsTimer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormulsPluginProcessor)
};

} // namespace formuls
