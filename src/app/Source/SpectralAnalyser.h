#pragma once

/* Spectral feature extraction for UX-logging builds.
 *
 * Why this exists: sound.csv measured the stereo master's loudness and
 * spectral centroid, and centroid on that signal says very little about this
 * instrument. Six polyphonic synths and the bus effects are summed before
 * anything is measured, so the figure follows whichever source is loudest and
 * cannot separate a brighter timbre from a higher pitch from a change in which
 * synth dominates. See docs/uxlog-spectral/README.md.
 *
 * So the engine's own channels are analysed here, in the app, and the numbers
 * go into the session log (one "spec" frame per interval, each channel its own
 * entry). With the channel count set to 14 those channels are the individual
 * synth and bus outputs, so each descriptor belongs to one source whose
 * parameters the player was adjusting; with 2 they are the master mix, which
 * is better than nothing and no worse than before. Logging numbers rather than
 * recording 14 channels of audio is what makes this affordable: a 14-channel
 * Ogg take is roughly 700 MB an hour, these frames are of the same order as
 * the existing "in" stream, about 15 MB an hour.
 *
 * Four descriptors, plus the level. The definitions are LibXtract's
 * (github.com/jamiebullock/LibXtract), so a number here is comparable with one
 * from that library; the named function is given against each in the .cpp.
 * LibXtract itself is not linked: it is a C library that would need a
 * submodule and build changes on three platforms plus the plugin, and these
 * are four closed-form summations over a magnitude spectrum. The transform
 * they need is a radix-2 FFT of one fixed size, written out in the .cpp --
 * juce_dsp is not among this project's modules, and adding a module to get
 * one function would mean module paths in three exporters.
 *
 * Threading. pushBlock() runs on the audio thread, allocates nothing and
 * locks nothing: it copies samples into a per-channel staging buffer and, once
 * a whole window is there, publishes it by flipping an atomic index. No FFT
 * happens there. takeFrame() runs on the message thread at the frame rate,
 * reads the most recently published window per channel and does the transform
 * and the arithmetic. A window published while the message thread is reading
 * the other buffer is simply the next frame's; nothing blocks the audio
 * thread, and no frame is ever half of one window and half of another.
 *
 * A frame is therefore a snapshot: it describes one 1024-sample window (21 ms
 * at 48 kHz) taken at that moment, not an average over the whole interval.
 */

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <JuceHeader.h>

class SpectralAnalyser
{
public:
    /** The analysis window, in samples: 1024 is 21 ms at 48 kHz, long enough
        to resolve about 47 Hz and short enough that a frame is current. */
    static constexpr int windowSize = 1024;

    /** Below this level a channel's descriptors are meaningless, so only its
        level is reported and the rest are left null. */
    static constexpr float silenceDb = -90.0f;

    SpectralAnalyser();
    ~SpectralAnalyser();

    /** Message thread, before the engine starts. Sizes the buffers for the
        channel count the engine will render and remembers the sample rate,
        which the descriptors need to turn bins into hertz. */
    void prepare (int numChannels, double sampleRate);

    /** Audio thread. Copies the block in; never allocates, never locks, and
        skips any channel JUCE handed over as null. */
    void pushBlock (const float* const* channelData, int numChannels, int numSamples) noexcept;

    /** Message thread. The latest window of every channel, analysed, as the
        object written to the log. Void until at least one window has been
        published. */
    juce::var takeFrame();

    /** Message thread. Forgets everything published so far. */
    void reset();

    int getNumChannels() const noexcept    { return (int) channels.size(); }

private:
    struct Channel
    {
        std::array<std::vector<float>, 2> published;   // two windows, one being written
        std::vector<float> staging;
        int stagingFill = 0;
        std::atomic<int> readable { -1 };              // index into published, -1 = none yet
        std::atomic<int> writing { 0 };
    };

    // vector<unique_ptr> because Channel holds atomics and so cannot be moved
    // when the vector grows.
    std::vector<std::unique_ptr<Channel>> channels;
    std::vector<float> hann, real, imag, magnitudes, frequencies;
    std::vector<int> bitReversed;
    double rate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralAnalyser)
};
