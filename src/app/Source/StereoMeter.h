/*
 * StereoMeter.h
 *
 * Two horizontal VU meters (L / R) drawn as gradient bars with peak hold.
 * Reads peak levels published by FormulsEngine on the audio thread.
 */

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace formuls
{

class StereoMeter : public juce::Component,
                    private juce::Timer
{
public:
    StereoMeter() : juce::Component(), juce::Timer()
    {
        startTimerHz (30);
    }

    void setPeakLevels (const std::atomic<float>* peaks)
    {
        peakLevels = peaks;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        const int barHeight = (bounds.getHeight() - 4) / 2;
        const int width = bounds.getWidth();

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = bounds.removeFromTop (barHeight);
            bounds.removeFromTop (4);

            g.setColour (juce::Colour (0xff1a3a1a));
            g.fillRect (bar);

            const float level = peakLevels != nullptr
                ? juce::jlimit (0.0f, 1.0f, peakLevels[ch].load (std::memory_order_relaxed))
                : 0.0f;

            if (level > 0.0f)
            {
                const int fillW = juce::jmax (1, (int) (width * level));

                // Green 0-60%, yellow 60-80%, red 80-100%
                for (int x = 0; x < fillW; ++x)
                {
                    const float pos = (float) x / (float) width;
                    juce::Colour c;
                    if (pos < 0.6f)
                        c = juce::Colour (0xff00cc44).interpolatedWith (juce::Colour (0xffcccc00), pos / 0.6f);
                    else if (pos < 0.8f)
                        c = juce::Colour (0xffcccc00).interpolatedWith (juce::Colour (0xffcc2200), (pos - 0.6f) / 0.2f);
                    else
                        c = juce::Colour (0xffcc2200);

                    g.setColour (c);
                    g.drawVerticalLine (x, (float) bar.getY(), (float) bar.getBottom());
                }
            }

            // Peak hold line
            if (peakHold[ch] > 0.01f)
            {
                const int peakX = juce::jlimit (0, width - 1, (int) (width * peakHold[ch]));
                g.setColour (juce::Colour (0xffffffff));
                g.drawVerticalLine (peakX, (float) bar.getY(), (float) bar.getBottom());
            }
        }
    }

private:
    void timerCallback() override
    {
        bool needsRepaint = false;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float current = peakLevels != nullptr
                ? juce::jlimit (0.0f, 1.0f, peakLevels[ch].load (std::memory_order_relaxed))
                : 0.0f;

            if (current > peakHold[ch])
            {
                peakHold[ch] = current;
                holdCounters[ch] = 30;   // ~1s at 30Hz
            }
            else if (holdCounters[ch] > 0)
            {
                holdCounters[ch]--;
            }
            else
            {
                peakHold[ch] *= 0.96f;
                if (peakHold[ch] < 0.005f)
                    peakHold[ch] = 0.0f;
            }

            if (std::abs (current - lastLevel[ch]) > 0.002f || peakHold[ch] != lastPeak[ch])
            {
                lastLevel[ch] = current;
                lastPeak[ch] = peakHold[ch];
                needsRepaint = true;
            }
        }

        if (needsRepaint)
            repaint();
    }

    const std::atomic<float>* peakLevels = nullptr;
    float peakHold[2] = { 0.0f, 0.0f };
    int holdCounters[2] = { 0, 0 };
    float lastLevel[2] = { -1.0f, -1.0f };
    float lastPeak[2] = { -1.0f, -1.0f };
};

} // namespace formuls
