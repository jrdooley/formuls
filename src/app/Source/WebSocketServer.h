/*
 * WebSocketServer.h
 *
 * A small HTTP + WebSocket server on juce::StreamingSocket, so the app can
 * serve the control GUI itself instead of running node + Open Stage Control.
 *
 * Deliberately minimal: one thread accepts, one thread per connection reads.
 * Plain HTTP requests are answered by a callback and the connection closed
 * (no keep-alive). An "Upgrade: websocket" request is handshaken (RFC 6455)
 * and becomes a Connection that delivers text messages to the callbacks and
 * can be sent text from any thread.
 *
 * Sending never blocks the caller (in the plugin that is the host's message
 * thread). Each Connection queues outgoing frames and a writer thread of its
 * own drains them in order. A client that falls too far behind (more than
 * maxQueuedBytes waiting) or accepts nothing for stallTimeoutMs is
 * disconnected rather than sent a gapped stream; the o-s-c client reconnects
 * on its own, and EmbeddedGuiServer resyncs it.
 *
 * Not supported, because the GUI client never needs them: TLS, permessage-
 * deflate, binary messages, fragmented messages from the server side.
 *
 * Prototype status (ws-prototype branch): see docs/embedded-gui/README.md.
 */

#pragma once

#include <JuceHeader.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace formuls
{

class WebSocketServer : private juce::Thread
{
public:
    struct HttpRequest
    {
        juce::String method, path;          // path includes any ?query
        juce::StringPairArray headers;      // keys lower-cased
        juce::String remoteAddress;
    };

    struct HttpResponse
    {
        int status = 404;
        juce::String contentType = "text/plain";
        juce::MemoryBlock body;
        juce::StringPairArray extraHeaders;
    };

    /** One upgraded WebSocket. Shared so a callback can keep it past close. */
    class Connection : public std::enable_shared_from_this<Connection>
    {
    public:
        /** The request path the client upgraded on, e.g. "/<clientId>/<auth>". */
        const juce::String path;
        const juce::String remoteAddress;

        /** A reset of the patch sends ~9,000 messages, ~1.3 MB: this is room
            for several back to back on a slow link. */
        static constexpr size_t maxQueuedBytes = 8u << 20;
        /** Longer than any Wi-Fi hiccup a tablet recovers from by itself. */
        static constexpr int stallTimeoutMs = 10000;

        /** Queues one text frame and returns at once. Thread-safe, and frames
            go out in the order queued. Returns false once the connection is
            gone, including when this frame would overflow the queue (the
            connection is then closed). */
        bool sendText (const juce::String& text);
        bool sendText (const char* utf8, size_t numBytes);

        void close();
        bool isOpen() const noexcept   { return open.load(); }

        ~Connection();

    private:
        friend class WebSocketServer;
        Connection (std::unique_ptr<juce::StreamingSocket> s, juce::String p, juce::String addr);

        bool sendFrame (int opcode, const void* data, size_t numBytes);
        void writerLoop();
        bool writeQueued (const juce::MemoryBlock& frame);
        void fail();   // stop sending and wake the reader, which then closes

        std::unique_ptr<juce::StreamingSocket> socket;
        std::atomic<bool> open { true };

        std::mutex queueLock;
        std::condition_variable queueChanged;
        std::deque<juce::MemoryBlock> queue;
        size_t queuedBytes = 0;   // includes the frame being written

        std::mutex closeLock;
        bool closed = false;
        std::thread writer;
    };

    using ConnectionPtr = std::shared_ptr<Connection>;

    // All callbacks run on the connection's own reader thread.
    std::function<HttpResponse (const HttpRequest&)> onHttpRequest;
    std::function<void (ConnectionPtr)> onOpen;
    std::function<void (ConnectionPtr, const juce::String&)> onMessage;
    std::function<void (ConnectionPtr)> onClose;

    WebSocketServer() : juce::Thread ("formuls GUI http") {}
    ~WebSocketServer() override   { stop(); }

    /** Binds the port and starts accepting. Returns false if it is taken. */
    bool start (int port);
    void stop();

    int getPort() const noexcept   { return listenPort; }

private:
    void run() override;   // accept loop

    class ConnectionThread;
    void serve (std::unique_ptr<juce::StreamingSocket> socket);
    void reapFinishedThreads();

    std::unique_ptr<juce::StreamingSocket> listener;
    int listenPort = 0;

    juce::CriticalSection threadsLock;
    juce::OwnedArray<juce::Thread> threads;

    JUCE_DECLARE_NON_COPYABLE (WebSocketServer)
};

} // namespace formuls
