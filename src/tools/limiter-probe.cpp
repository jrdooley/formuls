// Checks the guarantees SafetyLimiter.h claims, with no JUCE, no libpd and no
// audio device -- it compiles the header on its own. See src/tools/README.md.
//
//   clang++ -std=c++17 -O2 -I../app/Source limiter-probe.cpp -o limiter-probe
//
// Exit status is 0 when every check passes, 1 otherwise, so it can be wired
// into CI as-is.

#include "SafetyLimiter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using formuls::SafetyLimiter;

namespace
{

int failures = 0;
int checks   = 0;

void check (bool ok, const char* what, const std::string& detail = {})
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("  %s  %s%s%s\n", ok ? "pass" : "FAIL", what,
                 detail.empty() ? "" : " -- ", detail.c_str());
}

std::string f2s (double v, int dp = 6)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, "%.*f", dp, v);
    return buf;
}

// Runs `input` (numChannels x numSamples, de-interleaved) through a limiter in
// blocks of `blockSize`, returning the output. Mirrors how
// FormulsEngine::render() calls it: in place, on whatever block the device asks
// for.
std::vector<std::vector<float>> run (const std::vector<std::vector<float>>& input,
                                     int blockSize,
                                     SafetyLimiter::Config config,
                                     double sampleRate = 48000.0,
                                     SafetyLimiter* keep = nullptr)
{
    const int numChannels = (int) input.size();
    const int numSamples  = numChannels > 0 ? (int) input[0].size() : 0;

    SafetyLimiter local;
    SafetyLimiter& limiter = keep != nullptr ? *keep : local;
    limiter.prepare (config, numChannels, sampleRate);

    auto output = input;

    std::vector<float*> pointers ((size_t) numChannels);

    for (int i = 0; i < numSamples; i += blockSize)
    {
        const int n = std::min (blockSize, numSamples - i);

        for (int ch = 0; ch < numChannels; ++ch)
            pointers[(size_t) ch] = output[(size_t) ch].data() + i;

        limiter.process (pointers.data(), numChannels, n);
    }

    return output;
}

float peakOf (const std::vector<std::vector<float>>& buffers)
{
    float peak = 0.0f;
    for (const auto& channel : buffers)
        for (auto sample : channel)
            if (std::isfinite (sample))
                peak = std::max (peak, std::fabs (sample));
    return peak;
}

const char* const separator = "--------------------------------------------------";

} // namespace

