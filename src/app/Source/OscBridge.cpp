/*
 * OscBridge.cpp -- see OscBridge.h for why this exists at all.
 */

#include "OscBridge.h"
#include "PdBase.hpp"

namespace formuls
{

OscBridge::~OscBridge()
{
    stop();
}

juce::Result OscBridge::start (pd::PdBase& pdToFeed, int udpPort)
{
    if (listening)
        return juce::Result::ok();

    pd = &pdToFeed;

    if (! receiver.connect (udpPort))
    {
        pd = nullptr;
        return juce::Result::fail ("Could not listen for GUI messages on udp port "
                                   + juce::String (udpPort)
                                   + " (is another copy of formuls running?)");
    }

    // Open the gate *before* registering the listener. connect() has already
    // called startThread(), so the receiver thread is reading datagrams; the
    // moment addListener() runs, callbacks fire on that thread. Anything that
    // arrives before accepting is true would be dropped by post() silently.
    // The old MessageLoopCallback listener hid this: its callbacks could not
    // run until start() returned, because start() is on the message thread.
    listening = true;
    messageCount = 0;
    {
        const juce::ScopedLock sl (postLock);
        accepting = true;
    }

    receiver.addListener (this);

    udpPortInUse = udpPort;
    probeOutstanding = false;
    consecutiveMisses = 0;
    warnedAboutProbeSend = false;
    probeSeen = false;
    startTimer (watchdogIntervalMs);

    juce::Logger::writeToLog ("OSC bridge listening on udp " + juce::String (udpPort));
    return juce::Result::ok();
}

void OscBridge::startDirect (pd::PdBase& pdToFeed)
{
    if (listening)
        return;

    pd = &pdToFeed;
    direct = true;
    listening = true;
    messageCount = 0;

    const juce::ScopedLock sl (postLock);
    accepting = true;
    juce::Logger::writeToLog ("OSC bridge fed in-process (no udp)");
}

void OscBridge::post (const juce::OSCMessage& message)
{
    const juce::ScopedLock sl (postLock);
    if (! accepting)
        return;
    posted.push_back (message);
    triggerAsyncUpdate();
}

void OscBridge::handleAsyncUpdate()
{
    std::vector<juce::OSCMessage> batch;
    {
        const juce::ScopedLock sl (postLock);
        batch.swap (posted);
    }

    for (auto& message : batch)
        forward (message);
}

void OscBridge::stop()
{
    if (! listening)
        return;

    {
        const juce::ScopedLock sl (postLock);
        accepting = false;
        posted.clear();
    }
    cancelPendingUpdate();
    stopTimer();

    if (! direct)
    {
        receiver.removeListener (this);
        receiver.disconnect();
    }
    direct = false;
    listening = false;
    pd = nullptr;

    juce::Logger::writeToLog ("OSC bridge stopped after " + juce::String (messageCount)
                              + " message(s)");
}

bool OscBridge::interceptPowerOff (const juce::OSCMessage& message)
{
    auto path = juce::StringArray::fromTokens (message.getAddressPattern().toString(), "/", "");
    path.removeEmptyStrings();

    if (path.isEmpty() || path[0] != powerOffSelector)
        return false;

    // The same gate post() uses. Once stop() has closed the bridge, a
    // datagram still in flight on the receiver thread must not power the
    // machine off -- a Stop is not a shutdown. The old MessageLoopCallback
    // listener gave this for free: stop() runs on the message thread, so a
    // queued callback could not run until stop() had finished, by which
    // point forward()'s pd == nullptr check dropped it. The receiver thread
    // has no such ordering, so the gate has to be explicit.
    {
        const juce::ScopedLock sl (postLock);

        if (! accepting)
            return true;   // it is a /poweroff, just not an actionable one
    }

    // A button in "push" mode sends 1 on press and 0 on release, so act on
    // the press only.
    bool pressed = true;

    if (message.size() > 0)
    {
        const auto& first = message[0];

        if (first.isFloat32())    pressed = first.getFloat32() != 0.0f;
        else if (first.isInt32()) pressed = first.getInt32() != 0;
    }

    if (! pressed)
        return true;

    if (onPowerOffRequested != nullptr)
    {
        juce::Logger::writeToLog ("OSC bridge: power off requested by the GUI");
        onPowerOffRequested();
    }
    else
    {
        juce::Logger::writeToLog ("OSC bridge: ignoring /poweroff -- no handler is set");
    }

    return true;
}

bool OscBridge::sendWatchdogProbe()
{
    juce::OSCSender sender;

    if (! sender.connect ("127.0.0.1", udpPortInUse))
        return false;

    return sender.send (juce::OSCMessage (juce::OSCAddressPattern (watchdogAddress)));
}

void OscBridge::rebuildReceiver()
{
    juce::Logger::writeToLog ("OSC bridge: receiver stopped answering -- rebuilding it");

    receiver.removeListener (this);
    receiver.disconnect();

    if (receiver.connect (udpPortInUse))
    {
        receiver.addListener (this);
        juce::Logger::writeToLog ("OSC bridge: receiver rebuilt on udp "
                                  + juce::String (udpPortInUse));
    }
    else
    {
        juce::Logger::writeToLog ("OSC bridge: could NOT rebind udp "
                                  + juce::String (udpPortInUse)
                                  + " -- the GUI cannot reach this machine");
    }
}

void OscBridge::timerCallback()
{
    // Judge the probe sent last tick before sending another, so a starved
    // message thread delays detection rather than inventing a miss.
    if (probeOutstanding)
    {
        if (probeSeen.exchange (false))
        {
            consecutiveMisses = 0;
        }
        else if (++consecutiveMisses >= watchdogMissesBeforeRebuild)
        {
            consecutiveMisses = 0;
            probeOutstanding = false;
            rebuildReceiver();
            return;   // give the new receiver a full tick before judging it
        }
    }

    // A probe that could not be sent says nothing about the receiver, so it
    // is not counted against it -- but it does mean nothing is being
    // guarded, and that has to be said out loud. Otherwise a watchdog that
    // cannot send (no fds left, say -- the kind of state it exists for)
    // sits there judging nothing, as quiet as the fault it is watching for.
    const bool sent = sendWatchdogProbe();

    if (! sent && ! warnedAboutProbeSend)
    {
        warnedAboutProbeSend = true;
        juce::Logger::writeToLog ("OSC bridge: cannot send the watchdog probe to udp "
                                  + juce::String (udpPortInUse)
                                  + " -- the receiver is no longer being checked");
    }
    else if (sent && warnedAboutProbeSend)
    {
        warnedAboutProbeSend = false;
        juce::Logger::writeToLog ("OSC bridge: watchdog probe sending again");
    }

    probeOutstanding = sent;
}

void OscBridge::oscBundleReceived (const juce::OSCBundle& bundle)
{
    // The case that used to crash the app: Open Stage Control sends its whole
    // state as one bundle when a fresh client connects. Flattened here, on
    // the message thread, with no stack allocation involved.
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            oscMessageReceived (element.getMessage());
        else if (element.isBundle())
            oscBundleReceived (element.getBundle());
    }
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& message)
{
    // This runs on the receiver's own thread (RealtimeCallback), not the
    // message thread. Nothing here may touch Pd or the UI.

    // The watchdog's own probe. Answered here and dropped: the patch has no
    // use for it, and routing it through Pd would make the health check
    // depend on the thing it is checking.
    if (message.getAddressPattern().toString() == watchdogAddress)
    {
        probeSeen = true;
        return;
    }

    if (interceptPowerOff (message))
        return;

    // post() batches: the whole queue drains in one message-thread wakeup,
    // where a MessageLoopCallback listener cost one wakeup per datagram.
    // Under a talkative GUI that difference is the whole of issue #40.
    post (message);
}

