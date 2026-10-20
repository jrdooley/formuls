/*
 * EmbeddedGuiServer.cpp -- see EmbeddedGuiServer.h.
 *
 * Comments of the form `o-s-c: ...` name the Open Stage Control 1.31.0 server
 * code (server/node/index.js, class Callbacks) that a function reproduces.
 */

#include "EmbeddedGuiServer.h"

namespace formuls
{

namespace
{
    juce::var obj()   { return juce::var (new juce::DynamicObject()); }

    juce::String toJson (const juce::var& v)   { return juce::JSON::toString (v, true); }

    juce::String frame (const juce::String& event, const juce::var& data)
    {
        juce::Array<juce::var> a { juce::var (event), data };
        return toJson (juce::var (a));
    }

    bool has (const juce::var& o, const char* key)
    {
        auto* d = o.getDynamicObject();
        return d != nullptr && d->hasProperty (key);
    }

    // JS Array.prototype.concat: appends v, or v's elements if v is an array.
    void concatInto (juce::Array<juce::var>& out, const juce::var& v)
    {
        if (auto* a = v.getArray()) out.addArray (*a);
        else                        out.add (v);
    }

    juce::String contentTypeFor (const juce::File& f)
    {
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".html") return "text/html; charset=utf-8";
        if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
        if (ext == ".css")  return "text/css; charset=utf-8";
        if (ext == ".json" || ext == ".map") return "application/json";
        if (ext == ".png")  return "image/png";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".ico")  return "image/x-icon";
        if (ext == ".woff") return "font/woff";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".ttf")  return "font/ttf";
        return "application/octet-stream";
    }

    // formuls-module.js quantise(): a stepped fader snaps to the nearest step,
    // an exact tie going to the lower one. Range 0..1.
    double quantise (double v, int n)
    {
        double best = 0, bd = std::numeric_limits<double>::infinity();
        for (int u = 0; u < n; ++u)
        {
            const double s = (double) u / (n - 1), d = std::abs (s - v);
            if (d < bd) { bd = d; best = s; }
        }
        return best;
    }
}

//==============================================================================
juce::Result EmbeddedGuiServer::start (const juce::File& resourceRoot, int httpPort,
                                       const juce::String& patchTarget, PatchSender sender)
{
    stop();

    const auto guiDir = resourceRoot.getChildFile ("gui");
    clientDir   = guiDir.getChildFile ("open-stage-control");
    sessionFile = guiDir.getChildFile ("_main.json");
    stateFile   = guiDir.getChildFile ("_formuls-default.state");

    if (! clientDir.getChildFile ("client").getChildFile ("index.html").existsAsFile())
        return juce::Result::fail ("Open Stage Control client not found in " + clientDir.getFullPathName());

    sessionJson = juce::JSON::parse (sessionFile);
    if (! sessionJson.isObject())
        return juce::Result::fail ("Could not read GUI session " + sessionFile.getFullPathName());

    // Same rule as OpenStageControlProcess: only a build has the compounds map,
    // and without it the module is not loaded at all.
    compounds = juce::var();
    if (auto map = guiDir.getChildFile ("compounds.json"); map.existsAsFile())
        compounds = juce::JSON::parse (map);

    defaultTarget = patchTarget;
    toPatch = std::move (sender);
    envJson = R"({"framerate":"25","hdpi":"0"})";   // --client-options framerate=25 hdpi=0

    http.onHttpRequest = [this] (auto& r) { return handleHttp (r); };
    http.onOpen    = [this] (auto c) { handleOpen (c); };
    http.onMessage = [this] (auto c, auto& m) { handleMessage (c, m); };
    http.onClose   = [this] (auto c) { handleClose (c); };

    if (! http.start (httpPort))
        return juce::Result::fail ("Could not serve the GUI on port " + juce::String (httpPort)
                                   + " (is another copy of formuls running?)");

    running = true;
    juce::Logger::writeToLog ("Embedded GUI server on port " + juce::String (httpPort)
                              + (compounds.isObject() ? " (compounds module on)" : ""));
    return juce::Result::ok();
}

