// Loads a plugin like a DAW would, renders a few seconds, reports peak level.
// See src/tools/README.md (plugin-probe). Built with CMake against JUCE at ~/JUCE.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

// A hosted VST3 editor draws into the plugin's own child window, not into the
// JUCE component that holds it, so look for that window and capture it the
// way a screenshot would.
static bool findPluginWindow (void* topHandle)
{
    auto top = (HWND) topHandle;
    // Lists every visible child window, and returns the deepest: the host's
    // embedding window comes first, the plugin's own window inside it.
    struct Search { HWND top, deepest = nullptr; int depth = 0; } search { top };
    EnumChildWindows (top, [] (HWND child, LPARAM param) -> BOOL
    {
        auto& s = *(Search*) param;
        RECT r; GetClientRect (child, &r);
        if (! IsWindowVisible (child) || r.right <= 0 || r.bottom <= 0) return TRUE;
        int depth = 0;
        for (HWND w = child; w != nullptr && w != s.top; w = GetParent (w)) ++depth;
        char name[128] {}; GetClassNameA (child, name, (int) sizeof (name));
        std::cout << "  child window depth " << depth << " class " << name
                  << " " << r.right << "x" << r.bottom << "\n";
        if (depth > s.depth) { s.depth = depth; s.deepest = child; }
        return TRUE;
    }, (LPARAM) &search);
    return search.deepest != nullptr;
}

static juce::Image captureWindow (void* handle)
{
    auto hwnd = (HWND) handle;
    RECT r; GetClientRect (hwnd, &r);
    const int w = r.right, h = r.bottom;
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    HDC screen = GetDC (nullptr), dc = CreateCompatibleDC (screen);
    HBITMAP bitmap = CreateCompatibleBitmap (screen, w, h);
    auto old = SelectObject (dc, bitmap);
    PrintWindow (hwnd, dc, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */);
    BITMAPINFO info {};
    info.bmiHeader = { sizeof (BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    std::vector<juce::uint32> pixels ((size_t) (w * h));
    GetDIBits (dc, bitmap, 0, (UINT) h, pixels.data(), &info, DIB_RGB_COLORS);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixelAt (x, y, juce::Colour (pixels[(size_t) (y * w + x)] | 0xff000000));
    SelectObject (dc, old); DeleteObject (bitmap); DeleteDC (dc); ReleaseDC (nullptr, screen);
    return image;
}
#elif JUCE_LINUX
 #include <X11/Xlib.h>
 #include <X11/Xutil.h>

// On Linux the hosted editor is an X11 window of the plugin's, embedded in the
// host's. The probe opens its own X connection to look at the window tree.
// "mapped" is the window's own state: the plugin mapped it. "viewable" also
// needs every window above it mapped, which is the host's job. JUCE's LV2 host
// left its holder unmapped in an emulated x86 VM (not on CI), so a mapped
// plugin window counts.
struct WindowSearch { int deepestMapped = 0; bool deepestViewable = false; };

static void listChildren (Display* d, Window w, int depth, WindowSearch& search)
{
    Window root, parent, *children = nullptr; unsigned int n = 0;
    if (! XQueryTree (d, w, &root, &parent, &children, &n)) return;
    for (unsigned int i = 0; i < n; ++i)
    {
        XWindowAttributes a;
        if (! XGetWindowAttributes (d, children[i], &a) || a.width <= 1 || a.height <= 1)
            continue;
        XClassHint hint {};
        XGetClassHint (d, children[i], &hint);
        const char* state = a.map_state == IsViewable ? "viewable" : a.map_state == IsUnviewable ? "mapped" : "unmapped";
        std::cout << "  child window depth " << depth << " class " << (hint.res_class != nullptr ? hint.res_class : "-")
                  << " " << a.width << "x" << a.height << " " << state << "\n";
        if (hint.res_name != nullptr) XFree (hint.res_name);
        if (hint.res_class != nullptr) XFree (hint.res_class);
        if (a.map_state != IsUnmapped && depth > search.deepestMapped)
        {
            search.deepestMapped = depth;
            search.deepestViewable = a.map_state == IsViewable;
        }
        listChildren (d, children[i], depth + 1, search);
    }
    if (children != nullptr) XFree (children);
}

// JUCE 9 looks up WM_PROTOCOLS and the other window manager atoms only if they
// already exist, then sets WM_PROTOCOLS on every window it creates without
// checking: on a display with no window manager (a bare Xvfb in CI) that is
// atom 0 and a fatal BadAtom. A window manager would have created them, so do
// that before JUCE opens its display. Not needed on a real desktop.
static void createWindowManagerAtoms()
{
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr) return;
    for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING",
                        "WM_CHANGE_STATE", "WM_STATE", "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE" })
        XInternAtom (d, name, False);
    XSync (d, False);
    // Left open: an X server forgets its atoms when its last client goes.
}

