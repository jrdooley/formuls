/*
 * OpenStageControl.h -- the à la carte counterpart of the JUCE app's
 * OpenStageControlProcess. Same job, same arguments, same ports; the
 * process handling is our own ChildProcess (posix_spawn) instead of
 * juce::ChildProcess, and the browser-address list comes from getifaddrs()
 * instead of juce::IPAddress.
 */

#pragma once

#include "ChildProcess.h"

#include <filesystem>
#include <string>
#include <vector>

namespace formuls
{

class OpenStageControl
{
public:
    /** Port the Open Stage Control GUI is served on (also used for the
        browser addresses below). */
    static constexpr int guiPort = 9001;

    /** UDP port the Pd patch listens on for OSC from the GUI. */
    static constexpr int patchOscPort = 9000;

    /** Starts the server from <resourceRoot>/gui. Returns "" on success or
        a user-displayable error message. */
    std::string start (const std::filesystem::path& resourceRoot);

    /** Kills the server process. Safe to call when not running. */
    void stop()          { process.kill(); }

    bool isRunning()     { return process.isRunning(); }

    /** The web addresses the GUI can be opened at: loopback first (for a
        browser on this machine), then this machine's IPv4 address on each
        attached network (for a tablet/phone on the same network).
        Self-assigned 169.254.x addresses are skipped; IPv6 is omitted. */
    static std::vector<std::string> getBrowserAddresses();

private:
    ChildProcess process;
};

} // namespace formuls