void EmbeddedGuiServer::stop()
{
    if (! running)
        return;

    running = false;
    http.stop();   // joins every connection thread, so no handler runs after this

    const juce::ScopedLock sl (lock);   // waits out a packet or app command in flight
    udpOut.disconnect();
    udpOutTarget = {};
    toPatch = nullptr;
    clients.clear();
    clientByConnection.clear();
}

//==============================================================================
// HTTP. o-s-c: WebServer's request handler.
WebSocketServer::HttpResponse EmbeddedGuiServer::handleHttp (const WebSocketServer::HttpRequest& request)
{
    WebSocketServer::HttpResponse response;
    const auto path = request.path.upToFirstOccurrenceOf ("?", false, false);

    if (path == "/")
    {
        // index.html with the globals o-s-c injects before </body>.
        auto html = clientDir.getChildFile ("client").getChildFile ("index.html").loadFileAsString();
        juce::String globals;
        globals << "<script>\n"
                << "window.IP=" << toJson (request.remoteAddress) << "\n"
                << "window.ENV=" << envJson << "\n"
                << "window.READ_ONLY=true\n"        // --read-only
                << "window.KIOSK=false\n"
                << "window.AUTH=``\n"
                << "</script></body>";
        html = html.replaceFirstOccurrenceOf ("</body>", globals);

        response.status = 200;
        response.contentType = "text/html; charset=utf-8";
        response.body.append (html.toRawUTF8(), std::strlen (html.toRawUTF8()));
        response.extraHeaders.set ("Cache-Control", "no-store");
        return response;
    }

    if (path.contains ("__APP_THEME__.css"))   // no --theme
    {
        response.status = 200;
        response.contentType = "text/css";
        return response;
    }

    if (path.startsWith ("/__APP_DIR__/"))
    {
        const auto rel = juce::URL::removeEscapeChars (path.fromFirstOccurrenceOf ("/__APP_DIR__/", false, false));
        const auto file = clientDir.getChildFile (rel);

        if (file.isAChildOf (clientDir) && file.existsAsFile() && file.loadFileAsData (response.body))
        {
            response.status = 200;
            response.contentType = contentTypeFor (file);
        }
        return response;
    }

    if (path.contains ("/osc-ping"))
        response.status = 200;
    else
        response.status = 404;   // o-s-c also serves files beside the session; formuls has none

    return response;
}

//==============================================================================
// WebSocket. o-s-c: IpcServer + Client.
void EmbeddedGuiServer::handleOpen (WebSocketServer::ConnectionPtr connection)
{
    // The client connects to /<clientId>/<auth> and keeps its id across reconnects.
    auto parts = juce::StringArray::fromTokens (connection->path, "/", "");
    parts.removeEmptyStrings();
    const auto id = parts.size() >= 1
                        ? juce::URL::removeEscapeChars (parts.size() >= 2 ? parts[parts.size() - 2] : parts[0])
                        : juce::String ("anonymous");

    const juce::ScopedLock sl (lock);
    auto& client = clients[id];

    if (client == nullptr)
    {
        client = std::make_unique<Client>();
        client->id = id;
        // o-s-c: Callbacks.created
        auto console = obj();
        console.getDynamicObject()->setProperty ("typeTags", "");
        client->widgets["CONSOLE"] = console;
    }

    client->connection = connection;
    clientByConnection[connection.get()] = id;
    juce::Logger::writeToLog ("GUI client " + id + " connected from " + connection->remoteAddress);
}

void EmbeddedGuiServer::handleClose (WebSocketServer::ConnectionPtr connection)
{
    const juce::ScopedLock sl (lock);
    // The client and its widget table stay: a tablet that drops off Wi-Fi
    // reconnects with the same id and expects the server to remember it.
    clientByConnection.erase (connection.get());
}

