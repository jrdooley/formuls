/*
 * OpenStageControl.cpp -- see OpenStageControl.h for an overview.
 */

#include "OpenStageControl.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>

namespace formuls
{

namespace fs = std::filesystem;

std::string OpenStageControl::start (const fs::path& resourceRoot)
{
    if (process.isRunning())
        return {};

    const auto guiDir = resourceRoot / "gui";
    const auto node   = guiDir / "node";
    const auto oscDir = guiDir / "open-stage-control";
    const auto layout = guiDir / "_main.json";

    std::error_code ec;

    if (! fs::is_regular_file (node, ec))
        return "Bundled node.js binary not found: " + node.string();
    if (! fs::is_directory (oscDir, ec))
        return "Open Stage Control package not found: " + oscDir.string();
    if (! fs::is_regular_file (layout, ec))
        return "GUI layout not found: " + layout.string();

    // Same command line as the JUCE app (and the old Python front end).
    const std::vector<std::string> argv {
        node.string(),
        oscDir.string(),
        "--send", "127.0.0.1:" + std::to_string (patchOscPort),
        "--port", std::to_string (guiPort),
        "--load", layout.string(),
        "--client-options", "framerate=25", "hdpi=0",
    };

    if (! process.start (argv))
        return "Could not start Open Stage Control (" + node.string() + ")";

    return {};
}

std::vector<std::string> OpenStageControl::getBrowserAddresses()
{
    const auto port = ":" + std::to_string (guiPort);

    // Always first: a browser on this machine.
    std::vector<std::string> addresses { "http://127.0.0.1" + port };

    ifaddrs* list = nullptr;

    if (getifaddrs (&list) != 0)
        return addresses;

    for (auto* ifa = list; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        char text[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*> (ifa->ifa_addr);

        if (inet_ntop (AF_INET, &sin->sin_addr, text, sizeof (text)) == nullptr)
            continue;

        const std::string address = text;

        // 127.x is loopback (already listed above); 169.254.x is a
        // self-assigned address, which no other device can reach.
        if (address.rfind ("127.", 0) == 0 || address.rfind ("169.254.", 0) == 0)
            continue;

        auto url = "http://" + address + port;

        if (std::find (addresses.begin(), addresses.end(), url) == addresses.end())
            addresses.push_back (url);
    }

    freeifaddrs (list);
    return addresses;
}

} // namespace formuls
