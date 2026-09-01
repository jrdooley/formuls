/*
 * OpenStageControlProcess.h
 *
 * Launches and stops the Open Stage Control server (the formuls GUI) as a
 * child process, replacing the shell commands the old Python front end used.
 *
 * The server is the node-only distribution of Open Stage Control, run with
 * the node.js binary bundled alongside it by the build script:
 *
 *   <resource root>/gui/node                 node.js binary
 *   <resource root>/gui/open-stage-control/  the o-s-c node package
 *   <resource root>/gui/_main.json           the formuls control layout
 *
 * The launch arguments mirror the original Python command line: the GUI is
 * served on port 9001 and forwards control messages to the Pd patch, which
 * listens for OSC on udp port 9000 (see the netreceive/oscparse objects in
 * pd/_main.pd). To change ports or o-s-c options, edit the argument list in
 * OpenStageControlProcess.cpp.
 *
 * The process is started with the shared formuls::ChildProcess rather than
 * juce::ChildProcess, so stopping it (or quitting the app) kills exactly the
 * process this app started -- unlike the old "killall node", which would take
 * down any node process on the machine -- and, crucially, the server is also
 * killed if this app is force-quit or crashes. juce::ChildProcess handles the
 * clean-quit case but exposes no PID to hang a watchdog on, and an orphaned
 * node keeps ports 9000/9001 so the *next* launch's GUI fails to bind.
 * See src/shared/ChildProcess.h.
 */

#pragma once

#include <JuceHeader.h>
#include "ChildProcess.h"

namespace formuls
{

class OpenStageControlProcess
{
public:
    OpenStageControlProcess() = default;
    ~OpenStageControlProcess();

    /** Port the Open Stage Control GUI is served on. Used both for the
        server's --port argument and for the browser addresses shown in the
        main window, so change it here and both follow. */
    static constexpr int guiPort = 9001;

    /** UDP port the Pd patch listens on for OSC from the GUI
        (the [netreceive -u -b] / "listen 9000" objects in _main.pd). */
    static constexpr int patchOscPort = 9000;

    /** Starts the Open Stage Control server using the resources under
        resourceRoot/gui. Returns Result::ok() or a failure with a
        user-displayable message. */
    juce::Result start (const juce::File& resourceRoot);

    /** Kills the server process. Safe to call when not running. */
    void stop();

    bool isRunning();

    /** The web addresses the GUI can be opened at, ready to display.

        The first entry is always the loopback address, for a browser on the
        machine running formuls. Any further entries are this machine's
        addresses on the networks it is attached to -- these are the ones to
        type into an iPad or Android tablet joined to the same network.

        This enumerates the machine's own network interfaces rather than
        parsing the addresses Open Stage Control prints to its stdout, so the
        list does not depend on the wording of another program's log output.
        IPv6 is left out: the GUI is reached over IPv4 in practice, and the
        bracketed IPv6 URL form is awkward to type on a tablet. */
    static juce::StringArray getBrowserAddresses();

private:
    ChildProcess process;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenStageControlProcess)
};

} // namespace formuls
