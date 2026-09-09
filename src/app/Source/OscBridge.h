/*
 * OscBridge.h
 *
 * Receives the control GUI's OSC and hands it to the Pd patch.
 *
 * This used to be the patch's own job: _main.pd had
 *
 *     [netreceive -u -b]  ->  [oscparse]  ->  [list trim]  ->  [route ...]
 *
 * listening on udp 9000. That crashed the app, and the reason is worth
 * recording because it is not obvious.
 *
 *
 * Why the patch cannot do this
 * ---------------------------
 * libpd services sockets from inside the audio callback. Its PROCESS macro
 * (z_libpd.c) calls sys_pollgui(), which calls sys_domicrosleep(0)
 * (s_inter.c), which select()s over every registered fd and runs each
 * handler inline. So [netreceive], [oscparse] and every message they set off
 * ran on the CoreAudio IO thread -- a thread with a few hundred KB of stack,
 * not the 8 MB Pd normally enjoys.
 *
 * Two things then go wrong at once, both inside oscparse_list (x_misc.c):
 *
 *   outv = (t_atom *)alloca(outc * sizeof(t_atom));
 *
 * the parse buffer is on the *stack* and sized by the incoming packet (for a
 * blob, roughly one 16-byte atom per byte of payload); and a bundle is parsed
 * by recursing per element, so every element's alloca is still held when the
 * next one runs.
 *
 * Open Stage Control sends its entire state as one bundle when a fresh
 * client connects. The result was a stack-guard violation on the audio
 * thread -- nine identical crash reports, every one of them
 * EXC_BAD_ACCESS/SIGBUS in pd_typedmess's prologue on
 * com.apple.audio.IOThread.client. Reloading a page the browser already had
 * open did not crash, because that does not trigger a full state sync.
 *
 *
 * What happens instead
 * --------------------
 * juce::OSCReceiver parses on its own thread, onto the heap. The listener is
 * a MessageLoopCallback one, so messages arrive here on the *message*
 * thread, and libpd_message() -- which is what PdBase::finishMessage()
 * reaches -- takes sys_lock() itself and then runs pd_typedmess() on the
 * calling thread. The patch's message dispatch therefore happens on the
 * message thread's full-size stack, while the audio thread does nothing but
 * audio.
 *
 * The patch is fed at exactly the point [list trim] used to feed, through
 * [r formuls-osc-in], so everything downstream of that [route] is unchanged.
 *
 *
 * Still on the audio thread
 * -------------------------
 * _main.pd keeps a second [netreceive -u] on udp 9009 (FUDI, not OSC)
 * feeding MOD_RECEIVE_. Nothing in the shipped GUI sends to it, so it never
 * dispatches, but it is still polled from the audio callback and would carry
 * the same risk if anything ever did. Moving it needs a FUDI parser, which
 * is why it was left alone.
 */

#pragma once

#include <JuceHeader.h>

namespace pd { class PdBase; }

namespace formuls
{

class OscBridge : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    OscBridge() = default;
    ~OscBridge() override;

    /** Pd receive symbol the translated messages are sent to. _main.pd has
        an [r formuls-osc-in] wired where [list trim] used to be. */
    static constexpr const char* receiverName = "formuls-osc-in";

    /** Starts listening on the given udp port and forwards to pdToFeed.
        Returns Result::ok(), or a failure naming the port if it could not be
        bound (which usually means another formuls is already running).
        pdToFeed must outlive the bridge. */
    juce::Result start (pd::PdBase& pdToFeed, int udpPort);

    /** Stops listening. Safe to call when not started, and safe twice. */
    void stop();

    bool isListening() const noexcept   { return listening; }

    /** Messages forwarded since start(). Handy when checking that the GUI is
        actually talking to the patch. */
    int getMessageCount() const noexcept   { return messageCount; }

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void oscBundleReceived (const juce::OSCBundle& bundle) override;

    void forward (const juce::OSCMessage& message);

    juce::OSCReceiver receiver;
    pd::PdBase* pd = nullptr;
    bool listening = false;
    int messageCount = 0;

    // Logged once rather than per message: a malformed GUI would otherwise
    // fill the log faster than it could be read.
    bool warnedAboutArgumentType = false;
    bool warnedAboutAddress = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscBridge)
};

} // namespace formuls