static bool pluginWindowViewable = true;

static bool findPluginWindow (void* topHandle)
{
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr) return false;
    WindowSearch search;
    listChildren (d, (Window) (juce::pointer_sized_uint) topHandle, 1, search);
    XCloseDisplay (d);
    // Depth 1 is the host's holder for the hosted editor; the plugin's own
    // window sits inside it.
    pluginWindowViewable = search.deepestViewable;
    if (search.deepestMapped >= 2 && ! search.deepestViewable)
        std::cout << "plugin window mapped, but the host has not mapped its holder window\n";
    return search.deepestMapped >= 2;
}

static juce::Image captureWindow (void* handle)
{
    juce::Image image;
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr) return image;
    auto w = (Window) (juce::pointer_sized_uint) handle;
    XWindowAttributes a;
    // Xvfb has no compositor, so the window's pixels include its children.
    if (XGetWindowAttributes (d, w, &a))
        if (XImage* x = XGetImage (d, w, 0, 0, (unsigned) a.width, (unsigned) a.height, AllPlanes, ZPixmap))
        {
            image = juce::Image (juce::Image::ARGB, a.width, a.height, true, juce::SoftwareImageType());
            for (int y = 0; y < a.height; ++y)
                for (int xx = 0; xx < a.width; ++xx)
                    image.setPixelAt (xx, y, juce::Colour ((juce::uint32) XGetPixel (x, xx, y) | 0xff000000));
            XDestroyImage (x);
        }
    XCloseDisplay (d);
    return image;
}
#endif

// A bare WebSocket client: enough to act as a tablet and send one message.
struct TinyWs
{
    juce::StreamingSocket socket;
    bool connect (int port)
    {
        if (! socket.connect ("127.0.0.1", port, 2000)) return false;
        juce::String req = "GET /probe/ HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        socket.write (req.toRawUTF8(), (int) req.getNumBytesAsUTF8());
        juce::String head; char c;
        while (! head.endsWith ("\r\n\r\n") && socket.read (&c, 1, true) == 1) head += c;
        if (! head.startsWith ("HTTP/1.1 101")) return false;
        startReading();
        return true;
    }
    // Text frames the server has sent so far (unmasked, as a server sends),
    // without blocking. A thread of its own drains the socket: the plugin
    // sends from its message thread, which in this probe is the main thread,
    // and blocks while the socket buffer is full. Read only from the main
    // thread, a burst (the start-up reset) deadlocks the two until the
    // plugin gives up and closes the connection. A real tablet is another
    // device and keeps reading.
    juce::MemoryBlock pending;
    juce::CriticalSection pendingLock;
    struct Reader : juce::Thread
    {
        TinyWs& ws;
        explicit Reader (TinyWs& w) : juce::Thread ("probe ws reader"), ws (w) {}
        void run() override
        {
            char chunk[65536];
            while (! threadShouldExit())
            {
                const int ready = ws.socket.waitUntilReady (true, 50);
                if (ready < 0) break;
                if (ready == 0) continue;
                const int n = ws.socket.read (chunk, sizeof (chunk), false);
                if (n < 0) break;
                if (n == 0) { if (! ws.socket.isConnected()) break; continue; }
                const juce::ScopedLock sl (ws.pendingLock);
                ws.pending.append (chunk, (size_t) n);
            }
        }
    };
    std::unique_ptr<Reader> reader;
    // Drops the link, as a tablet does in a Wi-Fi dropout; connect() again comes
    // back with the same client id, as the o-s-c client does.
    void disconnect()
    {
        if (reader != nullptr) { reader->stopThread (1000); reader.reset(); }
        socket.close();
        const juce::ScopedLock sl (pendingLock);
        pending.reset();
    }
    void startReading() { if (reader == nullptr) { reader = std::make_unique<Reader> (*this); reader->startThread(); } }
    ~TinyWs() { if (reader != nullptr) reader->stopThread (1000); }

