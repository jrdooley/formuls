/*
 * PdOscOut.h
 *
 * Sends the patch's OSC to the control GUI from the app, outside Pd's lock.
 *
 * The mirror image of OscBridge. That class moved *receiving* out of the patch
 * because parsing on the audio thread crashed it. This one moves *sending* out,
 * for timing.
 *
 * The patch used to end every GUI update in [netsend -u -b]: one sendto()
 * system call per message, made while Pd holds sys_lock, the same lock the
 * audio callback takes to render each 64-frame tick. A global reset makes the
 * patch send 7,354 messages in one go. Measured in headless Pd, about half of
 * the reset's 35-55 ms was sendto(). Inside the app that whole time kept the
 * audio thread waiting, and the output dropped out: audible clicks, logged by
 * macOS as an IO overload with formuls as the late client.
 *
 * Now the patch's send subpatch (_main.pd, O-S-C_&_FORMULS_SEND) routes its
 * OSC packets to [s formuls-osc-out] instead of [netsend] once the app sends
 * `formuls-app-osc 1`. This class binds a receiver to that name. Its list method
 * runs inside Pd, under sys_lock, on whichever thread Pd is running (message
 * or audio). It only copies the packet's bytes into a pre-allocated lock-free
 * FIFO. A background thread drains the FIFO and does the UDP sends.
 *
 * In plain Pd nothing is bound to formuls-osc-out and nobody sends
 * formuls-app-osc, so the patch keeps using [netsend] exactly as before.
 *
 * Threading: producers run only under sys_lock, so there is only ever one at a
 * time, and the mutex orders them. That makes the single-producer
 * juce::AbstractFifo safe here. Each packet is written as one record,
 * [uint32 length][bytes], in a single write, so the consumer never sees half
 * of one.
 */

#pragma once

#include <JuceHeader.h>
#include "z_libpd.h"   // m_pd.h: class_new, pd_bind, t_atom ...

namespace formuls
{

class PdOscOut : private juce::Thread
{
public:
    PdOscOut() : juce::Thread ("formuls OSC out") {}
    ~PdOscOut() override { stop(); }

    // Message thread, with the audio callback not yet running (or already
    // removed): binds the receiver and starts the sender. Returns false if the
    // socket could not be created; the patch then keeps its own [netsend].
    bool start (int destinationPort)
    {
        stop();
        port = destinationPort;

        socket = std::make_unique<juce::DatagramSocket> (false);
        if (! socket->bindToPort (0))
        {
            socket.reset();
            return false;
        }

        if (receiverClass == nullptr)
        {
            receiverClass = class_new (gensym ("formuls_oscout"), nullptr, nullptr,
                                       sizeof (Receiver), CLASS_PD, A_NULL);
            class_addlist (receiverClass, (t_method) &Receiver::list);
        }

        receiver = (Receiver*) pd_new (receiverClass);
        receiver->owner = this;
        pd_bind (&receiver->pd, gensym (bindName));

        startThread (juce::Thread::Priority::high);
        return true;
    }

    // Message thread, with the audio callback removed and OscBridge stopped.
    void stop()
    {
        if (receiver != nullptr)
        {
            pd_unbind (&receiver->pd, gensym (bindName));
            pd_free (&receiver->pd);
            receiver = nullptr;
        }

        stopThread (1000);
        socket.reset();
    }

    // Packets dropped because the FIFO was full, or because one was too
    // large, since the last call (then reset). For the diagnostics summary.
    int takeDroppedCount() noexcept   { return dropped.exchange (0); }
    int takeSentCount() noexcept      { return sent.exchange (0); }

    static constexpr const char* bindName = "formuls-osc-out";

private:
    struct Receiver
    {
        t_pd pd;               // must be first: this is a Pd object
        PdOscOut* owner;

        // Under sys_lock, on the message or audio thread: copy, never block.
        static void list (Receiver* x, t_symbol*, int argc, t_atom* argv)
        {
            x->owner->push (argc, argv);
        }
    };

    void push (int argc, const t_atom* argv) noexcept
    {
        if (argc <= 0 || argc > maxPacketBytes)
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        const int need = 4 + argc;

        if (fifo.getFreeSpace() < need)
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        const auto scope = fifo.write (need);
        int written = 0;
        auto put = [&] (juce::uint8 byte)
        {
            const int i = written < scope.blockSize1 ? scope.startIndex1 + written
                                                     : scope.startIndex2 + (written - scope.blockSize1);
            buffer[(size_t) i] = byte;
            ++written;
        };

        const auto length = (juce::uint32) argc;
        put ((juce::uint8) (length & 0xff));
        put ((juce::uint8) ((length >> 8) & 0xff));
        put ((juce::uint8) ((length >> 16) & 0xff));
        put ((juce::uint8) ((length >> 24) & 0xff));

        for (int i = 0; i < argc; ++i)
            put ((juce::uint8) (int) atom_getfloat (argv + i));   // oscformat emits bytes as floats 0..255
    }

    void run() override
    {
        juce::HeapBlock<juce::uint8> packet ((size_t) maxPacketBytes);

        while (! threadShouldExit())
        {
            bool any = false;

            while (fifo.getNumReady() >= 4)
            {
                juce::uint8 header[4];
                read (header, 4);
                const auto length = (int) (header[0] | (header[1] << 8) | (header[2] << 16) | ((juce::uint32) header[3] << 24));
                read (packet.get(), length);   // written in the same record, so already there

                if (socket != nullptr)
                    socket->write ("127.0.0.1", port, packet.get(), length);

                sent.fetch_add (1, std::memory_order_relaxed);
                any = true;
            }

            if (! any)
                wait (1);   // a 1 ms poll: no signalling from the audio thread
        }
    }

    void read (juce::uint8* dest, int count) noexcept
    {
        const auto scope = fifo.read (count);
        std::memcpy (dest, buffer.data() + scope.startIndex1, (size_t) scope.blockSize1);
        if (scope.blockSize2 > 0)
            std::memcpy (dest + scope.blockSize1, buffer.data() + scope.startIndex2, (size_t) scope.blockSize2);
    }

    // A reset is ~7,400 packets of ~60 bytes; 4 MB holds many of those.
    static constexpr int fifoBytes = 4 * 1024 * 1024;
    static constexpr int maxPacketBytes = 65507;   // largest UDP payload

    juce::AbstractFifo fifo { fifoBytes };
    std::vector<juce::uint8> buffer = std::vector<juce::uint8> ((size_t) fifoBytes);
    std::unique_ptr<juce::DatagramSocket> socket;
    int port = 0;
    Receiver* receiver = nullptr;
    std::atomic<int> dropped { 0 }, sent { 0 };

    static inline t_class* receiverClass = nullptr;

    JUCE_DECLARE_NON_COPYABLE (PdOscOut)
};

} // namespace formuls
