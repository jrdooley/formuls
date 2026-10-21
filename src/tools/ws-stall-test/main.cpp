/*
 * ws-stall-test: does WebSocketServer block the thread that sends to a tablet
 * which has stopped reading (poor Wi-Fi, a frozen browser tab)?
 *
 * Starts the server, connects one raw client that completes the handshake
 * and then never reads, and sends it a reset-sized burst (9,000 patch
 * messages, as EmbeddedGuiServer::deliver does at start-up or a global
 * reset) from this thread, which stands in for the host's message thread.
 * A second, well-behaved client must receive every message in order.
 *
 * Exit 0: every sendText returned within maxSendMs; one reset fits in the
 * stalled client's queue; the good client got everything in order; and the
 * stalled client was eventually disconnected rather than kept forever. Non-zero otherwise. See src/tools/README.md.
 */

#include <JuceHeader.h>
#include "WebSocketServer.h"
#include <iostream>
#if ! JUCE_WINDOWS
 #include <sys/socket.h>
#endif

using formuls::WebSocketServer;

namespace
{
    // One reset is ~9,000 messages. Forty, 50 ms apart, outgrow even
    // loopback's auto-tuned socket buffers (several MB on macOS) plus the
    // server's queue, as a slow Wi-Fi link's much smaller window would sooner.
    // WS_STALL_RESETS=1 instead checks the stall timeout: one reset fits in
    // the queue, so the stalled client is dropped only after stallTimeoutMs.
    constexpr int perReset = 9000;
    const int numResets = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("WS_STALL_RESETS", "40").getIntValue());
    const int numMessages = perReset * numResets;
    constexpr double maxSendMs = 50.0;

    std::unique_ptr<juce::StreamingSocket> handshake (int port, const juce::String& path)
    {
        auto s = std::make_unique<juce::StreamingSocket>();
        if (! s->connect ("127.0.0.1", port, 2000))
            return nullptr;

        juce::String req;
        req << "GET " << path << " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
            << "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
        s->write (req.toRawUTF8(), (int) req.getNumBytesAsUTF8());

        juce::String head;   // read up to the blank line, byte by byte
        char c;
        while (! head.endsWith ("\r\n\r\n") && s->read (&c, 1, true) == 1)
            head += c;
        return head.startsWith ("HTTP/1.1 101") ? std::move (s) : nullptr;
    }

    // Reads frames until `count` text frames have arrived; returns them.
    juce::StringArray readFrames (juce::StreamingSocket& s, int count)
    {
        juce::StringArray out;
        auto readExact = [&s] (void* d, int n) { return n == 0 || s.read (d, n, true) == n; };

        while (out.size() < count)
        {
            juce::uint8 h[2];
            if (! readExact (h, 2)) break;
            juce::uint64 len = h[1] & 0x7f;
            if (len == 126) { juce::uint8 e[2]; if (! readExact (e, 2)) break; len = (juce::uint64) e[0] << 8 | e[1]; }
            else if (len == 127) { juce::uint8 e[8]; if (! readExact (e, 8)) break; len = 0; for (auto b : e) len = len << 8 | b; }
            juce::MemoryBlock p ((size_t) len);
            if (! readExact (p.getData(), (int) len)) break;
            if ((h[0] & 0x0f) == 0x1)
                out.add (juce::String::fromUTF8 ((const char*) p.getData(), (int) len));
        }
        return out;
    }
}

int main()
{
    WebSocketServer server;
    std::mutex m;
    std::vector<WebSocketServer::ConnectionPtr> conns;
    std::atomic<int> closed { 0 };
    server.onOpen = [&] (auto c) { std::lock_guard<std::mutex> l (m); conns.push_back (c); };
    server.onClose = [&] (auto) { ++closed; };

    int port = 18080;
    while (! server.start (port)) ++port;

    auto stalled = handshake (port, "/stalled/x");
    auto good = handshake (port, "/good/x");
    if (stalled == nullptr || good == nullptr) { std::cerr << "handshake failed on port " << port << "\n"; return 2; }
    // A tiny receive buffer on the stalled side, so the kernel fills quickly.
    int small = 4096;
    setsockopt (stalled->getRawSocketHandle(), SOL_SOCKET, SO_RCVBUF, &small, sizeof (small));

    while (true) { { std::lock_guard<std::mutex> l (m); if (conns.size() == 2) break; } juce::Thread::sleep (10); }

    juce::StringArray received;
    std::thread reader ([&] { received = readFrames (*good, numMessages); });

    // A realistic receiveOsc frame, ~150 bytes.
    const juce::String pad ("[\"receiveOsc\",{\"address\":\"/op1/ratio\",\"args\":0.25,\"host\":\"127.0.0.1\",\"port\":9000,\"seq\":");
    double worst = 0, total = 0;
    int firstFail = -1;
    bool afterFirstReset = false;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < numMessages; ++i)
    {
        const auto text = pad + juce::String (i) + "}]";
        const auto start = juce::Time::getMillisecondCounterHiRes();
        { std::lock_guard<std::mutex> l (m); for (auto& c : conns) c->sendText (text); }
        if (firstFail < 0) { std::lock_guard<std::mutex> l (m); for (auto& c : conns) if (! c->isOpen()) firstFail = i; }
        const auto took = juce::Time::getMillisecondCounterHiRes() - start;
        worst = juce::jmax (worst, took);
        if ((i + 1) % perReset == 0)
        {
            if (i + 1 == perReset)
                for (auto& c : conns) if (c->path == "/stalled/x") afterFirstReset = c->isOpen();
            juce::Thread::sleep (50);
        }
        total = juce::Time::getMillisecondCounterHiRes() - t0;
    }

    reader.join();

    bool inOrder = received.size() == numMessages;
    for (int i = 0; inOrder && i < numMessages; ++i)
        inOrder = received[i].endsWith (":" + juce::String (i) + "}]");

    // The stalled client should be disconnected eventually, not held forever.
    for (int i = 0; i < 2000 && closed.load() == 0; ++i) juce::Thread::sleep (10);

    std::cout << "burst " << numMessages << " msgs: total " << juce::String (total, 1) << " ms, worst sendText "
              << juce::String (worst, 1) << " ms\n"
              << "good client received " << received.size() << (inOrder ? " in order" : " OUT OF ORDER / INCOMPLETE") << "\n"
              << "stalled client " << (afterFirstReset ? "still queued" : "already dropped") << " after one reset\n"
              << "first connection lost after message " << firstFail << "\n"
              << "stalled client " << (closed.load() > 0 ? "disconnected" : "still connected") << "\n";

    const bool dropped = closed.load() > 0;
    stalled.reset();
    good.reset();
    server.stop();

    if (worst > maxSendMs) { std::cerr << "FAIL: a send blocked the caller\n"; return 1; }
    if (! afterFirstReset)  { std::cerr << "FAIL: one reset dropped a slow client\n"; return 1; }
    if (! inOrder)          { std::cerr << "FAIL: good client lost messages\n"; return 1; }
    if (! dropped)          { std::cerr << "FAIL: stalled client never dropped\n"; return 1; }
    std::cout << "PASS\n";
    return 0;
}
