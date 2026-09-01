/*
 * PerformanceMeter.h -- shared by BOTH formuls apps (src/app and
 * src/app-alacarte).
 *
 * Deliberately header-only, framework-free C++17 + POSIX so the JUCE build
 * and the à la carte build measure themselves with *identical* code. That
 * is the whole point: numbers from the two apps are only comparable if the
 * measurement is the same, so please keep this file free of JUCE types and
 * of anything platform-specific beyond POSIX getrusage().
 *
 * Two different numbers are reported, and the difference matters:
 *
 *   dspLoad()  -- what fraction of its deadline the audio callback used.
 *                 A 512-frame buffer at 48 kHz must be filled in 10.67 ms;
 *                 if rendering takes 3 ms that is 28%. This is the number
 *                 that predicts dropouts, and it is independent of how many
 *                 cores the machine has.
 *
 *   processCpu() -- CPU time used by the whole process (all threads) as a
 *                 percentage of ONE core, sampled between calls. This is
 *                 the number comparable to Activity Monitor, which is what
 *                 you are looking at when you say "CPU usage".
 *
 * They answer different questions. DSP load can sit at 30% while process
 * CPU climbs, if message handling or the GUI is doing the work rather than
 * the audio thread -- exactly the case worth distinguishing when comparing
 * these two builds under heavy modulation.
 *
 * Threading: endRender() is called on the audio thread and only touches
 * atomics and the clock -- no allocation, no locks. The readers are safe to
 * call from the GUI thread.
 */

#pragma once

#include <sys/resource.h>
#include <sys/time.h>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace formuls
{

class PerformanceMeter
{
public:
    using Clock = std::chrono::steady_clock;

    // ---------------------------------------------------------- audio thread

    /** Call at the top of the audio callback. */
    Clock::time_point beginRender() const  { return Clock::now(); }

    /** Call at the bottom of the audio callback, passing what beginRender()
        returned along with the size of the block just rendered. */
    void endRender (Clock::time_point started, int numFrames, double sampleRate)
    {
        if (numFrames <= 0 || sampleRate <= 0.0)
            return;

        const auto elapsed = std::chrono::duration<double> (Clock::now() - started).count();
        const auto deadline = (double) numFrames / sampleRate;
        const auto load = elapsed / deadline;

        // Exponential moving average: readable on screen without hiding
        // sustained rises. ~0.05 settles in roughly a second of callbacks.
        const auto previous = smoothedLoad.load (std::memory_order_relaxed);
        const auto updated = previous <= 0.0 ? load : previous + 0.05 * (load - previous);
        smoothedLoad.store (updated, std::memory_order_relaxed);

        // The first callbacks after the stream opens pay cold caches, page
        // faults and thread promotion, and routinely "miss" their deadline
        // without anything being audible. Counting them would show a scary
        // peak and a phantom dropout on every single run, so let the stream
        // settle before recording peaks and overloads. The smoothed load
        // above still updates from the very first callback.
        if (callbacks.fetch_add (1, std::memory_order_relaxed) < warmupCallbacks)
            return;

        auto currentPeak = peakLoad.load (std::memory_order_relaxed);
        while (load > currentPeak
               && ! peakLoad.compare_exchange_weak (currentPeak, load,
                                                    std::memory_order_relaxed))
        {}

        // Missing the deadline is what a dropout sounds like.
        if (load >= 1.0)
            overloads.fetch_add (1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------- readers

    /** Smoothed audio-callback load, as a percentage of the deadline. */
    double dspLoad() const   { return smoothedLoad.load (std::memory_order_relaxed) * 100.0; }

    /** Worst single callback since the last resetPeak(), as a percentage. */
    double dspPeak() const   { return peakLoad.load (std::memory_order_relaxed) * 100.0; }

    /** Callbacks that missed their deadline (>= 100% load). */
    long overloadCount() const { return overloads.load (std::memory_order_relaxed); }

    /** Clears the peak and the missed-deadline count, leaving the smoothed
        load alone. Handy for "reset, play hard for a minute, look again". */
    void resetPeak()
    {
        peakLoad.store (0.0, std::memory_order_relaxed);
        overloads.store (0, std::memory_order_relaxed);
    }

    /** Whole-process CPU since the previous call, as a percentage of one
        core (so >100% is possible and means more than one core's worth).
        Call it on a steady interval; the first call returns 0. */
    double processCpu()
    {
        rusage usage {};

        if (getrusage (RUSAGE_SELF, &usage) != 0)
            return 0.0;

        const double cpuSeconds =
            (double) usage.ru_utime.tv_sec + (double) usage.ru_utime.tv_usec * 1e-6 +
            (double) usage.ru_stime.tv_sec + (double) usage.ru_stime.tv_usec * 1e-6;
        const auto now = Clock::now();

        double percent = 0.0;

        if (haveCpuSample)
        {
            const auto wall = std::chrono::duration<double> (now - lastCpuTime).count();

            if (wall > 0.0)
                percent = (cpuSeconds - lastCpuSeconds) / wall * 100.0;
        }

        lastCpuSeconds = cpuSeconds;
        lastCpuTime = now;
        haveCpuSample = true;

        return std::max (0.0, percent);
    }

    /** Forget everything (called when the engine stops). */
    void reset()
    {
        smoothedLoad.store (0.0, std::memory_order_relaxed);
        resetPeak();
        callbacks.store (0, std::memory_order_relaxed);
        haveCpuSample = false;
    }

private:
    std::atomic<double> smoothedLoad { 0.0 };
    std::atomic<double> peakLoad { 0.0 };
    std::atomic<long> overloads { 0 };
    std::atomic<long> callbacks { 0 };

    // ~16 callbacks is a fifth of a second at 512 frames / 48 kHz.
    static constexpr long warmupCallbacks = 16;

    // processCpu() state -- GUI thread only, no atomics needed
    double lastCpuSeconds = 0.0;
    Clock::time_point lastCpuTime {};
    bool haveCpuSample = false;
};

} // namespace formuls
