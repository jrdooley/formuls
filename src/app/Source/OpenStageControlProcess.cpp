/*
 * OpenStageControlProcess.cpp -- see OpenStageControlProcess.h for an overview.
 */

#include "OpenStageControlProcess.h"

namespace formuls
{

OpenStageControlProcess::~OpenStageControlProcess()
{
    stop();
}

juce::Result OpenStageControlProcess::start (const juce::File& resourceRoot)
{
    if (process.isRunning())
        return juce::Result::ok();

    auto guiDir  = resourceRoot.getChildFile ("gui");
    auto node    = guiDir.getChildFile ("node");
    auto oscDir  = guiDir.getChildFile ("open-stage-control");
    auto layout  = guiDir.getChildFile ("_main.json");

    if (! node.existsAsFile())
        return juce::Result::fail ("Bundled node.js binary not found: "
                                   + node.getFullPathName());
    if (! oscDir.isDirectory())
        return juce::Result::fail ("Open Stage Control package not found: "
                                   + oscDir.getFullPathName());
    if (! layout.existsAsFile())
        return juce::Result::fail ("GUI layout not found: " + layout.getFullPathName());

    // Same command line the Python front end used:
    //   node open-stage-control/ --send 127.0.0.1:9000 --port 9001
    //        --load _main.json --client-options framerate=25 hdpi=0
    const std::vector<std::string> args {
        node.getFullPathName().toStdString(),
        oscDir.getFullPathName().toStdString(),
        // OSC out -> the Pd patch
        "--send", "127.0.0.1:" + std::to_string (patchOscPort),
        // GUI served on this port
        "--port", std::to_string (guiPort),
        "--load", layout.getFullPathName().toStdString(),
        "--client-options", "framerate=25", "hdpi=0" };

    if (! process.start (args))
        return juce::Result::fail ("Could not start Open Stage Control ("
                                   + node.getFullPathName() + ")");

    juce::Logger::writeToLog ("Open Stage Control started on port "
                              + juce::String (guiPort));
    return juce::Result::ok();
}

void OpenStageControlProcess::stop()
{
    if (process.isRunning())
    {
        process.kill();
        juce::Logger::writeToLog ("Open Stage Control stopped");
    }
}

bool OpenStageControlProcess::isRunning()
{
    return process.isRunning();
}

juce::StringArray OpenStageControlProcess::getBrowserAddresses()
{
    const auto port = ":" + juce::String (guiPort);

    // Always first: a browser on this machine.
    juce::StringArray addresses { "http://127.0.0.1" + port };

    for (auto& ip : juce::IPAddress::getAllAddresses (false))   // false = IPv4 only
    {
        if (ip.isNull())
            continue;

        const auto text = ip.toString();

        // 127.x is loopback (already listed above); 169.254.x is a
        // self-assigned address, which no other device can reach.
        if (text.startsWith ("127.") || text.startsWith ("169.254."))
            continue;

        addresses.addIfNotAlreadyThere ("http://" + text + port);
    }

    return addresses;
}

} // namespace formuls