void EmbeddedGuiServer::handleMessage (WebSocketServer::ConnectionPtr connection, const juce::String& text)
{
    const auto message = juce::JSON::parse (text);
    auto* array = message.getArray();

    if (array == nullptr || array->isEmpty() || ! array->getFirst().isString())
        return;

    const auto event = array->getFirst().toString();

    if (event == "ping")   // the client drops the link if this takes > 3 s
    {
        connection->sendText (R"(["pong"])");
        return;
    }

    const juce::ScopedLock sl (lock);
    auto found = clientByConnection.find (connection.get());
    if (found == clientByConnection.end())
        return;

    onIpc (*clients[found->second], event, array->size() > 1 ? (*array)[1] : juce::var());
}

void EmbeddedGuiServer::send (Client& client, const juce::String& event, const juce::var& data)
{
    if (client.connected())
        client.connection->sendText (frame (event, data));
}

void EmbeddedGuiServer::onIpc (Client& client, const juce::String& event, const juce::var& data)
{
    if (event == "sendOsc" || event == "syncOsc")
    {
        onSendOsc (client, data, event == "syncOsc");
    }
    else if (event == "addWidget")
    {
        // o-s-c: Callbacks.addWidget -- target and preArgs are stored as
        // arrays ("" becomes []), everything else as sent.
        auto& entry = client.widgets[data["hash"].toString()];
        if (! entry.isObject())
            entry = obj();

        if (auto* props = data["data"].getDynamicObject())
        {
            for (auto& p : props->getProperties())
            {
                auto value = p.value;
                if (p.name == juce::Identifier ("target") || p.name == juce::Identifier ("preArgs"))
                {
                    if (value.isString() && value.toString().isEmpty()) value = juce::Array<juce::var>();
                    else if (! value.isArray()) value = juce::Array<juce::var> { value };
                }
                entry.getDynamicObject()->setProperty (p.name, value);
            }
            entry.getDynamicObject()->setProperty ("_rawTarget", data["data"]["target"]);
        }
    }
    else if (event == "removeWidget")
    {
        client.widgets.erase (data["hash"].toString());
    }
    else if (event == "open")
    {
        // o-s-c: Callbacks.open, with --load set
        send (client, "connected", {});
        send (client, "sessionList", juce::Array<juce::var>());
        send (client, "clipboard", clipboard.isObject() ? clipboard : [] {
            auto c = obj();
            c.getDynamicObject()->setProperty ("clipboard", {});
            c.getDynamicObject()->setProperty ("idClipboard", {});
            return c;
        }());
        send (client, "serverTargets", juce::Array<juce::var> { defaultTarget });

        if (! (bool) data["hotReload"])
        {
            auto payload = obj();
            payload.getDynamicObject()->setProperty ("path", sessionFile.getFullPathName());
            payload.getDynamicObject()->setProperty ("fileContent", sessionJson);
            send (client, "sessionOpen", payload);
        }
    }
    else if (event == "sessionOpened")
    {
        // o-s-c: Callbacks.sessionOpened, with --state set. The state is only
        // *sent* (to the patch) by the first client, so a second tablet joining
        // does not reset the instrument; it takes the others' values instead.
        if (stateFile.existsAsFile())
        {
            bool alone = true;
            for (auto& [id, other] : clients)
                if (id != client.id && other->connected())
                    alone = false;

            auto payload = obj();
            payload.getDynamicObject()->setProperty ("state", juce::JSON::parse (stateFile));
            payload.getDynamicObject()->setProperty ("send", alone);
            send (client, "stateLoad", payload);
        }

        if ((bool) data["sync"])
            for (auto& [id, other] : clients)
                if (id != client.id)
                    send (*other, "stateSend", {});

        send (client, "setTitle", sessionFile.getFileName());
    }
    else if (event == "stateSave")
    {
        // Sent back by the client after MainComponent's /STATE/SAVE.
        const juce::File file (data["path"].toString());
        if (file.hasFileExtension ("state") && file.replaceWithText (data["session"].toString()))
            juce::Logger::writeToLog ("GUI state saved to " + file.getFullPathName());
        else
            juce::Logger::writeToLog ("GUI state could not be saved to " + file.getFullPathName());
    }
    else if (event == "stateOpen")
    {
        const juce::File file (data["path"].toString());
        auto state = juce::JSON::parse (file);
        if (state.isObject() || state.isArray())
        {
            auto payload = obj();
            payload.getDynamicObject()->setProperty ("state", state);
            payload.getDynamicObject()->setProperty ("path", file.getFullPathName());
            payload.getDynamicObject()->setProperty ("send", true);
            send (client, "stateLoad", payload);
        }
    }
    else if (event == "clipboard")
    {
        clipboard = data;
        for (auto& [id, other] : clients)
            if (id != client.id)
                send (*other, "clipboard", data);
    }
    else if (event == "errorLog" || event == "log" || event == "error")
    {
        juce::Logger::writeToLog ("GUI client " + client.id + ": " + toJson (data));
    }
    // Everything else (sessionSave, listDir, fragmentLoad, setOptions...) is
    // the editor's, and the GUI is served read-only.
}