    juce::StringArray poll()
    {
        juce::StringArray texts;
        startReading();
        const juce::ScopedLock sl (pendingLock);
        size_t used = 0;
        for (;;)
        {
            auto* b = (const juce::uint8*) pending.getData() + used;
            const size_t have = pending.getSize() - used;
            if (have < 2) break;
            size_t len = b[1] & 0x7f, head = 2;
            if (len == 126) { if (have < 4) break; len = ((size_t) b[2] << 8) | b[3]; head = 4; }
            else if (len == 127) { if (have < 10) break; len = 0; for (int k = 2; k < 10; ++k) len = (len << 8) | b[k]; head = 10; }
            if (have < head + len) break;
            if ((b[0] & 0x0f) == 1)
                texts.add (juce::String::fromUTF8 ((const char*) b + head, (int) len));
            used += head + len;
        }
        pending.removeSection (0, used);
        return texts;
    }

    void sendText (const juce::String& text)
    {
        const auto* utf8 = text.toRawUTF8(); const size_t n = std::strlen (utf8);
        std::vector<juce::uint8> f { 0x81 };
        if (n < 126) f.push_back ((juce::uint8) (0x80 | n));
        else { f.push_back (0x80 | 126); f.push_back ((juce::uint8) (n >> 8)); f.push_back ((juce::uint8) n); }
        const juce::uint8 mask[4] = { 1, 2, 3, 4 };
        f.insert (f.end(), mask, mask + 4);
        for (size_t i = 0; i < n; ++i) f.push_back ((juce::uint8) (utf8[i] ^ mask[i % 4]));
        socket.write (f.data(), (int) f.size());
    }
};

