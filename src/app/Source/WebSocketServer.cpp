/*
 * WebSocketServer.cpp -- see WebSocketServer.h.
 */

#if defined (_WIN32)
 #include <winsock2.h>   // before anything pulls in windows.h
#endif

#include "WebSocketServer.h"

#if ! defined (_WIN32)
 #include <cerrno>
 #include <poll.h>
 #include <sys/socket.h>
#endif

namespace formuls
{

namespace
{
    // SHA-1, only for the RFC 6455 handshake (JUCE has no SHA-1).
    std::array<juce::uint8, 20> sha1 (const void* data, size_t len)
    {
        juce::uint32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
        std::vector<juce::uint8> m ((const juce::uint8*) data, (const juce::uint8*) data + len);
        const juce::uint64 bits = (juce::uint64) len * 8;
        m.push_back (0x80);
        while (m.size() % 64 != 56) m.push_back (0);
        for (int i = 7; i >= 0; --i) m.push_back ((juce::uint8) (bits >> (i * 8)));

        auto rol = [] (juce::uint32 v, int n) { return (v << n) | (v >> (32 - n)); };

        for (size_t chunk = 0; chunk < m.size(); chunk += 64)
        {
            juce::uint32 w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (juce::uint32) m[chunk + i * 4] << 24 | (juce::uint32) m[chunk + i * 4 + 1] << 16
                     | (juce::uint32) m[chunk + i * 4 + 2] << 8 | (juce::uint32) m[chunk + i * 4 + 3];
            for (int i = 16; i < 80; ++i)
                w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

            auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                juce::uint32 f, k;
                if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
                const auto t = rol (a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol (b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }

        std::array<juce::uint8, 20> out;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j)
                out[(size_t) (i * 4 + j)] = (juce::uint8) (h[i] >> (24 - j * 8));
        return out;
    }

    juce::String acceptKey (const juce::String& clientKey)
    {
        const auto joined = (clientKey.trim() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toStdString();
        const auto digest = sha1 (joined.data(), joined.size());
        return juce::Base64::toBase64 (digest.data(), digest.size());
    }

    // The reader uses read (..., blockUntilSpecifiedAmountHasArrived = false),
    // and JUCE implements that by switching the socket to non-blocking. So a
    // send() can return -1 (EAGAIN) whenever the kernel buffer is full.
    //
    // Waiting for room is done on the raw handle, not with waitUntilReady():
    // JUCE's wait only try-locks the socket's read lock, which the reader
    // thread holds for nearly all of its own 200 ms wait, so it returns -1 at
    // once and a full buffer looked like a dead client.
    bool wouldBlock()
    {
       #if defined (_WIN32)
        return WSAGetLastError() == WSAEWOULDBLOCK;
       #else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
       #endif
    }

    int waitWritable (juce::StreamingSocket& socket, int timeoutMs)
    {
       #if defined (_WIN32)
        WSAPOLLFD p { (SOCKET) socket.getRawSocketHandle(), POLLWRNORM, 0 };
        return WSAPoll (&p, 1, timeoutMs);
       #else
        pollfd p { socket.getRawSocketHandle(), POLLOUT, 0 };
        const auto r = poll (&p, 1, timeoutMs);
        return (r < 0 && errno == EINTR) ? 0 : r;
       #endif
    }

    // Writes everything, waiting while the kernel buffer is full. Gives up if
    // nothing is accepted for stallTimeoutMs, or once keepGoing() is false.
    template <typename KeepGoing>
    bool writeFully (juce::StreamingSocket& socket, const void* data, size_t numBytes,
                     int stallTimeoutMs, KeepGoing&& keepGoing)
    {
        auto* p = static_cast<const char*> (data);
        auto lastProgress = juce::Time::getMillisecondCounter();

        while (numBytes > 0)
        {
            if (! keepGoing())
                return false;

            const auto n = socket.write (p, (int) juce::jmin (numBytes, (size_t) 1 << 20));

            if (n > 0)
            {
                p += n;
                numBytes -= (size_t) n;
                lastProgress = juce::Time::getMillisecondCounter();
            }
            else if (! wouldBlock()
                     || waitWritable (socket, 200) < 0
                     || juce::Time::getMillisecondCounter() - lastProgress > (juce::uint32) stallTimeoutMs)
            {
                return false;
            }
        }

        return true;
    }

    // Closing straight after the last write can reset the connection with the
    // response still in flight; wait (briefly) for the browser to close first.
    void closeGracefully (juce::StreamingSocket& socket)
    {
        char scratch[512];
        for (int i = 0; i < 20 && socket.waitUntilReady (true, 100) >= 0; ++i)
            if (socket.waitUntilReady (true, 0) > 0 && socket.read (scratch, sizeof (scratch), false) <= 0)
                break;
        socket.close();
    }

    juce::String statusText (int status)
    {
        switch (status)
        {
            case 200: return "OK";
            case 101: return "Switching Protocols";
            case 304: return "Not Modified";
            case 403: return "Forbidden";
            default:  return "Not Found";
        }
    }
}

//==============================================================================
WebSocketServer::Connection::Connection (std::unique_ptr<juce::StreamingSocket> s, juce::String p, juce::String addr)
    : path (std::move (p)), remoteAddress (std::move (addr)), socket (std::move (s))
{
    writer = std::thread ([this] { writerLoop(); });
}

WebSocketServer::Connection::~Connection()
{
    close();
}

bool WebSocketServer::Connection::sendFrame (int opcode, const void* data, size_t numBytes)
{
    juce::uint8 header[10];
    size_t headerSize = 2;
    header[0] = (juce::uint8) (0x80 | opcode);   // FIN, never fragmented

    if (numBytes < 126)
        header[1] = (juce::uint8) numBytes;
    else if (numBytes < 65536)
    {
        header[1] = 126;
        header[2] = (juce::uint8) (numBytes >> 8);
        header[3] = (juce::uint8) numBytes;
        headerSize = 4;
    }
    else
    {
        header[1] = 127;
        for (int i = 0; i < 8; ++i)
            header[2 + i] = (juce::uint8) ((juce::uint64) numBytes >> (56 - i * 8));
        headerSize = 10;
    }

    juce::MemoryBlock frame (header, headerSize);
    frame.append (data, numBytes);

    {
        const std::lock_guard<std::mutex> sl (queueLock);

        if (! open)
            return false;

        if (queuedBytes + frame.getSize() <= maxQueuedBytes)
        {
            queuedBytes += frame.getSize();
            queue.push_back (std::move (frame));
            queueChanged.notify_one();
            return true;
        }
    }

    // Too far behind to catch up: a gap would leave its controls showing
    // stale values, so drop it instead and let it reconnect.
    juce::Logger::writeToLog ("GUI client at " + remoteAddress + " fell "
                              + juce::String ((juce::int64) (maxQueuedBytes >> 20)) + " MB behind; disconnecting");
    fail();
    return false;
}

void WebSocketServer::Connection::writerLoop()
{
    for (;;)
    {
        juce::MemoryBlock frame;
        {
            std::unique_lock<std::mutex> sl (queueLock);
            queueChanged.wait (sl, [this] { return ! open || ! queue.empty(); });

            if (! open)
                return;

            frame = std::move (queue.front());
            queue.pop_front();
        }

        const bool ok = writeQueued (frame);

        {
            const std::lock_guard<std::mutex> sl (queueLock);
            queuedBytes -= frame.getSize();
        }

        if (! ok)
        {
            if (open)
                juce::Logger::writeToLog ("GUI client at " + remoteAddress + " stopped accepting data; disconnecting");
            fail();
            return;
        }
    }
}

bool WebSocketServer::Connection::writeQueued (const juce::MemoryBlock& frame)
{
    return writeFully (*socket, frame.getData(), frame.getSize(), stallTimeoutMs,
                       [this] { return open.load(); });
}

void WebSocketServer::Connection::fail()
{
    const std::lock_guard<std::mutex> cl (closeLock);

    if (closed)
        return;   // close() is tearing down already

    {
        const std::lock_guard<std::mutex> sl (queueLock);
        open = false;
        queueChanged.notify_all();
    }

    // Wakes the reader thread, whose read then fails: it closes this
    // connection and tells EmbeddedGuiServer, as for any dropped client.
   #if defined (_WIN32)
    ::shutdown ((SOCKET) socket->getRawSocketHandle(), SD_BOTH);
   #else
    ::shutdown (socket->getRawSocketHandle(), SHUT_RDWR);
   #endif
}

bool WebSocketServer::Connection::sendText (const char* utf8, size_t numBytes)
{
    return sendFrame (0x1, utf8, numBytes);
}

bool WebSocketServer::Connection::sendText (const juce::String& text)
{
    const auto utf8 = text.toRawUTF8();
    return sendText (utf8, std::strlen (utf8));
}

void WebSocketServer::Connection::close()
{
    bool wasOpen;
    {
        const std::lock_guard<std::mutex> cl (closeLock);
        if (closed)
            return;
        closed = true;

        const std::lock_guard<std::mutex> sl (queueLock);
        wasOpen = open.exchange (false);
        queueChanged.notify_all();
    }

    // The writer notices within one 200 ms wait at most.
    if (writer.joinable() && writer.get_id() != std::this_thread::get_id())
        writer.join();
    else if (writer.joinable())
        writer.detach();

    if (wasOpen)
    {
        const juce::uint8 frame[2] = { 0x88, 0 };
        socket->write (frame, 2);   // best effort; the socket is non-blocking
    }
    socket->close();
}

//==============================================================================
class WebSocketServer::ConnectionThread : public juce::Thread
{
public:
    ConnectionThread (WebSocketServer& o, std::unique_ptr<juce::StreamingSocket> s)
        : juce::Thread ("formuls GUI connection"), owner (o), socket (std::move (s)) {}

    ~ConnectionThread() override
    {
        signalThreadShouldExit();
        if (connection != nullptr) connection->close();
        else if (socket != nullptr) socket->close();
        stopThread (2000);
    }

    void run() override
    {
        HttpRequest request;
        if (! readRequest (request))
            return;

        if (request.headers["upgrade"].equalsIgnoreCase ("websocket"))
            runWebSocket (request);
        else
            answerHttp (request);
    }

private:
    juce::StreamingSocket& sock()   { return connection != nullptr ? *connection->socket : *socket; }

    // Reads exactly n bytes, giving up if the thread is asked to exit.
    bool readExact (void* dest, size_t n)
    {
        auto* p = static_cast<char*> (dest);

        while (n > 0)
        {
            if (threadShouldExit())
                return false;

            const auto ready = sock().waitUntilReady (true, 200);
            if (ready < 0) return false;
            if (ready == 0) continue;

            const auto got = sock().read (p, (int) juce::jmin (n, (size_t) 65536), false);
            if (got <= 0) return false;
            p += got;
            n -= (size_t) got;
        }

        return true;
    }

    bool readRequest (HttpRequest& request)
    {
        juce::MemoryOutputStream head;
        char c;
        int matched = 0;   // progress through "\r\n\r\n"

        while (matched < 4)
        {
            if (head.getDataSize() > 32768 || ! readExact (&c, 1))
                return false;
            head.writeByte (c);
            matched = (c == "\r\n\r\n"[matched]) ? matched + 1 : (c == '\r' ? 1 : 0);
        }

        auto lines = juce::StringArray::fromLines (head.toString());
        auto first = juce::StringArray::fromTokens (lines[0], " ", "");
        if (first.size() < 2)
            return false;

        request.method = first[0];
        request.path = first[1];
        request.remoteAddress = socket->getHostName();

        for (int i = 1; i < lines.size(); ++i)
        {
            const auto colon = lines[i].indexOfChar (':');
            if (colon > 0)
                request.headers.set (lines[i].substring (0, colon).trim().toLowerCase(),
                                     lines[i].substring (colon + 1).trim());
        }

        return true;
    }

    void answerHttp (const HttpRequest& request)
    {
        auto response = owner.onHttpRequest ? owner.onHttpRequest (request) : HttpResponse();

        juce::String head;
        head << "HTTP/1.1 " << response.status << " " << statusText (response.status) << "\r\n"
             << "Content-Type: " << response.contentType << "\r\n"
             << "Content-Length: " << (juce::int64) response.body.getSize() << "\r\n"
             << "Connection: close\r\n";
        for (auto& key : response.extraHeaders.getAllKeys())
            head << key << ": " << response.extraHeaders[key] << "\r\n";
        head << "\r\n";

        auto keepGoing = [this] { return ! threadShouldExit(); };
        if (writeFully (*socket, head.toRawUTF8(), std::strlen (head.toRawUTF8()), 5000, keepGoing))
            writeFully (*socket, response.body.getData(), response.body.getSize(), 5000, keepGoing);
        closeGracefully (*socket);
    }

    void runWebSocket (const HttpRequest& request)
    {
        juce::String head;
        head << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << acceptKey (request.headers["sec-websocket-key"]) << "\r\n\r\n";
        if (! writeFully (*socket, head.toRawUTF8(), std::strlen (head.toRawUTF8()), 5000,
                          [this] { return ! threadShouldExit(); }))
            return;

        connection.reset (new Connection (std::move (socket), request.path, request.remoteAddress));

        if (owner.onOpen) owner.onOpen (connection);

        juce::MemoryBlock message;   // reassembles continuation frames

        while (! threadShouldExit() && connection->isOpen())
        {
            juce::uint8 h[2];
            if (! readExact (h, 2))
                break;

            const int opcode = h[0] & 0x0f;
            const bool fin = (h[0] & 0x80) != 0;
            const bool masked = (h[1] & 0x80) != 0;
            juce::uint64 length = h[1] & 0x7f;

            if (length == 126)
            {
                juce::uint8 ext[2];
                if (! readExact (ext, 2)) break;
                length = (juce::uint64) ext[0] << 8 | ext[1];
            }
            else if (length == 127)
            {
                juce::uint8 ext[8];
                if (! readExact (ext, 8)) break;
                length = 0;
                for (auto b : ext) length = length << 8 | b;
            }

            if (length > (64u << 20))   // a GUI never sends anything near this
                break;

            juce::uint8 mask[4] = {};
            if (masked && ! readExact (mask, 4))
                break;

            juce::MemoryBlock payload ((size_t) length);
            if (length > 0 && ! readExact (payload.getData(), (size_t) length))
                break;

            if (masked)
                for (size_t i = 0; i < (size_t) length; ++i)
                    static_cast<juce::uint8*> (payload.getData())[i] ^= mask[i % 4];

            if (opcode == 0x8)                                    // close
                break;
            if (opcode == 0x9)                                    // ping -> pong
            {
                connection->sendFrame (0xA, payload.getData(), payload.getSize());
                continue;
            }
            if (opcode == 0xA)                                    // pong
                continue;

            if (opcode == 0x1 || opcode == 0x0)                   // text / continuation
            {
                message.append (payload.getData(), payload.getSize());
                if (fin)
                {
                    if (owner.onMessage)
                        owner.onMessage (connection, juce::String::fromUTF8 ((const char*) message.getData(),
                                                                             (int) message.getSize()));
                    message.reset();
                }
            }
        }

        connection->close();
        if (owner.onClose) owner.onClose (connection);
    }

    WebSocketServer& owner;
    std::unique_ptr<juce::StreamingSocket> socket;
    ConnectionPtr connection;
};

//==============================================================================
bool WebSocketServer::start (int port)
{
    stop();

    listener = std::make_unique<juce::StreamingSocket>();
    if (! listener->createListener (port))
    {
        listener.reset();
        return false;
    }

    listenPort = port;
    startThread();
    return true;
}

void WebSocketServer::stop()
{
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (2000);
    listener.reset();

    const juce::ScopedLock sl (threadsLock);
    threads.clear();   // each destructor closes its socket and joins
}

void WebSocketServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());

        if (client == nullptr)
        {
            if (threadShouldExit()) break;
            continue;
        }

        serve (std::move (client));
    }
}

void WebSocketServer::serve (std::unique_ptr<juce::StreamingSocket> socket)
{
    reapFinishedThreads();

    auto* thread = new ConnectionThread (*this, std::move (socket));
    {
        const juce::ScopedLock sl (threadsLock);
        threads.add (thread);
    }
    thread->startThread();
}

void WebSocketServer::reapFinishedThreads()
{
    const juce::ScopedLock sl (threadsLock);

    for (int i = threads.size(); --i >= 0;)
        if (! threads[i]->isThreadRunning())
            threads.remove (i);
}

} // namespace formuls
