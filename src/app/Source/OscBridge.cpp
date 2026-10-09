/*
 * OscBridge.cpp -- see OscBridge.h for why this exists at all.
 */

#include "OscBridge.h"
#include "PdBase.hpp"
#include "Diagnostics.h"   // TEMPORARY timing diagnostics

namespace
{
    // TEMPORARY timing diagnostics -- message thread only
    int diagMessages = 0;
    double diagHeldMs = 0.0, diagLongestMs = 0.0;
    juce::String diagLongestAddress;
}

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

    receiver.addListener (this);
    listening = true;
    messageCount = 0;

    juce::Logger::writeToLog ("OSC bridge listening on udp " + juce::String (udpPort));
    return juce::Result::ok();
}

void OscBridge::stop()
{
    if (! listening)
        return;

    receiver.removeListener (this);
    receiver.disconnect();
    listening = false;
    pd = nullptr;

    juce::Logger::writeToLog ("OSC bridge stopped after " + juce::String (messageCount)
                              + " message(s)");
}

void OscBridge::oscBundleReceived (const juce::OSCBundle& bundle)
{
    // The case that used to crash the app: Open Stage Control sends its whole
    // state as one bundle when a fresh client connects. Flattened here, on
    // the message thread, with no stack allocation involved.
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            forward (element.getMessage());
        else if (element.isBundle())
            oscBundleReceived (element.getBundle());
    }
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& message)
{
    forward (message);
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
    const auto heldFrom = juce::Time::getMillisecondCounterHiRes();   // TEMPORARY diagnostics
    pd->finishMessage (receiverName, selector.toStdString());
    ++messageCount;

    // TEMPORARY: how long this message kept Pd (and so the audio thread) locked
    const auto heldMs = juce::Time::getMillisecondCounterHiRes() - heldFrom;
    ++diagMessages;
    diagHeldMs += heldMs;

    if (heldMs > diagLongestMs)
    {
        diagLongestMs = heldMs;
        diagLongestAddress = message.getAddressPattern().toString();
    }

    // every record/reset button message, whatever it cost, with its value: a stray
    // record stop was seen at 15:46:27 that the player did not press
    const auto address = message.getAddressPattern().toString();
    if (address.contains ("record") || address.contains ("reset"))
    {
        juce::String args;
        for (const auto& arg : message)
            args << " " << (arg.isFloat32() ? juce::String (arg.getFloat32()) : arg.isInt32() ? juce::String (arg.getInt32())
                                                                                              : arg.isString() ? arg.getString() : juce::String ("?"));
        Diagnostics::log ("OSC BUTTON  " + address + args + " (held Pd " + juce::String (heldMs, 2) + " ms)", heldFrom);
    }
    else if (heldMs > 1.0)
        Diagnostics::log ("OSC HOLD    " + message.getAddressPattern().toString() + " ("
                          + juce::String (message.size()) + " args) held Pd for "
                          + juce::String (heldMs, 2) + " ms", heldFrom);
}

juce::String OscBridge::takeDiagnosticSummary()
{
    auto s = ", " + juce::String (diagMessages) + " GUI messages holding Pd "
             + juce::String (diagHeldMs, 1) + " ms in total, longest "
             + juce::String (diagLongestMs, 2) + " ms"
             + (diagLongestAddress.isNotEmpty() ? " (" + diagLongestAddress + ")" : juce::String());
    diagMessages = 0; diagHeldMs = 0.0; diagLongestMs = 0.0; diagLongestAddress = {};
    return s;
}

} // namespace formuls