void EmbeddedGuiServer::onSendOsc (Client& client, const juce::var& t, bool syncOnly)
{
    // o-s-c: Callbacks.sendOsc / syncOsc.
    const auto hash = t["h"].toString();
    const bool hashed = has (t, "h") && hash.isNotEmpty();

    if (hashed && client.widgets.count (hash) == 0)
        return;

    auto i = hashed ? client.widgets[hash].clone() : obj();
    if (auto* props = t.getDynamicObject())
        for (auto& p : props->getProperties())
            i.getDynamicObject()->setProperty (p.name, p.value);

    juce::Array<juce::var> args;
    if (auto* pre = i["preArgs"].getArray()) { args.addArray (*pre); concatInto (args, t["v"]); }
    else                                       args.add (t["v"]);
    i.getDynamicObject()->setProperty ("args", args);

    const bool noSync = (bool) i["noSync"];

    if (syncOnly)
    {
        i.getDynamicObject()->removeProperty ("target");
        if (! noSync) deliver (i, {}, client.id);
        return;
    }

    if (has (i, "target") && ! i["target"].isVoid())
    {
        juce::StringArray targets;
        if (! (bool) t["i"] && ! has (t, "target"))
            targets.add (defaultTarget);                 // --send 127.0.0.1:9000
        if (auto* own = i["target"].getArray())
            for (auto& x : *own)
                targets.add (x.isVoid() ? juce::String ("null") : x.toString());

        const auto address = i["address"].toString();

        for (auto& target : targets)
        {
            if (target == "self")
                deliver (i, client.id);
            else if (target != "null" && target.contains (":")
                     && moduleOutFilter (address, args, client.id))
                sendToPatch (target, address, args, i["typeTags"].toString());
        }
    }

    if (! noSync)
        deliver (i, {}, client.id);
}

void EmbeddedGuiServer::deliver (const juce::var& data, const juce::String& onlyTo, const juce::String& except)
{
    // Serialised once for every client (what patch-osc-perf.py does to o-s-c).
    const auto text = frame ("receiveOsc", data);
    const auto utf8 = text.toRawUTF8();
    const auto size = std::strlen (utf8);

    for (auto& [id, client] : clients)
    {
        if ((onlyTo.isNotEmpty() && id != onlyTo) || (except.isNotEmpty() && id == except))
            continue;
        if (client->connected())
            client->connection->sendText (utf8, size);
    }
}

