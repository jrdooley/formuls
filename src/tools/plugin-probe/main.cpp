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
static HWND findPluginWindow (HWND top)
{
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
    return search.deepest;
}

static juce::Image captureWindow (HWND hwnd)
{
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
        return head.startsWith ("HTTP/1.1 101");
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
    TinyWs ws; bool wsOk = false;
    const int blocks = (int) (seconds * rate / block);
    for (int i = 0; i < blocks; ++i)
    {
        if (guiPort > 0 && i == 40)
            std::cout << "gui websocket on " << guiPort << ": " << ((wsOk = ws.connect (guiPort)) ? "connected" : "FAILED") << "\n";
        if (wsOk && guiAt.empty() && i > 40 && i % 47 == 0)
        {
            ws.sendText (R"(["sendOsc",{"address":"/testtone","v":1,"target":["127.0.0.1:9000"]}])");
            ++pings;
        }

        const double now = i * (double) block / rate;
        for (auto& g : guiAt)
            if (! g.sent && now >= g.t && wsOk)
            {
                ws.sendText ("[\"sendOsc\",{\"address\":\"" + g.a + "\",\"v\":" + juce::String (g.v) + ",\"target\":[\"127.0.0.1:9000\"]}]");
                g.sent = true;
                std::cout << "t=" << now << " gui " << g.a << " " << g.v << "\n";
            }
        midi.clear();
        for (auto& n : notesAt)
            if (! n.sent && now >= n.t)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, (int) n.v, (juce::uint8) 100), 0);
                n.sent = true;
            }

        buffer.clear();
        plugin->processBlock (buffer, midi);
        const float p = juce::jmax (buffer.getMagnitude (0, 0, block), buffer.getMagnitude (1, 0, block));
        peak = juce::jmax (peak, p);
        for (int k = 0; k + 1 < segments.size(); ++k)
            if (now >= segments[k] && now < segments[k + 1]) segPeak[(size_t) k] = juce::jmax (segPeak[(size_t) k], p);
        if (p > 1.0e-6f) ++nonSilentBlocks;
        if (realtime)   // pace like a real audio device, running the message loop meanwhile
            juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (1000.0 * block / rate));
        else if (i % 16 == 0)   // let the plugin's timers run, as a host's message loop would
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    for (int k = 0; k + 1 < segments.size(); ++k)
        std::cout << "peak " << segments[k] << "-" << segments[k + 1] << " s: " << segPeak[(size_t) k] << "\n";
    std::cout << "rendered " << blocks << " blocks, peak " << peak
              << ", non-silent blocks " << nonSilentBlocks << ", testtone pings sent " << pings << "\n";

    // PROBE_MIN_PEAK=x: fail (exit 3) when the output never reaches x, for CI.
    if (auto minPeak = juce::SystemStats::getEnvironmentVariable ("PROBE_MIN_PEAK", {}); minPeak.isNotEmpty()
        && peak < minPeak.getFloatValue())
    {
        std::cerr << "peak " << peak << " is below PROBE_MIN_PEAK " << minPeak << "\n";
        return 3;
    }

    // The editor: created and sized by default. PROBE_SHOW_EDITOR=1 also puts it
    // in a desktop window for PROBE_SHOW_SECS (default 2) with the message loop
    // running, as a DAW does, and checks it is on screen (on Windows, that the
    // plugin made its own child window); PROBE_SNAPSHOT=<png> saves a capture
    // of the window (Windows only). PROBE_REQUIRE_EDITOR=1: exit 4 if any of that fails.
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

               #if JUCE_WINDOWS
                // The real test: the plugin made its own window inside the host's.
                HWND pluginWindow = onScreen ? findPluginWindow ((HWND) window.getPeer()->getNativeHandle()) : nullptr;
                std::cout << "plugin window " << (pluginWindow != nullptr ? "found" : "NOT found") << "\n";
                onScreen = onScreen && pluginWindow != nullptr;

                if (auto png = env ("PROBE_SNAPSHOT"); png.isNotEmpty() && pluginWindow != nullptr)
                {
                    auto image = captureWindow ((HWND) window.getPeer()->getNativeHandle());
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
