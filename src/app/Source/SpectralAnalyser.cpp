#include "SpectralAnalyser.h"

#include <cmath>

namespace
{
    // How many FFT bins the descriptors are computed over: the magnitude
    // spectrum, so half the window.
    constexpr int numBins = SpectralAnalyser::windowSize / 2;

    float toDb (float rms) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (rms, 1.0e-9f));
    }

    /* An in-place iterative radix-2 Cooley-Tukey FFT, for one fixed size.
       The caller has already put the windowed samples in real[] with imag[]
       cleared, in bit-reversed order. 1024 points for each channel five times
       a second is a few hundred microseconds in all, on the message thread. */
    void fftInPlace (float* real, float* imag, int size) noexcept
    {
        for (int span = 2; span <= size; span *= 2)
        {
            const auto step = juce::MathConstants<double>::twoPi / span;

            for (int start = 0; start < size; start += span)
            {
                for (int k = 0; k < span / 2; ++k)
                {
                    // Twiddle factor e^(-2i pi k / span), applied to the odd half.
                    const auto angle = -step * k;
                    const auto wr = (float) std::cos (angle);
                    const auto wi = (float) std::sin (angle);

                    const auto even = start + k;
                    const auto odd  = even + span / 2;

                    const auto tr = wr * real[odd] - wi * imag[odd];
                    const auto ti = wr * imag[odd] + wi * real[odd];

                    real[odd] = real[even] - tr;
                    imag[odd] = imag[even] - ti;
                    real[even] += tr;
                    imag[even] += ti;
                }
            }
        }
    }
}

SpectralAnalyser::SpectralAnalyser() = default;
SpectralAnalyser::~SpectralAnalyser() = default;

void SpectralAnalyser::prepare (int numChannels, double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels.clear();

    for (int ch = 0; ch < juce::jmax (0, numChannels); ++ch)
    {
        auto channel = std::make_unique<Channel>();
        channel->staging.assign ((size_t) windowSize, 0.0f);

        for (auto& buffer : channel->published)
            buffer.assign ((size_t) windowSize, 0.0f);

        channels.push_back (std::move (channel));
    }

    // Bit-reversal permutation for the FFT below, computed once.
    bitReversed.resize ((size_t) windowSize);
    const auto bits = (int) std::log2 (windowSize);

    for (int i = 0; i < windowSize; ++i)
    {
        int reversed = 0;

        for (int bit = 0; bit < bits; ++bit)
            if (i & (1 << bit))
                reversed |= 1 << (bits - 1 - bit);

        bitReversed[(size_t) i] = reversed;
    }

    // A Hann window: the descriptors are moments of the spectrum, and
    // rectangular-window leakage would put energy at frequencies that are not
    // in the signal, which spread and skew would then report.
    hann.resize ((size_t) windowSize);

    for (int i = 0; i < windowSize; ++i)
        hann[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                    * (float) i / (float) (windowSize - 1)));

    real.assign ((size_t) windowSize, 0.0f);
    imag.assign ((size_t) windowSize, 0.0f);
    magnitudes.assign ((size_t) numBins, 0.0f);
    frequencies.assign ((size_t) numBins, 0.0f);

    for (int bin = 0; bin < numBins; ++bin)
        frequencies[(size_t) bin] = (float) (bin * rate / windowSize);
}

void SpectralAnalyser::reset()
{
    for (auto& channel : channels)
    {
        channel->stagingFill = 0;
        channel->readable.store (-1, std::memory_order_release);
    }
}

void SpectralAnalyser::pushBlock (const float* const* channelData, int numChannels,
                                  int numSamples) noexcept
{
    const auto count = juce::jmin (numChannels, (int) channels.size());

    for (int ch = 0; ch < count; ++ch)
    {
        const auto* source = channelData[ch];

        if (source == nullptr)
            continue;

        auto& channel = *channels[(size_t) ch];
        int offset = 0;

        while (offset < numSamples)
        {
            const auto room = windowSize - channel.stagingFill;
            const auto take = juce::jmin (room, numSamples - offset);

            juce::FloatVectorOperations::copy (channel.staging.data() + channel.stagingFill,
                                               source + offset, take);
            channel.stagingFill += take;
            offset += take;

            if (channel.stagingFill < windowSize)
                break;

            // A full window: publish it into whichever buffer the message
            // thread is not reading, then point it at that one.
            const auto slot = channel.writing.load (std::memory_order_relaxed);
            juce::FloatVectorOperations::copy (channel.published[(size_t) slot].data(),
                                               channel.staging.data(), windowSize);
            channel.readable.store (slot, std::memory_order_release);
            channel.writing.store (1 - slot, std::memory_order_relaxed);
            channel.stagingFill = 0;
        }
    }
}

