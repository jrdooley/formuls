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
 * Towards the patch there is no network: messages for the default target
 * (o-s-c's --send 127.0.0.1:9000) go to the `toPatch` callback, which the app
 * points at FormulsEngine::sendToPatch (OscBridge, on the message thread);
 * the patch's own OSC packets come in through receivePatchPacket(), which
 * PdOscOut calls from its background thread. Any other host:port target in
 * the session is still sent over UDP, as o-s-c would.
 *
 * Threading: WebSocket events arrive on each connection's reader thread,
 * patch packets on PdOscOut's thread, app commands on the message thread.
 * All of them go through `lock`.
 */

#pragma once

#include <JuceHeader.h>
#include "WebSocketServer.h"

namespace formuls
{

class EmbeddedGuiServer
{
public:
    using PatchSender = std::function<void (const juce::OSCMessage&)>;

    EmbeddedGuiServer() = default;
    ~EmbeddedGuiServer()   { stop(); }

    /** Serves resourceRoot/gui on tcp httpPort. Messages for the patch go to
        toPatch; defaultTarget is the o-s-c target string they are addressed
        to (the GUI is told it as its server target). */
    juce::Result start (const juce::File& resourceRoot, int httpPort,
                        const juce::String& defaultTarget, PatchSender toPatch);
    void stop();
    bool isRunning() const noexcept   { return running; }

    /** Any thread. One OSC packet (message or bundle) from the patch, as its
        [oscformat] built it. Ignored while stopped. */
    void receivePatchPacket (const juce::uint8* data, int size);

    /** Any thread. A message from the app itself (e.g. /STATE/SAVE), handled
        exactly like one from the patch. */
    void receiveFromApp (const juce::OSCMessage& message);

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

    // Patch / app side. Call with `lock` held.
    void receiveOsc (const juce::String& address, const juce::Array<juce::var>& args);

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
    PatchSender toPatch;

    // Only for session targets other than the patch.
    juce::OSCSender udpOut;
    juce::String udpOutTarget;

    juce::CriticalSection lock;
    std::map<juce::String, std::unique_ptr<Client>> clients;
    std::map<WebSocketServer::Connection*, juce::String> clientByConnection;
    juce::var clipboard;

    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmbeddedGuiServer)
};

} // namespace formuls