//==============================================================================
// Patch side.
void EmbeddedGuiServer::sendToPatch (const juce::String& hostPort, const juce::String& address,
                                     const juce::Array<juce::var>& args, const juce::String& typeTags)
{
    try
    {
        juce::OSCMessage message { juce::OSCAddressPattern (address) };
        int index = 0;

        std::function<void (const juce::var&)> add = [&] (const juce::var& v)
        {
            if (auto* a = v.getArray()) { for (auto& x : *a) add (x); return; }

            const auto tag = index < typeTags.length() ? typeTags[index] : 'f';
            ++index;

            if (v.isString())                      message.addString (v.toString());
            else if (v.isBool())                   message.addInt32 ((bool) v ? 1 : 0);
            else if (tag == 'i')                   message.addInt32 ((int) std::lround ((double) v));
            else if (! v.isVoid())                 message.addFloat32 ((float) (double) v);
        };

        for (auto& a : args)
            add (a);

        if (hostPort == defaultTarget)
        {
            if (toPatch) toPatch (message);   // in-process: FormulsEngine::sendToPatch
            return;
        }

        // Some other target in the session: send it over UDP, as o-s-c would.
        if (hostPort != udpOutTarget)
        {
            udpOut.disconnect();
            udpOut.connect (hostPort.upToLastOccurrenceOf (":", false, false),
                            hostPort.fromLastOccurrenceOf (":", false, false).getIntValue());
            udpOutTarget = hostPort;
        }
        udpOut.send (message);
    }
    catch (const juce::OSCException& e)
    {
        juce::Logger::writeToLog ("Embedded GUI: not sending " + address + " (" + e.description + ")");
    }
}

namespace
{
    // A minimal OSC 1.0 reader for what [oscformat] produces: messages (and
    // bundles of them) with f / i / s arguments. Blobs and the data-less tags
    // are skipped; anything else ends the message. JUCE keeps its own parser
    // private, and this runs on PdOscOut's thread, so it must not throw.
    struct OscReader
    {
        const juce::uint8* p;
        const juce::uint8* end;

        bool string (juce::String& out)
        {
            auto* start = p;
            while (p < end && *p != 0) ++p;
            if (p >= end) return false;
            out = juce::String::fromUTF8 ((const char*) start, (int) (p - start));
            p = start + ((p - start) / 4 + 1) * 4;   // NUL included, padded to 4
            return p <= end;
        }

        bool int32 (juce::int32& out)
        {
            if (end - p < 4) return false;
            out = (juce::int32) ((juce::uint32) p[0] << 24 | (juce::uint32) p[1] << 16
                                 | (juce::uint32) p[2] << 8 | p[3]);
            p += 4;
            return true;
        }
    };

    template <typename Callback>
    void readOscPacket (const juce::uint8* data, int size, Callback&& onMessage, int depth = 0)
    {
        OscReader r { data, data + size };
        juce::String address;
        if (depth > 8 || ! r.string (address))
            return;

        if (address == "#bundle")
        {
            r.p += 8;   // time tag
            juce::int32 length;
            while (r.p < r.end && r.int32 (length) && length > 0 && length <= r.end - r.p)
            {
                readOscPacket (r.p, length, onMessage, depth + 1);
                r.p += length;
            }
            return;
        }

        juce::String tags;
        juce::Array<juce::var> args;

        if (r.p < r.end && r.string (tags) && tags.startsWithChar (','))
        {
            for (int i = 1; i < tags.length(); ++i)
            {
                const auto tag = tags[i];
                juce::int32 n;

                if (tag == 'f')      { if (! r.int32 (n)) break; float f; std::memcpy (&f, &n, 4); args.add ((double) f); }
                else if (tag == 'i') { if (! r.int32 (n)) break; args.add ((int) n); }
                else if (tag == 's') { juce::String s; if (! r.string (s)) break; args.add (s); }
                else if (tag == 'b') { if (! r.int32 (n) || n < 0 || n > r.end - r.p) break; r.p += (n + 3) / 4 * 4; }
                else if (tag == 'T' || tag == 'F' || tag == 'N' || tag == 'I') continue;
                else break;
            }
        }

        onMessage (address, args);
    }
}

void EmbeddedGuiServer::receivePatchPacket (const juce::uint8* data, int size)
{
    if (! running)
        return;

    const juce::ScopedLock sl (lock);
    readOscPacket (data, size, [this] (const juce::String& address, const juce::Array<juce::var>& args)
    {
        receiveOsc (address, args);
    });
}

void EmbeddedGuiServer::receiveFromApp (const juce::OSCMessage& message)
{
    if (! running)
        return;

    juce::Array<juce::var> args;
    for (auto& a : message)
    {
        if (a.isFloat32())     args.add ((double) a.getFloat32());
        else if (a.isInt32())  args.add (a.getInt32());
        else if (a.isString()) args.add (a.getString());
    }

    const juce::ScopedLock sl (lock);
    receiveOsc (message.getAddressPattern().toString(), args);
}