juce::var SpectralAnalyser::takeFrame()
{
    if (channels.empty() || bitReversed.empty())
        return {};

    juce::Array<juce::var> entries;

    for (int ch = 0; ch < (int) channels.size(); ++ch)
    {
        auto& channel = *channels[(size_t) ch];
        const auto slot = channel.readable.load (std::memory_order_acquire);

        if (slot < 0)
            continue;

        const auto& window = channel.published[(size_t) slot];

        // RMS of the raw window, before the Hann window scales it.
        double sumOfSquares = 0.0;

        for (int i = 0; i < windowSize; ++i)
            sumOfSquares += (double) window[(size_t) i] * window[(size_t) i];

        const auto level = toDb ((float) std::sqrt (sumOfSquares / windowSize));

        auto* entry = new juce::DynamicObject();
        entry->setProperty ("i", ch);
        entry->setProperty ("db", juce::roundToInt (level * 10.0f) / 10.0);

        if (level <= silenceDb)
        {
            // Silence: the moments of an empty spectrum are noise. Report the
            // level and leave the descriptors null, so an analysis can tell
            // "nothing was sounding" from "this is what it sounded like".
            entries.add (juce::var (entry));
            continue;
        }

        juce::FloatVectorOperations::clear (imag.data(), windowSize);

        // Window, and scatter into bit-reversed order for the in-place FFT.
        for (int i = 0; i < windowSize; ++i)
            real[(size_t) bitReversed[(size_t) i]] = window[(size_t) i] * hann[(size_t) i];

        fftInPlace (real.data(), imag.data(), windowSize);

        for (int bin = 0; bin < numBins; ++bin)
            magnitudes[(size_t) bin] = std::sqrt (real[(size_t) bin] * real[(size_t) bin]
                                                  + imag[(size_t) bin] * imag[(size_t) bin]);

        // The moments below are all weighted by the magnitudes, so a spectrum
        // of zeros has none of them.
        double sumOfMagnitudes = 0.0;

        for (int bin = 0; bin < numBins; ++bin)
            sumOfMagnitudes += magnitudes[(size_t) bin];

        if (sumOfMagnitudes <= 0.0)
        {
            entries.add (juce::var (entry));
            continue;
        }

        // Centroid: xtract_spectral_centroid -- sum(f * a) / sum(a), in Hz.
        double weighted = 0.0;

        for (int bin = 0; bin < numBins; ++bin)
            weighted += (double) frequencies[(size_t) bin] * magnitudes[(size_t) bin];

        const auto centroid = weighted / sumOfMagnitudes;

        // Spread: xtract_spectral_variance -- sum((f - c)^2 * a) / sum(a).
        // Logged as its square root (xtract_spectral_standard_deviation), so
        // the number is in Hz and comparable with the centroid rather than
        // being its square.
        double variance = 0.0;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto d = (double) frequencies[(size_t) bin] - centroid;
            variance += d * d * magnitudes[(size_t) bin];
        }

        variance /= sumOfMagnitudes;
        const auto deviation = std::sqrt (juce::jmax (0.0, variance));

        // Skew: xtract_spectral_skewness -- sum((f - c)^3 * a) / (sum(a) *
        // sd^3). Dimensionless; positive means energy leans above the
        // centroid, negative below.
        juce::var skew;

        if (deviation > 0.0)
        {
            double cubed = 0.0;

            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto d = (double) frequencies[(size_t) bin] - centroid;
                cubed += d * d * d * magnitudes[(size_t) bin];
            }

            skew = juce::roundToInt (cubed / (sumOfMagnitudes * deviation * deviation * deviation)
                                     * 1000.0) / 1000.0;
        }

        // Flatness: xtract_flatness -- the geometric mean of the magnitudes
        // over the arithmetic mean, both taken over the bins above zero only,
        // as LibXtract does. Near 1 is noise-like, near 0 tonal.
        double logSum = 0.0, positiveSum = 0.0;
        int positive = 0;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto magnitude = (double) magnitudes[(size_t) bin];

            if (magnitude > 0.0)
            {
                logSum += std::log (magnitude);
                positiveSum += magnitude;
                ++positive;
            }
        }

        juce::var flatness;

        if (positive > 0 && positiveSum > 0.0)
            flatness = juce::roundToInt (std::exp (logSum / positive) / (positiveSum / positive)
                                         * 100000.0) / 100000.0;

        // Irregularity: xtract_irregularity_j (Jensen) -- sum((a[n] -
        // a[n+1])^2) / sum(a[n]^2). Krimphoff's xtract_irregularity_k is not
        // normalised, so it would rise with level alone; this one does not,
        // which is what makes a loud passage comparable with a quiet one.
        // Read it the right way round: it is high for a sparse spectrum,
        // where neighbouring bins differ hugely (0.64 for a sine), and low
        // for a dense one (0.27 for white noise). It is not "roughness".
        double differences = 0.0, squares = 0.0;

        for (int bin = 0; bin < numBins - 1; ++bin)
        {
            const auto d = (double) magnitudes[(size_t) bin] - magnitudes[(size_t) bin + 1];
            differences += d * d;
        }

        for (int bin = 0; bin < numBins; ++bin)
            squares += (double) magnitudes[(size_t) bin] * magnitudes[(size_t) bin];

        juce::var irregularity;

        if (squares > 0.0)
            irregularity = juce::roundToInt (differences / squares * 10000.0) / 10000.0;

        entry->setProperty ("cen", juce::roundToInt (centroid));
        entry->setProperty ("spread", juce::roundToInt (deviation));
        entry->setProperty ("flat", flatness);
        entry->setProperty ("irr", irregularity);
        entry->setProperty ("skew", skew);
        entries.add (juce::var (entry));
    }

    if (entries.isEmpty())
        return {};

    auto* frame = new juce::DynamicObject();
    frame->setProperty ("k", "spec");
    frame->setProperty ("n", windowSize);
    frame->setProperty ("sr", (int) rate);
    frame->setProperty ("ch", entries);
    return juce::var (frame);
}
