/*
 * PluginProcessor.cpp -- see PluginProcessor.h.
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
#endif

namespace formuls
{

namespace
{
    std::atomic<int> instancesCreated { 0 };
}

FormulsPluginProcessor::FormulsPluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      engine (SharedEngine::acquire()),
      instanceNumber (++instancesCreated)
{
    const auto format = juce::PluginHostType::getPluginLoadedAs();
    log ("created in " + juce::String (juce::PluginHostType().getHostDescription())
         + " (" + (format == AudioProcessor::wrapperType_AudioUnit ? "AU" :
                   format == AudioProcessor::wrapperType_VST3 ? "VST3" : "other")
         + ", pid " + juce::String ((int) getpid()) + ")");

    statsTimer.tick = [this] { logStats(); };
    statsTimer.startTimer (5000);
}

FormulsPluginProcessor::~FormulsPluginProcessor()
{
    statsTimer.stopTimer();
    logStats();
    engine->forget (this);
    log ("destroyed");
}

void FormulsPluginProcessor::log (const juce::String& message) const
{
    SharedEngine::log ("[formuls #" + juce::String (instanceNumber) + "] " + message);
}

void FormulsPluginProcessor::logStats()
{
    const auto processed = blocksProcessed.load(), rendered = blocksRendered.load();
    log ("audio: " + juce::String (processed - processedAtLastLog) + " blocks called, "
         + juce::String (rendered - renderedAtLastLog) + " rendered by this instance in the last interval, "
         + juce::String (lastNumChannels.load()) + " ch x " + juce::String (lastNumSamples.load())
         + " samples, peak " + juce::String (peakSinceLastLog.exchange (0.0f), 4)
         + "; engine " + (engine->isRunning() ? "running" : "stopped")
         + ", patch " + (engine->isPatchReady() ? "ready" : "not ready")
         + ", GUI port " + juce::String (engine->getGuiPort()) + "; " + getStatus());
    processedAtLastLog = processed;
    renderedAtLastLog = rendered;
}

bool FormulsPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void FormulsPluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    log ("prepareToPlay " + juce::String (sampleRate) + " Hz, up to " + juce::String (maximumExpectedSamplesPerBlock)
         + " samples, " + juce::String (getTotalNumInputChannels()) + " in / "
         + juce::String (getTotalNumOutputChannels()) + " out");

    engine->prepare (sampleRate, maximumExpectedSamplesPerBlock);
}

void FormulsPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    ++blocksProcessed;
    lastNumChannels.store (buffer.getNumChannels(), std::memory_order_relaxed);
    lastNumSamples.store (buffer.getNumSamples(), std::memory_order_relaxed);

    if (engine->render (this, buffer))
    {
        ++blocksRendered;
        const auto peak = buffer.getMagnitude (0, buffer.getNumSamples());
        if (peak > peakSinceLastLog.load (std::memory_order_relaxed))
            peakSinceLastLog.store (peak, std::memory_order_relaxed);
    }
}

//==============================================================================
juce::String FormulsPluginProcessor::getStatus() const
{
    if (engine->isAnotherInstancePlaying (this))
        return "Another formuls in this session is playing; only one can, so this one is silent.";
    return engine->getStatus();
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