void EmbeddedGuiServer::receiveOsc (const juce::String& address, const juce::Array<juce::var>& args)
{
    // o-s-c: OscServer -> Callbacks.receiveOsc. One argument is sent bare.
    if (! moduleInFilter (address, args))
        return;

    auto data = obj();
    data.getDynamicObject()->setProperty ("address", address);
    data.getDynamicObject()->setProperty ("args", args.size() == 1 ? args[0] : juce::var (args));
    data.getDynamicObject()->setProperty ("host", defaultTarget.upToLastOccurrenceOf (":", false, false));
    data.getDynamicObject()->setProperty ("port", defaultTarget.fromLastOccurrenceOf (":", false, false).getIntValue());
    deliver (data);
}

//==============================================================================
// formuls-module.js, ported. See src/gui/formuls-module.js for the design.
void EmbeddedGuiServer::modulePatch (const juce::var& targets, const juce::Array<juce::var>& args,
                                     const juce::String& onlyTo, const juce::String& except)
{
    auto* list = targets.getArray();
    if (list == nullptr)
        return;

    for (auto& target : *list)
    {
        // juce::var's array operator[] is unchecked, and a target has a 4th
        // element (steps) only when its fader is stepped: JS reads undefined.
        auto* t = target.getArray();
        if (t == nullptr || t->size() < 3)
            continue;
        auto at = [t] (int i) { return i < t->size() ? t->getReference (i) : juce::var(); };

        const auto cid = at (0), slot = at (1), count = at (2);
        juce::Array<juce::var> out;

        if (slot.isString())   // a widget variable, e.g. sequencer dimming
        {
            if (args.isEmpty()) continue;
            out = { "@var", cid, slot, count, args[0] };
        }
        else
        {
            out.add (cid);
            const int steps = (int) at (3);
            for (int k = 0; k < (int) count && k < args.size(); ++k)
            {
                juce::var v = args[k];
                if (steps > 0)
                    v = quantise (juce::jlimit (0.0, 1.0, (double) v), steps);
                out.add ((int) slot + k);
                out.add (v);
            }
            if (out.size() <= 1) continue;
        }

        auto data = obj();
        data.getDynamicObject()->setProperty ("address", "/fc_patch");
        data.getDynamicObject()->setProperty ("args", out);
        deliver (data, onlyTo, except);
    }
}

bool EmbeddedGuiServer::moduleInFilter (const juce::String& address, const juce::Array<juce::var>& args)
{
    if (! compounds.isObject())
        return true;

    const auto targets = compounds["inbound"][juce::Identifier (address)];
    if (targets.isVoid())
        return true;

    modulePatch (targets, args, {});

    if (auto* pass = compounds["passthrough"].getArray())
        return pass->contains (address);
    return false;
}

bool EmbeddedGuiServer::moduleOutFilter (const juce::String& address, const juce::Array<juce::var>& args,
                                         const juce::String& clientId)
{
    if (! compounds.isObject() || address.isEmpty())
        return true;

    const auto canvas = compounds["canvases"][juce::Identifier (address)];

    if (canvas.isVoid())
    {
        // A gesture (or a widget sharing the address): mirror it to the other tablets.
        const auto targets = compounds["inbound"][juce::Identifier (address)];
        if (! targets.isVoid())
            modulePatch (targets, args, {}, clientId);
        return true;
    }

    // A canvas's own address is only sent by a state recall: unpack it into
    // the per-address messages the stacked widgets sent, as floats.
    if (auto* recall = canvas["recall"].getArray())
        for (auto& r : *recall)
        {
            if (! r.isArray() || r.size() < 2)
                continue;
            const int slot = (int) r[0];
            if (slot >= 0 && slot < args.size())
                sendToPatch (defaultTarget, r[1].toString(), { (double) args[slot] }, "f");
        }

    return false;
}

} // namespace formuls
