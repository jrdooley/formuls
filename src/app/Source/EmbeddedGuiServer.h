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

class EmbeddedGuiServer : private juce::Timer
{
public:
    using PatchSender = std::function<void (const juce::OSCMessage&)>;

    EmbeddedGuiServer() = default;
    ~EmbeddedGuiServer() override   { stop(); }

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

    //==========================================================================
    // State kept by the server, for a plugin to save with the host project.
    // It has two parts:
    //
    //   - the GUI state: what the tablets show. o-s-c keeps widget values in
    //     the browser, so after a tablet's change the server asks one
    //     connected tablet for its state (o-s-c's /STATE/SAVE, answered with a
    //     stateSave the server keeps instead of writing a file), at most once
    //     a second; it can be a couple of seconds behind the last gesture;
    //   - the patch state: the last message the patch was sent on each
    //     address, recorded as it goes by. Restoring replays exactly those, so
    //     the patch is back where it was without a tablet, and without
    //     re-deriving OSC from the session (matrix children, rounding to each
    //     widget's decimals and string values all live in the browser). An
    //     address never sent keeps the patch's own default, as it did before.
    //
    // The patch's own poll (/GET) and /STATE/* commands are neither changes
    // nor recorded.

    /** Message thread, before start(). Off by default (the app). */
    void setStateCapture (bool shouldCapture)   { capture = shouldCapture; }

    /** Any thread. The GUI state as o-s-c state JSON ({widget id: value});
        empty if none has been captured or restored yet. */
    juce::String getStateJson() const;

    /** Any thread. The patch state as JSON: [[address, args...], ...] in the
        order the addresses were first sent; empty if nothing was sent. */
    juce::String getPatchStateJson() const;

    /** Message thread. Restores both parts: the patch messages are replayed
        to the patch (if running), open tablets are told to show the GUI state
        without sending it, and tablets that connect later load it instead of
        the default state. Either part may be empty. */
    void setState (const juce::String& guiJson, const juce::String& patchJson);

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

    // State capture / restore. Call with `lock` held, except timerCallback.
    void timerCallback() override;
    void rememberPatchMessage (const juce::String& address, const juce::OSCMessage&);

    // "receiveOsc" to clients. Only one of `onlyTo` / `except` is used;
    // neither means everyone.
    void deliver (const juce::var& data, const juce::String& onlyTo = {}, const juce::String& except = {});
    void send (Client&, const juce::String& event, const juce::var& data);

    // Resync. A tablet that reconnects (Wi-Fi dropout, or dropped by
    // WebSocketServer for falling behind) missed whatever was delivered while
    // it was away, and the o-s-c client only asks for state when its page
    // loads. So the last broadcast frame for each widget is kept and replayed
    // to it on reconnect. Display only: nothing is sent to the patch.
    void rememberForResync (const juce::var& data, const juce::String& text);
    juce::String resyncKey (const juce::String& address, const juce::var& args) const;
    void resync (Client&);
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

    bool capture = false;
    juce::String stateJson;                   // under lock
    std::map<juce::String, juce::Array<juce::var>> patchValues;   // address -> last args
    juce::StringArray patchOrder;             // first-sent order
    bool replaying = false;
    bool stateDirty = false, captureInFlight = false;
    juce::String lastChangedBy;               // client id
    juce::uint32 captureRequestedAt = 0;
    static constexpr const char* captureSentinelPath = "__formuls_host_project__.state";

    struct ResyncEntry { juce::uint64 seq; juce::String text; };
    std::map<juce::String, ResyncEntry> resyncFrames;   // resyncKey -> last frame
    juce::uint64 resyncSeq = 0;
    // address -> the distinct preArgs of widgets on it, from every addWidget
    std::map<juce::String, std::vector<juce::Array<juce::var>>> widgetPreArgs;

    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmbeddedGuiServer)
};

} // namespace formuls
