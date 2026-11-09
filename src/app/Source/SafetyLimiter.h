/*
 * SafetyLimiter.h
 *
 * The last stage before the audio device and the recorder: a look-ahead peak
 * limiter whose only job is that nothing leaves the engine above its ceiling.
 *
 * Why this exists
 * ---------------
 * Nothing in the Pd patch limits the signal. fsynth.lib sums POLYPHONY = 20
 * voices with `:>` and no 1/N normalisation, so one instance playing a chord
 * sums well past 1.0; `AUDIO_SAFETY______` then buys a flat 6 dB of headroom,
 * which covers about two voices at full velocity. Past that the audio device
 * clamps, with no control over how. A take of a SINGLE instance measured on
 * 29 October 2026 had 8,784 frames pinned at exactly 0 dBFS across 27 of its
 * 467 seconds. docs/headroom/README.md has the whole picture.
 *
 * Why here rather than in the patch
 * ---------------------------------
 *   - It is the last stage, which is where a ceiling belongs.
 *   - One implementation covers every channel, including the `dac~ 9..14`
 *     per-instance sends and the TEST_TONE~~~ subpatch, both of which are
 *     wired straight to the `dac~` and bypass `AUDIO_SAFETY______`.
 *   - FormulsEngine::render() hands the same buffers to the device and to the
 *     recorder, so limiting there means the file and the room agree.
 *   - It survives patch edits. A guard inside _main.pd does not.
 *
 * No JUCE here on purpose: src/tools/limiter-probe.cpp compiles this header on
 * its own with one clang++ line and checks the guarantees below, with no JUCE,
 * no libpd and no audio device.
 *
 * The guarantee
 * -------------
 * For every output sample, |y| <= ceiling, give or take the rounding of a
 * float multiply -- there is no final clamp to fall back on, because a clamp
 * would be the hard clipping this class exists to avoid. The bound the probe
 * asserts is ceiling * (1 + 1e-6), about -120 dB of slack; the gain is snapped
 * to its frame target at each boundary so that rounding cannot accumulate
 * across frames. How the bound is arranged:
 *
 * Audio is read in frames of `frameSamples` (64, one libpd tick) and each
 * frame's peak is recorded. While frame m is being read, frame m-2 is being
 * emitted, so the peaks of both frame m-2 and frame m-1 are already known.
 * Frame j is emitted under a gain that ramps linearly from a[j] to
 *
 *     b[j] = min (1, ceiling/P[j], ceiling/P[j+1], a[j] * riseLimit)
 *
 * with a[j] = b[j-1]. Because b[j-1] already included the ceiling/P[j] term,
 * BOTH ends of the ramp are <= ceiling/P[j], and a linear ramp never leaves
 * the interval between its ends -- so every gain applied to frame j is
 * <= ceiling/P[j], and P[j] is the largest sample in it. That is the whole
 * proof, and it holds however violent the input is: a drop too steep for the
 * ramp simply arrives in one frame instead of being eased in.
 *
 * Needing P[j+1] before frame j can be emitted is what costs two frames of
 * latency rather than one. `latencySamples` is 128 -- 2.67 ms at 48 kHz --
 * and it is constant, so a plugin host can compensate for it
 * (FormulsPluginProcessor::prepareToPlay reports it).
 *
 * Gain linking
 * ------------
 * Linked channels share one gain, so a stereo stem cannot have its image
 * pulled sideways by a limiter acting on one side. The engine's channels are
 * (read out of _main.pd's MASTER_BUS_FX_________________________ and
 * AUDIO_RECEIVES_DIGITALISER_&_MULTI_OUT):
 *
 *     1/2    main mix L/R
 *     3/4    reverb L/R
 *     5/6    repeater L/R
 *     7/8    dry/direct L/R
 *     9..14  six mono per-instance sends, one per synth
 *
 * So channels 1-8 are four stereo stems and are linked in pairs, while 9-14
 * are separate stems and each gets its own gain: ducking one synth's send
 * because another synth peaked would be wrong, and they are recorded as
 * individual tracks. In 2-channel mode there is one linked pair.
 *
 * Non-finite samples
 * ------------------
 * A NaN or infinity out of Pd is replaced with silence and counted. This is
 * not hypothetical tidiness: one NaN reaching the gain computer would poison
 * every later comparison, and a NaN reaching the device is worse than
 * clipping. getNonFiniteCount() is there so a run that produced any can say
 * so rather than failing quietly.
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace formuls
{

class SafetyLimiter
{
public:
    /** One libpd tick. Frames are aligned to it so the limiter's natural
        granularity matches the engine's. */
    static constexpr int frameSamples = 64;

    /** Constant delay through the limiter when it is enabled: two frames,
        because emitting frame j needs the peak of frame j+1. 2.67 ms at
        48 kHz. Zero when disabled -- it is bypassed, not zero-gain. */
    static constexpr int latencySamples = 2 * frameSamples;

    struct Config
    {
        bool enabled = true;

        /** Linear ceiling. -1 dBFS by default: low enough that a sample-rate
            converter or a lossy encode downstream has somewhere to put its
            own overshoot, high enough to be inaudible. */
        float ceiling = 0.8912509381337456f;   // 10^(-1/20)

        /** How fast gain is allowed back up, in dB per second. 40 dB/s brings
            a 3 dB dip home in 75 ms: quick enough not to duck the notes after
            a transient, slow enough not to pump on a source with this one's
            21 dB crest factor. */
        float releaseDbPerSecond = 40.0f;
    };

    /** Config from the environment, so a performance can be run without the
        limiter without a rebuild:

            FORMULS_LIMITER=off              bypass entirely
            FORMULS_LIMITER_CEILING_DB=-0.3  ceiling, in dBFS (must be <= 0)
            FORMULS_LIMITER_RELEASE_DB_PER_SEC=80

        Anything unparseable leaves that field at its default, on the same
        reasoning as the recording disk guard: a mistyped variable should not
        quietly turn the safety net off. */
    static Config configFromEnvironment()
    {
        Config config;

        if (const auto* off = std::getenv ("FORMULS_LIMITER"))
        {
            const std::string value (off);
            if (value == "off" || value == "0" || value == "false")
                config.enabled = false;
        }

        if (const auto* dB = std::getenv ("FORMULS_LIMITER_CEILING_DB"))
        {
            try
            {
                const auto parsed = std::stof (std::string (dB));
                if (std::isfinite (parsed) && parsed <= 0.0f && parsed >= -60.0f)
                    config.ceiling = std::pow (10.0f, parsed / 20.0f);
            }
            catch (...) {}
        }

        if (const auto* release = std::getenv ("FORMULS_LIMITER_RELEASE_DB_PER_SEC"))
        {
            try
            {
                const auto parsed = std::stof (std::string (release));
                if (std::isfinite (parsed) && parsed > 0.0f && parsed <= 1000.0f)
                    config.releaseDbPerSecond = parsed;
            }
            catch (...) {}
        }

        return config;
    }

    /** Allocates and resets. Not on the audio thread -- FormulsEngine calls
        this from prepareRender(), beside the other audio-thread buffers. */
    void prepare (const Config& configToUse, int numChannels, double sampleRate)
    {
        config = configToUse;
        channels = std::max (0, numChannels);
        groups = numGroupsFor (channels);

        if (! std::isfinite (config.ceiling) || config.ceiling <= 0.0f)
            config.ceiling = Config{}.ceiling;

        // Gain may rise by this factor per frame. Multiplicative, so the
        // recovery is linear in dB.
        const auto frameSeconds = sampleRate > 0.0 ? (double) frameSamples / sampleRate
                                                   : (double) frameSamples / 48000.0;
        riseLimit = (float) std::pow (10.0, config.releaseDbPerSecond * frameSeconds / 20.0);

        delay.assign ((size_t) std::max (1, channels) * (size_t) latencySamples, 0.0f);
        framePeak.assign ((size_t) std::max (1, groups), 0.0f);
        previousFramePeak.assign ((size_t) std::max (1, groups), 0.0f);
        gain.assign ((size_t) std::max (1, groups), 1.0f);
        gainStep.assign ((size_t) std::max (1, groups), 0.0f);
        frameTarget.assign ((size_t) std::max (1, groups), 1.0f);

        writeIndex = 0;
        samplesIntoFrame = 0;
        maxReduction.store (1.0f, std::memory_order_relaxed);
        nonFinite.store (0, std::memory_order_relaxed);
    }

    /** Which linked group a channel belongs to: pairs across channels 0-7,
        one each from 8 up. See the channel map above. */
    static int groupOf (int channel) noexcept
    {
        return channel < 8 ? channel / 2 : 4 + (channel - 8);
    }

    static int numGroupsFor (int numChannels) noexcept
    {
        return numChannels <= 0 ? 0 : groupOf (numChannels - 1) + 1;
    }

    int getLatencySamples() const noexcept   { return config.enabled ? latencySamples : 0; }
    bool isEnabled() const noexcept          { return config.enabled; }
    float getCeiling() const noexcept        { return config.ceiling; }

    /** Largest gain reduction applied since the last takeMaxReductionDb(), as
        a positive number of dB (0 when the limiter has not engaged). For a
        meter or a log line; reading it clears it. */
    float takeMaxReductionDb() noexcept
    {
        const auto smallest = maxReduction.exchange (1.0f, std::memory_order_relaxed);
        return smallest >= 1.0f ? 0.0f : -20.0f * std::log10 (smallest);
    }

    /** How many non-finite samples have been replaced with silence. */
    long long getNonFiniteCount() const noexcept { return nonFinite.load (std::memory_order_relaxed); }

    /** Audio thread. Limits in place: `channelData[ch][0..numSamples)` is
        read and overwritten. Does nothing at all when disabled, so the
        bypassed path costs one branch per block. */
    void process (float* const* channelData, int numChannelsIn, int numSamples) noexcept
    {
        if (! config.enabled || numSamples <= 0 || channels <= 0)
            return;

        const int n = std::min (numChannelsIn, channels);

        if (n <= 0 || delay.empty())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // A frame boundary falls before the sample that starts the new
            // frame, so the ramp for the frame about to be emitted is in
            // place before any of its samples leave.
            if (samplesIntoFrame == 0)
                startFrame();

            for (int ch = 0; ch < n; ++ch)
            {
                // JUCE may hand the callback a null channel; nothing is
                // audible there, so it is skipped rather than written
                // through. Its delay line then holds stale samples, which
                // matters only if a device starts providing a channel it
                // previously did not -- worth at most one frame of staleness,
                // and prepare() clears the lines whenever the channel count
                // changes.
                if (channelData[ch] == nullptr)
                    continue;

                auto* line = delay.data() + (size_t) ch * (size_t) latencySamples;

                float in = channelData[ch][i];

                if (! std::isfinite (in))
                {
                    in = 0.0f;
                    nonFinite.fetch_add (1, std::memory_order_relaxed);
                }

                const int g = groupOf (ch);
                framePeak[(size_t) g] = std::max (framePeak[(size_t) g], std::fabs (in));

                // Read the sample leaving the line before overwriting it:
                // position writeIndex currently holds the sample from
                // latencySamples ago.
                const float out = line[(size_t) writeIndex];
                line[(size_t) writeIndex] = in;

                channelData[ch][i] = out * gain[(size_t) g];
            }

            for (int g = 0; g < groups; ++g)
                gain[(size_t) g] += gainStep[(size_t) g];

            writeIndex = (writeIndex + 1) % latencySamples;
            samplesIntoFrame = (samplesIntoFrame + 1) % frameSamples;
        }
    }