static int runOnce (int argc, char** argv);
int main (int argc, char** argv)
{
   #if JUCE_LINUX
    createWindowManagerAtoms();
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::cerr << "usage: plugin_probe <plugin path> [seconds]\n"; return 2; }
    // PROBE_CYCLES=n: load, play and unload n times in one process, as a host
    // that closes and reopens a project does. PROBE_PATH2: use this bundle from
    // the second cycle on (a reinstalled copy).
    const int cycles = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("PROBE_CYCLES", "1").getIntValue());
    const auto path2 = juce::SystemStats::getEnvironmentVariable ("PROBE_PATH2", {});
    int result = 0;
    for (int c = 0; c < cycles && result == 0; ++c)
    {
        std::cout << "=== cycle " << (c + 1) << "\n";
        if (c > 0 && path2.isNotEmpty()) argv[1] = const_cast<char*> (path2.toRawUTF8());
        result = runOnce (argc, argv);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    }
    return result;
}
static int runOnce (int argc, char** argv)
{
    const double seconds = argc > 2 ? std::atof (argv[2]) : 2.0;
    const int guiPort = argc > 3 ? std::atoi (argv[3]) : 0;   // send /testtone pings through the GUI socket

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : formats.getFormats())
        f->findAllTypesForFile (found, argv[1]);
    if (found.isEmpty()) { std::cerr << "no plugin found in " << argv[1] << "\n"; return 1; }

    const double rate = 48000;
    const int block = juce::SystemStats::getEnvironmentVariable ("PROBE_BLOCK", "512").getIntValue();
    juce::String error;

    // PROBE_IDLE_FIRST=1: like a host that creates an instance, prepares it and
    // keeps it alive without ever processing it, then plays a second one.
    std::unique_ptr<juce::AudioPluginInstance> idleFirst;
    if (juce::SystemStats::getEnvironmentVariable ("PROBE_IDLE_FIRST", {}) == "1")
    {
        idleFirst = formats.createPluginInstance (*found[0], rate, block, error);
        if (idleFirst != nullptr)
        {
            idleFirst->setPlayConfigDetails (0, 2, rate, block);
            idleFirst->prepareToPlay (rate, block);
            std::cout << "created an idle first instance (prepared, never processed)\n";
        }
    }

    auto plugin = formats.createPluginInstance (*found[0], rate, block, error);
    if (plugin == nullptr) { std::cerr << "load failed: " << error << "\n"; return 1; }
    std::cout << "loaded " << found[0]->pluginFormatName << " " << found[0]->name
              << " (" << plugin->getTotalNumOutputChannels() << " out)\n";

    // PROBE_LOAD=<file>: restore a saved state first, as a DAW opening a project does.
    if (auto load = juce::SystemStats::getEnvironmentVariable ("PROBE_LOAD", {}); load.isNotEmpty())
    {
        juce::MemoryBlock saved;
        juce::File (load).loadFileAsData (saved);
        plugin->setStateInformation (saved.getData(), (int) saved.getSize());
        std::cout << "restored state from " << load << " (" << saved.getSize() << " bytes)\n";
    }
    const bool realtime = juce::SystemStats::getEnvironmentVariable ("PROBE_REALTIME", {}) == "1";

    plugin->setPlayConfigDetails (0, 2, rate, block);
    plugin->prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    float peak = 0; int nonSilentBlocks = 0, pings = 0;
    // PROBE_GUI_AT="secs:address:value,...": tablet messages (sendOsc) at those times.
    // PROBE_NOTES_AT="secs:note,...": MIDI note-ons (velocity 100, channel 1).
    // PROBE_SEGMENTS="a,b,...": report the peak between each pair of times.
    struct Timed { double t; juce::String a; float v; bool sent = false; };
    std::vector<Timed> guiAt, notesAt;
    for (auto item : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PROBE_GUI_AT", {}), ",", {}))
    { auto p = juce::StringArray::fromTokens (item, ":", {}); if (p.size() == 3) guiAt.push_back ({ p[0].getDoubleValue(), p[1], p[2].getFloatValue() }); }
    for (auto item : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PROBE_NOTES_AT", {}), ",", {}))
    { auto p = juce::StringArray::fromTokens (item, ":", {}); if (p.size() == 2) notesAt.push_back ({ p[0].getDoubleValue(), {}, p[1].getFloatValue() }); }
    juce::Array<double> segments;
    for (auto t : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PROBE_SEGMENTS", {}), ",", {})) segments.add (t.getDoubleValue());
    std::vector<float> segPeak ((size_t) juce::jmax (0, segments.size() - 1), 0.0f);
    // PROBE_WATCH="/addr,/addr2": print each message the plugin sends the
    // tablet to one of these addresses (* for all), with its time.
    // PROBE_EXPECT_GUI="/addr:from:to,...": exit 5 unless such a message arrives
    // between those times; PROBE_FORBID_GUI, same form: exit 5 if one does.
    auto watch = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PROBE_WATCH", {}), ",", {});
    struct Window { juce::String a; double from, to; int count = 0; };
    auto parseWindows = [] (const char* name)
    {
        std::vector<Window> w;
        for (auto item : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable (name, {}), ",", {}))
        { auto p = juce::StringArray::fromTokens (item, ":", {}); if (p.size() == 3) w.push_back ({ p[0], p[1].getDoubleValue(), p[2].getDoubleValue() }); }
        return w;
    };
    auto expectGui = parseWindows ("PROBE_EXPECT_GUI"), forbidGui = parseWindows ("PROBE_FORBID_GUI");
    for (auto& w : expectGui) watch.addIfNotAlreadyThere (w.a);
    for (auto& w : forbidGui) watch.addIfNotAlreadyThere (w.a);
    // PROBE_READY_ON="/addr": the times above count from the first message the
    // plugin sends to that address (the patch's start-up reset, on a machine of
    // any speed), and the run ends once the last of them has passed.
    const auto readyOn = juce::SystemStats::getEnvironmentVariable ("PROBE_READY_ON", {});
    if (readyOn.isNotEmpty()) watch.addIfNotAlreadyThere (readyOn);
    double t0 = readyOn.isEmpty() ? 0.0 : -1.0, lastTime = 0;
    for (auto& g : guiAt) lastTime = juce::jmax (lastTime, g.t);
    for (auto& n : notesAt) lastTime = juce::jmax (lastTime, n.t);
    for (auto* list : { &expectGui, &forbidGui }) for (auto& w : *list) lastTime = juce::jmax (lastTime, w.to);
    // PROBE_LINGER=s: with PROBE_READY_ON, keep going this long after the last
    // time (default 0.1 s), to see messages that arrive late.
    const double linger = juce::SystemStats::getEnvironmentVariable ("PROBE_LINGER", "0.1").getDoubleValue();
    int received = 0; double lastReceived = -1;
    TinyWs ws; bool wsOk = false;
    // PROBE_AWAY="a:b": the tablet drops its link at a and reconnects at b
    // (counted like PROBE_GUI_AT), to see what the server resyncs it with.
    const auto away = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PROBE_AWAY", {}), ":", {});
    double awayFrom = away.size() == 2 ? away[0].getDoubleValue() : -1, awayTo = away.size() == 2 ? away[1].getDoubleValue() : -1;
    bool isAway = false;
    const int blocks = (int) (seconds * rate / block);
    int renderedBlocks = 0;
    for (int i = 0; i < blocks; ++i)
    {
        ++renderedBlocks;
        if (guiPort > 0 && i == 40)
            std::cout << "gui websocket on " << guiPort << ": " << ((wsOk = ws.connect (guiPort)) ? "connected" : "FAILED") << "\n";
        if (wsOk && guiAt.empty() && i > 40 && i % 47 == 0)
        {
            ws.sendText (R"(["sendOsc",{"address":"/testtone","v":1,"target":["127.0.0.1:9000"]}])");
            ++pings;
        }

        const double now = i * (double) block / rate;
        if (wsOk && ! watch.isEmpty())
            for (auto& text : ws.poll())
            {
                // ["receiveOsc", {"address": "/x", "args": ...}]
                const auto data = juce::JSON::parse (text);
                if (! data.isArray() || data.size() < 2 || data[0].toString() != "receiveOsc") continue;
                ++received; lastReceived = now;
                const auto address = data[1]["address"].toString();
                if (! watch.contains (address) && ! watch.contains ("*")) continue;
                const double rel = now - juce::jmax (0.0, t0);
                if (address == readyOn && t0 < 0)
                {
                    t0 = now;
                    std::cout << "t=" << now << " ready (" << readyOn << "); times count from here\n";
                }
                std::cout << "t=" << now << " from plugin " << address << " " << juce::JSON::toString (data[1]["args"], true).substring (0, 80) << "\n";
                for (auto* list : { &expectGui, &forbidGui })
                    for (auto& w : *list)
                        if (w.a == address && t0 >= 0 && rel >= w.from && rel < w.to) ++w.count;
            }

        if (awayFrom >= 0 && t0 >= 0 && now - t0 >= awayFrom && wsOk)
        {
            awayFrom = -1;
            ws.disconnect();
            wsOk = false; isAway = true;
            std::cout << "t=" << now << " tablet away\n";
        }
        if (isAway && now - t0 >= awayTo)
        {
            isAway = false;
            std::cout << "t=" << now << " tablet back: " << ((wsOk = ws.connect (guiPort)) ? "reconnected" : "FAILED") << "\n";
        }
        for (auto& g : guiAt)
            if (! g.sent && t0 >= 0 && now - t0 >= g.t && wsOk)
            {
                ws.sendText ("[\"sendOsc\",{\"address\":\"" + g.a + "\",\"v\":" + juce::String (g.v) + ",\"target\":[\"127.0.0.1:9000\"]}]");
                g.sent = true;
                std::cout << "t=" << now << " gui " << g.a << " " << g.v << "\n";
            }
        midi.clear();
        for (auto& n : notesAt)
            if (! n.sent && t0 >= 0 && now - t0 >= n.t)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, (int) n.v, (juce::uint8) 100), 0);
                n.sent = true;
                std::cout << "t=" << now << " note-on " << (int) n.v << "\n";
            }

        buffer.clear();
        plugin->processBlock (buffer, midi);
        const float p = juce::jmax (buffer.getMagnitude (0, 0, block), buffer.getMagnitude (1, 0, block));
        peak = juce::jmax (peak, p);
        for (int k = 0; k + 1 < segments.size(); ++k)
            if (now >= segments[k] && now < segments[k + 1]) segPeak[(size_t) k] = juce::jmax (segPeak[(size_t) k], p);
        if (p > 1.0e-6f) ++nonSilentBlocks;
        if (readyOn.isNotEmpty() && t0 >= 0 && now - t0 > lastTime + linger)
            break;
        if (realtime)   // pace like a real audio device, running the message loop meanwhile
            juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (1000.0 * block / rate));
        else if (i % 16 == 0)   // let the plugin's timers run, as a host's message loop would
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    for (int k = 0; k + 1 < segments.size(); ++k)
        std::cout << "peak " << segments[k] << "-" << segments[k + 1] << " s: " << segPeak[(size_t) k] << "\n";
    std::cout << "rendered " << renderedBlocks << " blocks, peak " << peak
              << ", non-silent blocks " << nonSilentBlocks << ", testtone pings sent " << pings << "\n";

    if (! watch.isEmpty())
        std::cout << "received " << received << " message(s) from the plugin, the last at t=" << lastReceived << "\n";
    if (readyOn.isNotEmpty() && t0 < 0)
    {
        std::cerr << "never saw " << readyOn << " from the plugin\n";
        return 5;
    }
    for (auto& w : expectGui)
    {
        std::cout << "expected " << w.a << " in " << w.from << "-" << w.to << " s: " << w.count << " message(s)\n";
        if (w.count == 0) return 5;
    }
    for (auto& w : forbidGui)
    {
        std::cout << "forbidden " << w.a << " in " << w.from << "-" << w.to << " s: " << w.count << " message(s)\n";
        if (w.count > 0) return 5;
    }

    // PROBE_MIN_PEAK=x: fail (exit 3) when the output never reaches x, for CI.
    if (auto minPeak = juce::SystemStats::getEnvironmentVariable ("PROBE_MIN_PEAK", {}); minPeak.isNotEmpty()
        && peak < minPeak.getFloatValue())
    {
        std::cerr << "peak " << peak << " is below PROBE_MIN_PEAK " << minPeak << "\n";
        return 3;
    }

    // The editor: created and sized by default. PROBE_SHOW_EDITOR=1 also puts it
    // in a desktop window for PROBE_SHOW_SECS (default 2) with the message loop
    // running, as a DAW does, and checks it is on screen (on Windows and
    // Linux, that the plugin made its own child window); PROBE_SNAPSHOT=<png>
    // saves a capture of the window (Windows and Linux). PROBE_REQUIRE_EDITOR=1: exit 4 if any of that fails.
    auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
    const bool requireEditor = env ("PROBE_REQUIRE_EDITOR") == "1";
    if (env ("PROBE_NO_EDITOR") != "1")
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
        if (editor == nullptr)
        {
            std::cerr << "no editor\n";
            if (requireEditor) return 4;
        }
        else
        {
            std::cout << "editor " << editor->getWidth() << "x" << editor->getHeight() << "\n";
            if (requireEditor && (editor->getWidth() <= 0 || editor->getHeight() <= 0)) return 4;

            if (env ("PROBE_SHOW_EDITOR") == "1")
            {
                juce::DocumentWindow window (plugin->getName(), juce::Colours::black,
                                             juce::DocumentWindow::closeButton);
                window.setUsingNativeTitleBar (true);
                window.setContentNonOwned (editor.get(), true);
                window.setVisible (true);
                const int secs = env ("PROBE_SHOW_SECS").isEmpty() ? 2 : env ("PROBE_SHOW_SECS").getIntValue();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1000 * secs);

                bool onScreen = window.getPeer() != nullptr && editor->isShowing();
                std::cout << "editor window " << (onScreen ? "showing" : "NOT showing") << " at "
                          << editor->getScreenBounds().toString() << "\n";

               #if JUCE_WINDOWS || JUCE_LINUX
                // The real test: the plugin made its own window inside the host's.
                auto* top = onScreen ? window.getPeer()->getNativeHandle() : nullptr;
                const bool pluginWindow = top != nullptr && findPluginWindow (top);
                std::cout << "plugin window " << (pluginWindow ? "found" : "NOT found") << "\n";
                onScreen = onScreen && pluginWindow;

                if (auto png = env ("PROBE_SNAPSHOT"); png.isNotEmpty() && pluginWindow
                   #if JUCE_LINUX
                    && pluginWindowViewable
                   #endif
                    )
                {
                    auto image = captureWindow (top);
                    juce::File file (png);
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (image, out);
                    std::cout << "snapshot " << image.getWidth() << "x" << image.getHeight() << " to " << png << "\n";
                }
               #endif

                window.clearContentComponent();
                window.setVisible (false);
                if (requireEditor && ! onScreen) return 4;
            }
        }
        editor.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    // PROBE_SAVE=<file>: what a DAW would store in the project.
    if (auto save = juce::SystemStats::getEnvironmentVariable ("PROBE_SAVE", {}); save.isNotEmpty())
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        juce::File (save).replaceWithData (state.getData(), state.getSize());
        std::cout << "saved state to " << save << " (" << state.getSize() << " bytes)\n";
    }

    plugin->releaseResources();
    if (idleFirst != nullptr) { idleFirst->releaseResources(); idleFirst.reset(); }
    plugin.reset();
    std::cout << "unloaded\n";
    return 0;
}
