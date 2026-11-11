// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

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

    receiver.addListener (this);
    listening = true;
    messageCount = 0;
    {
        const juce::ScopedLock sl (postLock);
        accepting = true;
    }

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

    // /poweroff is for the machine, not the patch. Intercepted here rather
    // than routed in Pd: the patch cannot power anything off, and giving its
    // [route] a silent branch would hide where this actually happens.
    if (selector == powerOffSelector)
    {
        // A button in "push" mode sends 1 on press and 0 on release, so act
        // on the press only. Checking the argument rather than requiring a
        // particular button mode means the GUI can use push, tap or toggle
        // and this still fires exactly once.
        bool pressed = true;

        if (message.size() > 0)
        {
            const auto& first = message[0];

            if (first.isFloat32())    pressed = first.getFloat32() != 0.0f;
            else if (first.isInt32()) pressed = first.getInt32() != 0;
        }

        if (! pressed)
            return;

        if (onPowerOffRequested != nullptr)
        {
            juce::Logger::writeToLog ("OSC bridge: power off requested by the GUI");
            onPowerOffRequested();
        }
        else
        {
            juce::Logger::writeToLog ("OSC bridge: ignoring /poweroff -- no handler is set");
        }

        return;
    }

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
