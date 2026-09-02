// Loads the REAL formuls _main.pd and drives BPM the way the GUI does,
// measuring the beat rate abl_link~ actually produces after each change.
#include "PdBase.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

class Recv : public pd::PdReceiver {
public:
    double lastBeat = 0; long beatMsgs = 0;
    void print(const std::string&) override {}          // patch is noisy; ignore
    void receiveFloat(const std::string& dest, float v) override {
        if (dest == "link") { lastBeat = v; ++beatMsgs; }
    }
};

int main(int argc, char** argv) {
    const int sr = 48000, blk = 64;
    const std::string pdDir = argv[1];
    pd::PdBase pd; Recv recv;
    if (!pd.init(0, 2, sr, true)) { std::puts("init failed"); return 1; }
    pd.setReceiver(&recv);
    pd.addToSearchPath(pdDir + "/externals");
    pd.addToSearchPath(pdDir + "/controlabstractions");
    pd.addToSearchPath(pdDir + "/audioabstractions");
    pd.subscribe("link");                                // abl_link~ beat stream
    pd.computeAudio(true);
    auto patch = pd.openPatch("_main.pd", pdDir);
    if (!patch.isValid()) { std::puts("patch failed to open"); return 1; }

    std::vector<float> in(blk,0.f), out(blk*2,0.f);
    using Clock = std::chrono::steady_clock;
    auto nextDue = Clock::now();
    auto runFor = [&](double seconds) {
        const int chunkTicks = 8;
        const auto chunkDur = std::chrono::duration<double>(chunkTicks*blk/(double)sr);
        const auto until = Clock::now() + std::chrono::duration<double>(seconds);
        while (Clock::now() < until) {
            for (int i = 0; i < chunkTicks; ++i)
                pd.processFloat(1, in.data(), out.data());
            pd.receiveMessages();
            nextDue += std::chrono::duration_cast<Clock::duration>(chunkDur);
            if (nextDue > Clock::now()) std::this_thread::sleep_until(nextDue);
            else nextDue = Clock::now();          // fell behind; don't spiral
        }
    };

    runFor(2.0);                                   // settle / loadbangs
    std::printf("beat messages seen while idle: %ld\n", recv.beatMsgs);

    // Send real OSC to the patch's netreceive on 9000, exactly as the
    // Open Stage Control GUI does. oscparse turns /bpm 0.5 into "bpm 0.5",
    // which the patch's [route ... bpm ...] picks up. BPM travels
    // normalised 0..1 internally and is scaled by 240.
    const std::string addr = (argc > 2) ? argv[2] : "/bpm";
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

    auto sendOsc = [&](const std::string& address, float value) {
        auto pad = [](std::string b){ b.push_back('\0'); while (b.size()%4) b.push_back('\0'); return b; };
        std::string pkt = pad(address) + pad(",f");
        uint32_t bits; std::memcpy(&bits, &value, 4); bits = htonl(bits);
        pkt.append(reinterpret_cast<const char*>(&bits), 4);
        sendto(sock, pkt.data(), pkt.size(), 0, (sockaddr*)&dst, sizeof(dst));
    };
    std::printf("driving via OSC %s (normalised, x240)\n\n", addr.c_str());

    struct Step { const char* addr; double val; const char* note; };
    const Step steps[] = {
        {"/bpm", 140, "baseline"},
        {"/bpm",  90, "another change"},
        {"/bpmhold", 1, "ENGAGE bpmhold"},
        {"/bpm", 160, "change while held"},
        {"/bpm", 100, "change while held"},
        {"/bpmhold", 0, "RELEASE bpmhold"},
        {"/bpm", 160, "change after release"},
    };
    for (auto& st : steps) {
        sendOsc(st.addr, (float)st.val);
        runFor(1.0);
        const double b0 = recv.lastBeat;
        const auto t0 = Clock::now();
        runFor(2.5);
        const double secs = std::chrono::duration<double>(Clock::now()-t0).count();
        const double measured = (recv.lastBeat - b0)/secs*60.0;
        std::printf("%-28s %-10s %6.1f -> beat %6.1f bpm\n",
                    st.note, st.addr, st.val, measured);
    }

    close(sock);

    pd.closePatch(patch);
    return 0;
}
