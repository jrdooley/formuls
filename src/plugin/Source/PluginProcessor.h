/*
 * PluginProcessor.h
 *
 * formuls as an instrument plugin (VST3 / AU). The plugin hosts the same
 * FormulsEngine (libpd running pd/_main.pd) and EmbeddedGuiServer as the
 * standalone app, but the host drives the audio: there is no device, no
 * recorder and no node process. The control GUI is served by the plugin
 * itself and opened in a browser or on a tablet, exactly as with the app;
 * the plugin's own window just says where.
 *
 * Limits of this first version (see docs/plugin/README.md):
 *   - one formuls per host process. libpd is built single-instance, and the
 *     Faust and abl_link~ externals are not built for multi-instance Pd, so a
 *     second instance stays silent and says why;
 *   - the engine always runs 2 output channels;
 *   - the GUI state is not yet saved with the host project;
 *   - no MIDI or host automation yet.
 */

#pragma once

#include <JuceHeader.h>
#include "FormulsEngine.h"
#include "EmbeddedGuiServer.h"

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

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // For the editor (message thread).
    juce::String getStatus() const;
    juce::StringArray getGuiAddresses() const;

    static constexpr int numOutputChannels = 2;
    static constexpr int firstGuiPort = 9001, lastGuiPort = 9020;

private:
    juce::Result startEngine (double sampleRate, int maxBlockSize);
    void stopEngine();

    // Declared before the engine so it outlives it (the engine's PdOscOut
    // thread hands it packets until the engine stops).
    EmbeddedGuiServer gui;
    FormulsEngine engine;

    bool ownsTheInstanceSlot = false;
    std::atomic<bool> engineRunning { false };
    double runningSampleRate = 0;
    int guiPort = 0;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormulsPluginProcessor)
};

} // namespace formuls
