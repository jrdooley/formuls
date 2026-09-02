// Minimal libpd harness: loads a patch containing only abl_link~, sends it a
// series of tempo changes, and reports the beat rate it actually produces
// after each one. No formuls patch involved, so anything that goes wrong
// here is the external's doing.
#include "PdBase.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <thread>

class Recv : public pd::PdReceiver {
public:
    double lastBeat = 0; double lastTempo = 0; bool sawTempo = false;
    void print(const std::string& m) override {
        if (m.rfind("BEAT: ",0)==0) lastBeat = atof(m.c_str()+6);
        else if (m.rfind("TEMPO: ",0)==0) { lastTempo = atof(m.c_str()+7); sawTempo = true; }
        else std::printf("  pd: %s\n", m.c_str());
    }
};

int main(int argc, char** argv) {
    const int sr = 48000, blk = 64;
    pd::PdBase pd; Recv recv;
    if (!pd.init(0, 2, sr, true)) { std::puts("init failed"); return 1; }
    pd.setReceiver(&recv);
    pd.addToSearchPath(argv[1]);              // folder holding abl_link~
    pd.computeAudio(true);
    auto patch = pd.openPatch(argc>2?argv[2]:"test.pd", ".");
    if (!patch.isValid()) { std::puts("patch failed to open"); return 1; }

    std::vector<float> in(blk,0.f), out(blk*2,0.f);

    // abl_link~ drives itself from the REAL host clock, so the patch has to
    // be rendered in real time; running flat out makes the beat crawl.
    using Clock = std::chrono::steady_clock;
    auto nextDue = Clock::now();
    auto runFor = [&](double seconds) {
        const int chunkTicks = 8;                      // 512 frames
        const auto chunkDur = std::chrono::duration<double>(chunkTicks * blk / (double)sr);
        const auto until = Clock::now() + std::chrono::duration<double>(seconds);
        while (Clock::now() < until) {
            for (int i = 0; i < chunkTicks; ++i)
                pd.processFloat(1, in.data(), out.data());
            pd.receiveMessages();
            nextDue += std::chrono::duration_cast<Clock::duration>(chunkDur);
            std::this_thread::sleep_until(nextDue);
        }
    };

    if (std::getenv("ENABLE_LINK")) { pd.sendFloat("doconnect", 1); std::puts("  [Link ENABLED]"); }
    else std::puts("  [Link DISABLED - as in formuls by default]");
    if (const char* rep = std::getenv("REPEAT")) pd.sendFloat("dorepeat", atof(rep));
    runFor(1.0);

    // Model a slider drag: a new distinct tempo every 30 ms, then stop and
    // see where it settles.
    auto sweep = [&](double from, double to, int stepMs) {
        const int dir = to > from ? 1 : -1;
        for (double v = from; dir > 0 ? v <= to : v >= to; v += dir) {
            pd.sendFloat("settempo", (float)v);
            runFor(stepMs / 1000.0);      // real-time paced, like the tablet
        }
    };
    auto settle = [&](double expect, const char* what) {
        runFor(1.0);
        const double b0 = recv.lastBeat;
        const auto t0 = Clock::now();
        runFor(3.0);
        const double secs = std::chrono::duration<double>(Clock::now()-t0).count();
        const double measured = (recv.lastBeat - b0)/secs*60.0;
        std::printf("%-34s expected %6.1f -> beat %6.1f (reported %6.1f)  %s\n",
                    what, expect, measured, recv.lastTempo,
                    (measured > expect*0.9 && measured < expect*1.1) ? "ok" : "*** STUCK ***");
    };
    pd.sendFloat("settempo", 120.f); settle(120, "baseline");
    sweep(120, 100, atoi(getenv("STEPMS") ? getenv("STEPMS") : "30"));   settle(100, "after fast drag 120->100");

    pd.closePatch(patch);
    return 0;
}