int main()
{
    const SafetyLimiter::Config defaults {};
    const float ceiling = defaults.ceiling;
    const float tolerance = ceiling * (1.0f + 1e-6f);
    const int L = SafetyLimiter::latencySamples;

    std::printf ("SafetyLimiter probe\n");
    std::printf ("  ceiling %s (%.2f dBFS), frame %d, latency %d samples\n%s\n",
                 f2s (ceiling).c_str(), 20.0 * std::log10 ((double) ceiling),
                 SafetyLimiter::frameSamples, L, separator);

    // ---------------------------------------------------------------- ceiling
    // The claim is for EVERY output sample, so the inputs here are chosen to
    // be nastier than any synth: instant onsets, full-scale steps, noise well
    // above the ceiling, and DC that never lets the gain recover.
    std::printf ("Ceiling holds, on hostile input\n");
    {
        const int n = 48000;

        struct Case { const char* name; std::vector<float> (*make) (int); };

        auto sine = [] (int len)
        {
            std::vector<float> v ((size_t) len);
            for (int i = 0; i < len; ++i)
                v[(size_t) i] = 2.8f * std::sin (6.283185307f * 440.0f * (float) i / 48000.0f);
            return v;
        };

        auto step = [] (int len)
        {
            std::vector<float> v ((size_t) len, 0.0f);
            for (int i = len / 3; i < len; ++i)
                v[(size_t) i] = 4.0f;   // silence, then straight to +4
            return v;
        };

        auto impulses = [] (int len)
        {
            std::vector<float> v ((size_t) len, 0.0f);
            for (int i = 0; i < len; i += 977)
                v[(size_t) i] = 12.0f;  // isolated spikes, nothing to warn of them
            return v;
        };

        auto noise = [] (int len)
        {
            std::vector<float> v ((size_t) len);
            std::mt19937 rng (12345);
            std::uniform_real_distribution<float> dist (-5.0f, 5.0f);
            for (int i = 0; i < len; ++i)
                v[(size_t) i] = dist (rng);
            return v;
        };

        auto dc = [] (int len) { return std::vector<float> ((size_t) len, 3.0f); };

        auto rampUp = [] (int len)
        {
            std::vector<float> v ((size_t) len);
            for (int i = 0; i < len; ++i)
                v[(size_t) i] = 10.0f * (float) i / (float) len;
            return v;
        };

        const std::vector<std::pair<const char*, std::vector<float>>> cases {
            { "440 Hz sine at +8.9 dBFS", sine (n) },
            { "silence -> +12 dBFS step", step (n) },
            { "isolated +21.6 dBFS impulses", impulses (n) },
            { "uniform noise, +14 dBFS", noise (n) },
            { "constant DC at +9.5 dBFS", dc (n) },
            { "slow ramp to +20 dBFS", rampUp (n) },
        };

        for (const auto& c : cases)
        {
            // Two channels fed the same thing, then a copy phase-inverted, so
            // the linked pair is exercised both ways.
            std::vector<std::vector<float>> in { c.second, c.second };
            for (auto& s : in[1])
                s = -s;

            const auto out = run (in, 512, defaults);
            const auto peak = peakOf (out);

            check (peak <= tolerance, c.first,
                   "peak " + f2s (peak) + " vs ceiling " + f2s (ceiling));
        }
    }

    // ----------------------------------------------------------- transparency
    // The property that matters musically: when the material is already below
    // the ceiling the limiter must not touch it at all -- not "almost", but
    // the same float values, merely delayed.
    std::printf ("%s\nTransparent below the ceiling (bit-exact, only delayed)\n", separator);
    {
        const int n = 20000;
        std::vector<float> a ((size_t) n), b ((size_t) n);
        std::mt19937 rng (999);
        std::uniform_real_distribution<float> dist (-0.80f, 0.80f);   // below -1 dBFS

        for (int i = 0; i < n; ++i)
        {
            a[(size_t) i] = dist (rng);
            b[(size_t) i] = dist (rng);
        }

        const std::vector<std::vector<float>> in { a, b };
        const auto out = run (in, 333, defaults);

        bool exact = true;
        int firstBad = -1;

        for (int ch = 0; ch < 2 && exact; ++ch)
            for (int i = 0; i + L < n; ++i)
                if (out[(size_t) ch][(size_t) (i + L)] != in[(size_t) ch][(size_t) i])
                {
                    exact = false;
                    firstBad = i;
                    break;
                }

        check (exact, "every sample identical after the delay",
               exact ? "" : "first difference at sample " + std::to_string (firstBad));

        bool silentPreroll = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < L; ++i)
                if (out[(size_t) ch][(size_t) i] != 0.0f)
                    silentPreroll = false;

        check (silentPreroll, "the first 128 samples are the empty delay line");
    }

    // ----------------------------------------------------------------- latency
    std::printf ("%s\nLatency is exactly %d samples\n", separator, L);
    {
        const int n = 4096;
        std::vector<float> impulse ((size_t) n, 0.0f);
        impulse[1000] = 0.5f;           // below the ceiling, so gain stays 1

        const std::vector<std::vector<float>> in { impulse, impulse };
        const auto out = run (in, 64, defaults);

        int found = -1;
        for (int i = 0; i < n; ++i)
            if (out[0][(size_t) i] != 0.0f) { found = i; break; }

        check (found == 1000 + L, "impulse emerges delayed by the reported latency",
               "at sample " + std::to_string (found) + ", expected "
                   + std::to_string (1000 + L));

        check (found >= 0 && out[0][(size_t) found] == 0.5f,
               "and at its original amplitude");
    }

    // ------------------------------------------------------------ gain linking
    std::printf ("%s\nStereo pairs share a gain; separate stems do not\n", separator);
    {
        // Channel 0 gets a loud passage, channel 1 a quiet steady tone. If the
        // pair is linked, channel 1 is ducked by exactly the same factor, which
        // is what keeps a stereo image from sliding sideways.
        const int n = 8192;
        std::vector<float> loud ((size_t) n, 0.0f), quiet ((size_t) n);

        for (int i = 2000; i < 3000; ++i)
            loud[(size_t) i] = 3.0f;

        for (int i = 0; i < n; ++i)
            quiet[(size_t) i] = 0.25f;

        std::vector<std::vector<float>> in { loud, quiet };
        const auto out = run (in, 128, defaults);

        // Somewhere inside the loud passage both channels must be scaled by
        // the same factor.
        const int probe = 2500 + L;
        const float gainCh1 = out[1][(size_t) probe] / 0.25f;
        const float gainCh0 = out[0][(size_t) probe] / 3.0f;

        check (std::fabs (gainCh0 - gainCh1) < 1e-6f,
               "channels 1 and 2 are reduced by the same factor",
               "ch1 " + f2s (gainCh0) + " vs ch2 " + f2s (gainCh1));

        check (gainCh1 < 0.9f, "the quiet channel really was ducked (so linking is live)",
               "gain " + f2s (gainCh1));

        // Now 14 channels: a peak on channel 9 (the first mono per-instance
        // send) must leave channel 10 alone.
        std::vector<std::vector<float>> wide (14, std::vector<float> ((size_t) n, 0.0f));
        for (int i = 0; i < n; ++i)
            wide[9][(size_t) i] = 0.25f;        // steady, below the ceiling
        for (int i = 2000; i < 3000; ++i)
            wide[8][(size_t) i] = 3.0f;         // loud, on its neighbour

        const auto wideOut = run (wide, 128, defaults);

        check (wideOut[9][(size_t) probe] == 0.25f,
               "channel 10 is untouched by a peak on channel 9",
               "value " + f2s (wideOut[9][(size_t) probe]));

        check (peakOf ({ wideOut[8] }) <= tolerance,
               "while channel 9 itself is still under the ceiling",
               "peak " + f2s (peakOf ({ wideOut[8] })));

        check (SafetyLimiter::groupOf (0) == SafetyLimiter::groupOf (1)
                   && SafetyLimiter::groupOf (6) == SafetyLimiter::groupOf (7)
                   && SafetyLimiter::groupOf (8) != SafetyLimiter::groupOf (9)
                   && SafetyLimiter::numGroupsFor (2) == 1
                   && SafetyLimiter::numGroupsFor (14) == 10,
               "the channel map is four stereo pairs then six mono stems");
    }

    // ----------------------------------------------------------------- release
    std::printf ("%s\nRelease recovers at about the configured rate\n", separator);
    {
        // One loud frame, then a steady quiet tone: measure how long the gain
        // takes to come back to unity.
        const int n = 48000;
        const float quiet = 0.5f;
        std::vector<float> sig ((size_t) n, quiet);

        for (int i = 1000; i < 1000 + SafetyLimiter::frameSamples; ++i)
            sig[(size_t) i] = 2.0f;

        std::vector<std::vector<float>> in { sig, sig };
        const auto out = run (in, 64, defaults);

        // Find where the quiet tone is back to full amplitude.
        int recovered = -1;
        for (int i = 1000 + L + SafetyLimiter::frameSamples; i < n; ++i)
            if (out[0][(size_t) i] >= quiet * 0.9999f) { recovered = i; break; }

        const double seconds = recovered < 0 ? -1.0
                                             : (double) (recovered - (1000 + L)) / 48000.0;

        // 2.0 -> ceiling is about 7 dB of reduction; at 40 dB/s that is ~175 ms.
        const double expected = 20.0 * std::log10 (2.0 / (double) ceiling)
                                / (double) defaults.releaseDbPerSecond;

        check (recovered > 0 && seconds > expected * 0.5 && seconds < expected * 2.0,
               "gain is back to unity in the expected time",
               "took " + f2s (seconds, 4) + " s, expected about " + f2s (expected, 4));

        check (recovered > 0 && seconds > 0.02,
               "and not instantly, which would be a click rather than a release",
               "took " + f2s (seconds, 4) + " s");
    }

    // ------------------------------------------------------- block invariance
    std::printf ("%s\nThe result does not depend on the device's block size\n", separator);
    {
        const int n = 12000;
        std::vector<float> sig ((size_t) n);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-2.5f, 2.5f);
        for (int i = 0; i < n; ++i)
            sig[(size_t) i] = dist (rng);

        const std::vector<std::vector<float>> in { sig, sig };
        const auto reference = run (in, 64, defaults);

        for (int blockSize : { 1, 7, 32, 64, 100, 128, 480, 512, 1024 })
        {
            const auto out = run (in, blockSize, defaults);
            bool same = true;
            for (int ch = 0; ch < 2 && same; ++ch)
                for (int i = 0; i < n; ++i)
                    if (out[(size_t) ch][(size_t) i] != reference[(size_t) ch][(size_t) i])
                    { same = false; break; }

            check (same, ("block size " + std::to_string (blockSize)
                          + " matches block size 64").c_str());
        }
    }

    // -------------------------------------------------------- non-finite input
    std::printf ("%s\nNaN and infinity are replaced with silence, not propagated\n", separator);
    {
        const int n = 8192;
        std::vector<float> sig ((size_t) n, 0.5f);
        sig[1000] = std::numeric_limits<float>::quiet_NaN();
        sig[1001] = std::numeric_limits<float>::infinity();
        sig[1002] = -std::numeric_limits<float>::infinity();

        std::vector<std::vector<float>> in { sig, sig };

        SafetyLimiter limiter;
        const auto out = run (in, 256, defaults, 48000.0, &limiter);

        bool allFinite = true;
        for (const auto& channel : out)
            for (auto sample : channel)
                if (! std::isfinite (sample)) allFinite = false;

        check (allFinite, "no non-finite sample reaches the output");
        check (limiter.getNonFiniteCount() == 6,
               "all six were counted (3 per channel)",
               "counted " + std::to_string (limiter.getNonFiniteCount()));

        // The gain must not be stuck afterwards: the steady 0.5 tone should be
        // untouched well after the bad samples.
        check (out[0][(size_t) (4000 + L)] == 0.5f,
               "the gain is not poisoned: later samples pass through unchanged",
               "value " + f2s (out[0][(size_t) (4000 + L)]));
    }

    // ------------------------------------------------------------------ bypass
    std::printf ("%s\nSwitched off, it is a true bypass\n", separator);
    {
        auto off = defaults;
        off.enabled = false;

        const int n = 4096;
        std::vector<float> sig ((size_t) n);
        for (int i = 0; i < n; ++i)
            sig[(size_t) i] = 3.0f * std::sin (0.01f * (float) i);

        const std::vector<std::vector<float>> in { sig, sig };
        const auto out = run (in, 128, off);

        bool identical = true;
        for (int i = 0; i < n; ++i)
            if (out[0][(size_t) i] != in[0][(size_t) i]) { identical = false; break; }

        check (identical, "input passes through untouched, overshoot included");

        SafetyLimiter limiter;
        limiter.prepare (off, 2, 48000.0);
        check (limiter.getLatencySamples() == 0, "and reports no latency");
    }

    // ----------------------------------------------------------- reduction read
    std::printf ("%s\nThe reduction report is usable as a meter\n", separator);
    {
        const int n = 8192;
        std::vector<float> sig ((size_t) n, 2.0f);
        std::vector<std::vector<float>> in { sig, sig };

        SafetyLimiter limiter;
        run (in, 128, defaults, 48000.0, &limiter);

        const auto reduction = limiter.takeMaxReductionDb();
        const auto expected = 20.0f * std::log10 (2.0f / ceiling);

        check (std::fabs (reduction - expected) < 0.5f,
               "reports roughly the reduction actually needed",
               f2s (reduction, 2) + " dB, expected about " + f2s (expected, 2));

        check (limiter.takeMaxReductionDb() == 0.0f,
               "and reading it clears it");
    }

    std::printf ("%s\n%d checks, %d failed\n", separator, checks, failures);
    return failures == 0 ? 0 : 1;
}
