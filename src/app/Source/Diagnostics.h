/*
 * Diagnostics.h -- TEMPORARY timing diagnostics for random audio dropouts.
 *
 * macOS logged formuls missing its audio deadline (an "IO overload, client
 * timeout") with callbacks taking ~10.1 ms of a 10.7 ms budget, at about 2% CPU
 * on average. Two suspects:
 *
 *   - a single 64-frame Pd tick that is expensive (DSP, or Pd clocks firing
 *     inside it: automation, sequencer, netsend to the GUI);
 *   - the audio thread waiting for Pd's global lock (sys_lock) while the
 *     message thread holds it: OscBridge feeds each GUI message through
 *     libpd_message(), which takes sys_lock and runs the patch's reaction on
 *     the message thread.
 *
 * This records both, timestamped in one file, so a slow block can be matched
 * with whatever held the lock at that moment:
 *
 *   ~/Library/Logs/formuls/timing.log
 *
 *   SLOW BLOCK   an audio callback that used more than half its budget
 *   OSC HOLD     a GUI message that held Pd for more than 1 ms
 *   SUMMARY      every 10 s: callbacks, slow/missed counts, worst times
 *
 * Writing happens on the message thread only; the audio thread pushes plain
 * structs into a lock-free FIFO (see FormulsEngine). Remove once the cause is
 * known.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{

class Diagnostics
{
public:
    // Message thread only.
    static void log (const juce::String& line, double atMsHiRes = 0.0)
    {
        auto& d = instance();

        if (d.stream == nullptr)
            d.open();

        if (d.stream != nullptr)
        {
            *d.stream << stamp (atMsHiRes) << "  " << line << "\n";
            d.stream->flush();
        }
    }

    // Wall-clock time of a getMillisecondCounterHiRes() reading (0 = now).
    static juce::String stamp (double atMsHiRes)
    {
        const auto nowHiRes = juce::Time::getMillisecondCounterHiRes();
        const auto ageMs = atMsHiRes > 0.0 ? nowHiRes - atMsHiRes : 0.0;
        const auto when = juce::Time ((juce::int64) (juce::Time::currentTimeMillis() - (juce::int64) ageMs));
        return when.formatted ("%H:%M:%S") + juce::String::formatted (".%03d", when.getMilliseconds());
    }

    static juce::File logFile()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Logs/formuls/timing.log");
    }

private:
    static Diagnostics& instance() { static Diagnostics d; return d; }

    void open()
    {
        auto f = logFile();
        f.getParentDirectory().createDirectory();
        stream = std::make_unique<juce::FileOutputStream> (f);   // appends

        if (! stream->openedOk())
        {
            stream.reset();
            return;
        }

        *stream << "\n==== formuls timing diagnostics, "
                << juce::Time::getCurrentTime().toString (true, true, true, true) << " ====\n";
    }

    std::unique_ptr<juce::FileOutputStream> stream;
};

} // namespace formuls
