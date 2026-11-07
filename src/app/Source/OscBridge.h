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
 * juce::OSCReceiver parses on its own thread, onto the heap. That thread
 * then queues the message (post()) and the queue is drained on the *message*
 * thread, where libpd_message() -- which is what PdBase::finishMessage()
 * reaches -- takes sys_lock() itself and runs pd_typedmess() on the calling
 * thread. The patch's message dispatch therefore still happens on the
 * message thread's full-size stack, while the audio thread does nothing but
 * audio. That is what matters for the crash above, and it is unchanged.
 *
 * The listener is a RealtimeCallback one rather than MessageLoopCallback,
 * which is a throughput decision and not a safety one. MessageLoopCallback
 * posts one CallbackMessage per datagram; a talkative GUI then costs one
 * message-thread wakeup per message, the unbounded queue grows faster than
 * it drains, and everything behind it -- including /poweroff, the only
 * control an appliance has -- waits behind a reverb parameter. Draining a
 * batch per wakeup is the same work with a fraction of the round trips.
 * See issue #40.
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

class OscBridge : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                  private juce::AsyncUpdater,
                  private juce::Timer
{
public:
    OscBridge() = default;
    ~OscBridge() override;

    /** First address component that means "shut the machine down", rather
        than anything the patch should see. The GUI's RESET tab has a modal
        holding one button that sends /poweroff; this is intercepted in
        forward() and never reaches Pd, because a DSP patch is the wrong
        place to decide the machine's power state and [route] would have to
        grow a branch that does nothing audible. */
    static constexpr const char* powerOffSelector = "poweroff";

    /** Called on the message thread when /poweroff arrives. Set by
        FormulsEngine; if it is null the message is dropped with a log line,
        so a GUI that sends it to a build without the hook does nothing
        rather than silently feeding "poweroff" to the patch. */
    std::function<void()> onPowerOffRequested;

    /** First address component that starts and stops a recording. Like
        /poweroff this is the app's business and not the patch's -- the patch
        has no idea a file is being written -- so it is intercepted in
        forward() rather than routed in Pd. */
    static constexpr const char* recordSelector = "audiorecord";

    /** Called on the message thread when /audiorecord arrives, with true to
        start and false to stop.

        Unlike /poweroff, the argument is the message: a release (0) means
        "stop", not "ignore this". Null is dropped with a log line, which is
        what the plugin does -- it shares this GUI but has no transport of
        its own to record. */
    std::function<void (bool)> onRecordRequested;

    /** Pd receive symbol the translated messages are sent to. _main.pd has
        an [r formuls-osc-in] wired where [list trim] used to be. */
    static constexpr const char* receiverName = "formuls-osc-in";

    /** Starts listening on the given udp port and forwards to pdToFeed.
        Returns Result::ok(), or a failure naming the port if it could not be
        bound (which usually means another formuls is already running).
        pdToFeed must outlive the bridge. */
    juce::Result start (pd::PdBase& pdToFeed, int udpPort);

    /** Feeds pdToFeed from post() only, with no socket: the embedded GUI
        server hands messages over in-process (EmbeddedGuiServer.h). */
    void startDirect (pd::PdBase& pdToFeed);

    /** Thread-safe. Queues one message; the queue is drained on the message
        thread, through the same forward() a UDP message takes, so the patch
        is still never run from the caller's thread. Dropped when stopped. */
    void post (const juce::OSCMessage& message);

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

    /** Acts on /poweroff and returns true if it did. Called from whichever
        thread the message arrived on: it only creates a file (PowerOff.h)
        and never touches Pd or the UI, so it does not need the message
        thread -- and must not wait for it, because the queue it would wait
        in is exactly what stops it arriving (issue #40). */
    bool interceptPowerOff (const juce::OSCMessage& message);
    void handleAsyncUpdate() override;   // drains `posted`

    /** Watchdog. juce::OSCReceiver's thread returns for good if
        waitUntilReady() ever reports an error (juce_OSCReceiver.cpp, "if
        (ready < 0 ... ) return;"), and says nothing when it does: the bridge
        is then deaf for the lifetime of the process with no log line and no
        visible cause. On a headless appliance that is a brick -- no POWER
        OFF, no record, no control of any kind.

        So the bridge proves to itself that it can still hear. Every tick
        sends one /oscwatchdog to its own port; receiving it clears the flag.
        Two ticks in a row unanswered and the receiver is rebuilt, loudly.
        Never seen in the wild; it costs one datagram a minute and turns an
        invisible, unrecoverable failure into a logged and survivable one. */
    void timerCallback() override;
    bool sendWatchdogProbe();
    void rebuildReceiver();

    static constexpr const char* watchdogAddress = "/oscwatchdog";
    static constexpr int watchdogIntervalMs = 30000;
    static constexpr int watchdogMissesBeforeRebuild = 2;

    int udpPortInUse = 0;             // message thread
    bool probeOutstanding = false;    // message thread
    int consecutiveMisses = 0;        // message thread
    std::atomic<bool> probeSeen { false };

    juce::OSCReceiver receiver;
    bool direct = false;

    juce::CriticalSection postLock;
    std::vector<juce::OSCMessage> posted;
    bool accepting = false;   // under postLock
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