void OscBridge::forward (const juce::OSCMessage& message)
{
    if (pd == nullptr)
        return;

    // Reproduce what [oscparse] -> [list trim] used to put into the patch:
    // the address path becomes leading symbols, the arguments follow, and
    // the first path component becomes the message selector.
    //
    //     /foo/bar 1 "x"   ->   selector "foo", arguments: bar 1 x
    //
    // Everything downstream of the [route] in _main.pd sees exactly what it
    // saw before, so nothing in the patch had to change to suit this.
    auto path = juce::StringArray::fromTokens (message.getAddressPattern().toString(), "/", "");
    path.removeEmptyStrings();

    if (path.isEmpty())
    {
        if (! warnedAboutAddress)
        {
            warnedAboutAddress = true;
            juce::Logger::writeToLog ("OSC bridge: ignoring message with an empty address");
        }

        return;
    }

    const auto selector = path[0];

    // /poweroff is for the machine, not the patch. Intercepted rather than
    // routed in Pd: the patch cannot power anything off, and giving its
    // [route] a silent branch would hide where this actually happens.
    //
    // On the UDP path this already ran on the receiver thread; this is the
    // in-process path (EmbeddedGuiServer -> post()), which arrives here.
    if (interceptPowerOff (message))
        return;

    // /audiorecord is the app's business too: the patch does not know a file
    // is being written, and nothing in it would change if it did.
    if (selector == recordSelector)
    {
        // Here the argument IS the message, so a missing one is a protocol
        // error rather than something to assume about. A toggle sends 1/0; a
        // push button sends 1 then 0, which would start and immediately stop
        // a take -- so the GUI's button is a toggle, and this trusts it.
        bool shouldRecord = false;
        bool haveValue = false;

        if (message.size() > 0)
        {
            const auto& first = message[0];

            if (first.isFloat32())    { shouldRecord = first.getFloat32() != 0.0f; haveValue = true; }
            else if (first.isInt32()) { shouldRecord = first.getInt32() != 0;      haveValue = true; }
        }

        if (! haveValue)
        {
            juce::Logger::writeToLog ("OSC bridge: ignoring /audiorecord with no "
                                      "numeric argument -- cannot tell start from stop");
            return;
        }

        if (onRecordRequested != nullptr)
        {
            juce::Logger::writeToLog (juce::String ("OSC bridge: recording ")
                                      + (shouldRecord ? "start" : "stop")
                                      + " requested by the GUI");
            onRecordRequested (shouldRecord);
        }
        else
        {
            juce::Logger::writeToLog ("OSC bridge: ignoring /audiorecord -- no handler is set");
        }

        return;
    }

    pd->startMessage();

    for (int i = 1; i < path.size(); ++i)
        pd->addSymbol (path[i].toStdString());

    for (const auto& arg : message)
    {
        // Pd numbers are all floats, so the integer types collapse into one.
        if (arg.isFloat32())        pd->addFloat (arg.getFloat32());
        else if (arg.isInt32())     pd->addFloat ((float) arg.getInt32());
        else if (arg.isString())    pd->addSymbol (arg.getString().toStdString());
        else if (arg.isColour())    pd->addFloat ((float) arg.getColour().toInt32());
        else
        {
            // Blobs and the time-tag types have no sensible Pd equivalent
            // here. oscparse turned a blob into one atom per byte, which is
            // the behaviour that made the old path dangerous; dropping it is
            // both safer and closer to what the GUI actually sends.
            if (! warnedAboutArgumentType)
            {
                warnedAboutArgumentType = true;
                juce::Logger::writeToLog ("OSC bridge: dropping an argument of an "
                                          "unsupported type (first occurrence only), "
                                          "address " + message.getAddressPattern().toString());
            }
        }
    }

    // finishMessage -> libpd_message, which takes sys_lock() itself and then
    // runs pd_typedmess() on this thread. That is the whole point of the
    // exercise: this is the message thread, not the audio thread.
    pd->finishMessage (receiverName, selector.toStdString());
    ++messageCount;
}

} // namespace formuls
