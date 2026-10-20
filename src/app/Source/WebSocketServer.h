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
 * Not supported, because the GUI client never needs them: TLS, permessage-
 * deflate, binary messages, fragmented messages from the server side.
 *
 * Prototype status (ws-prototype branch): see docs/embedded-gui/README.md.
 */

#pragma once

#include <JuceHeader.h>

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

        /** Sends one text frame. Thread-safe; blocks until written or the
            socket fails. Returns false once the connection is gone. */
        bool sendText (const juce::String& text);
        bool sendText (const char* utf8, size_t numBytes);

        void close();
        bool isOpen() const noexcept   { return open.load(); }

    private:
        friend class WebSocketServer;
        Connection (std::unique_ptr<juce::StreamingSocket> s, juce::String p, juce::String addr)
            : path (std::move (p)), remoteAddress (std::move (addr)), socket (std::move (s)) {}

        bool sendFrame (int opcode, const void* data, size_t numBytes);
        bool writeAll (const void* data, size_t numBytes);

        std::unique_ptr<juce::StreamingSocket> socket;
        juce::CriticalSection writeLock;
        std::atomic<bool> open { true };
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