private:
    /** Called on each frame boundary: the frame just read becomes the
        "next" peak, and the ramp for the frame now being emitted is set. */
    void startFrame() noexcept
    {
        for (int g = 0; g < groups; ++g)
        {
            // Snap to the target the last ramp was aiming at, so the rounding
            // of frameSamples additions cannot accumulate from frame to frame
            // and lift a[j] above ceiling/P[j].
            gain[(size_t) g] = frameTarget[(size_t) g];

            const auto peakOfEmittedFrame = previousFramePeak[(size_t) g];
            const auto peakOfNextFrame    = framePeak[(size_t) g];

            // b[j] = min (1, C/P[j], C/P[j+1], a[j] * riseLimit)
            float target = std::min (1.0f, gain[(size_t) g] * riseLimit);

            if (peakOfEmittedFrame > config.ceiling)
                target = std::min (target, config.ceiling / peakOfEmittedFrame);

            if (peakOfNextFrame > config.ceiling)
                target = std::min (target, config.ceiling / peakOfNextFrame);

            if (! std::isfinite (target) || target < 0.0f)
                target = 0.0f;

            gainStep[(size_t) g] = (target - gain[(size_t) g]) / (float) frameSamples;
            frameTarget[(size_t) g] = target;

            // The ramp is monotone, so the deepest gain over this frame is one
            // of its two ends. Recorded here rather than scanned per sample.
            reportReduction (std::min (gain[(size_t) g], target));

            previousFramePeak[(size_t) g] = peakOfNextFrame;
            framePeak[(size_t) g] = 0.0f;
        }
    }

    void reportReduction (float g) noexcept
    {
        if (g >= 1.0f)
            return;

        auto current = maxReduction.load (std::memory_order_relaxed);
        while (g < current
               && ! maxReduction.compare_exchange_weak (current, g, std::memory_order_relaxed))
            ;
    }

    Config config {};
    int channels = 0;
    int groups = 0;
    float riseLimit = 1.0f;

    // latencySamples per channel, written and read in lockstep.
    std::vector<float> delay;
    int writeIndex = 0;
    int samplesIntoFrame = 0;

    std::vector<float> framePeak;           // peak of the frame being read
    std::vector<float> previousFramePeak;   // peak of the frame being emitted
    std::vector<float> gain;                // gain at the current sample
    std::vector<float> gainStep;            // per-sample ramp for this frame
    std::vector<float> frameTarget;         // where this frame's ramp ends

    std::atomic<float> maxReduction { 1.0f };
    std::atomic<long long> nonFinite { 0 };
};

} // namespace formuls
