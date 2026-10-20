/*
 * EmbeddedGuiServer.h
 *
 * Serves the formuls control GUI from inside the app, replacing the node +
 * Open Stage Control child process (OpenStageControlProcess).
 *
 * The browser side is unchanged: the stock Open Stage Control 1.31.0 client
 * (gui/open-stage-control/client) is served as-is, with the same session and
 * default state. What is reimplemented here is the o-s-c *server*:
 *
 *   - the WebSocket IPC (JSON arrays ["event", data]) and its events
 *     open / sessionOpened / addWidget / removeWidget / sendOsc / syncOsc /
 *     stateSave / stateOpen, plus the replies they need;
 *   - the per-client widget table: the browser sends only {h: widget hash,
 *     v: value}, and the server fills in address, preArgs, target and
 *     typeTags from what that client registered with addWidget;
 *   - multi-tablet sync: a widget change is re-delivered to every *other*
 *     client unless the widget has noSync;
 *   - formuls-module.js (oscInFilter / oscOutFilter for the compiled
 *     slider canvases, from gui/compounds.json), ported to C++.
 *
 * Towards the patch it speaks UDP OSC exactly as o-s-c did: it sends to
 * 127.0.0.1:9000 (OscBridge) and listens on udp 9001, where PdOscOut and
 * MainComponent's state save/open commands already send. So nothing on the
 * Pd side changed for this prototype; the OSC hop can be replaced with
 * direct libpd calls later.
 *
 * Threading: WebSocket events arrive on each connection's reader thread,
 * OSC from the patch on the message thread. Both go through `lock`.
 */

#pragma once

#include <JuceHeader.h>
#include "WebSocketServer.h"

namespace formuls
{

class EmbeddedGuiServer : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                          private juce::Timer
{
public:
    EmbeddedGuiServer() = default;
    ~EmbeddedGuiServer() override   { stop(); }

    /** Serves resourceRoot/gui on tcp httpPort and listens for the patch's
        OSC on udp httpPort; sends GUI OSC to 127.0.0.1:patchPort. */
    juce::Result start (const juce::File& resourceRoot, int httpPort, int patchPort);
    void stop();
    bool isRunning() const noexcept   { return running; }

private:
    struct Client
    {
        juce::String id;
        WebSocketServer::ConnectionPtr connection;
        std::map<juce::String, juce::var> widgets;   // hash -> {address, preArgs, target, ...}
        bool connected() const   { return connection != nullptr && connection->isOpen(); }
    };

    // WebSocket side (connection threads)
    WebSocketServer::HttpResponse handleHttp (const WebSocketServer::HttpRequest&);
    void handleOpen (WebSocketServer::ConnectionPtr);
    void handleMessage (WebSocketServer::ConnectionPtr, const juce::String&);
    void handleClose (WebSocketServer::ConnectionPtr);

    void onIpc (Client&, const juce::String& event, const juce::var& data);
    void onSendOsc (Client&, const juce::var& data, bool syncOnly);

    // Patch side (message thread)
    void oscMessageReceived (const juce::OSCMessage&) override;
    void oscBundleReceived (const juce::OSCBundle&) override;

    // "receiveOsc" to clients. Only one of `onlyTo` / `except` is used;
    // neither means everyone.
    void deliver (const juce::var& data, const juce::String& onlyTo = {}, const juce::String& except = {});
    void send (Client&, const juce::String& event, const juce::var& data);
    void sendToPatch (const juce::String& hostPort, const juce::String& address,
                      const juce::Array<juce::var>& args, const juce::String& typeTags);

    // formuls-module.js
    bool moduleInFilter (const juce::String& address, const juce::Array<juce::var>& args);   // false = drop
    bool moduleOutFilter (const juce::String& address, const juce::Array<juce::var>& args,
                          const juce::String& clientId);
    void modulePatch (const juce::var& targets, const juce::Array<juce::var>& args,
                      const juce::String& onlyTo, const juce::String& except = {});

    juce::File clientDir, sessionFile, stateFile;
    juce::var sessionJson, compounds;
    juce::String defaultTarget;          // "127.0.0.1:9000"
    juce::String envJson;                // window.ENV, from --client-options

    WebSocketServer http;
    juce::OSCReceiver fromPatch;
    juce::OSCSender toPatch;
    juce::String toPatchTarget;

    juce::CriticalSection lock;
    std::map<juce::String, std::unique_ptr<Client>> clients;
    std::map<WebSocketServer::Connection*, juce::String> clientByConnection;
    juce::var clipboard;

    // Traffic counters, logged every few seconds while they change.
    void timerCallback() override;
    std::atomic<int> fromPatchCount { 0 }, toPatchCount { 0 }, framesOut { 0 }, clientEvents { 0 };
    int lastLogged[4] = {};
    std::map<juce::String, int> toPatchByAddress, fromPatchByAddress;   // under lock, reset each log

    bool running = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmbeddedGuiServer)
};

} // namespace formuls
