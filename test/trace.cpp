// Runs an engine the way the firmware simulator does (1 ms steps, 30 BPM clock)
// and writes "tick gate cv" on every change, so the output can be compared with
// the firmware's own trace (see test/README.md).
//   trace <engine> <scenario.txt> <out.txt>
#include "../src/engine/NoteEngine.hpp"
#if __has_include("../src/engine/TuesdayEngine.hpp")
#include "../src/engine/TuesdayEngine.hpp"
#define HAVE_TUESDAY 1
#endif
#if __has_include("../src/engine/StochasticEngine.hpp")
#include "../src/engine/StochasticEngine.hpp"
#define HAVE_STOCHASTIC 1
#endif
#if __has_include("../src/engine/IndexedEngine.hpp")
#include "../src/engine/IndexedEngine.hpp"
#define HAVE_INDEXED 1
#endif
#if __has_include("../src/engine/DiscreteEngine.hpp")
#include "../src/engine/DiscreteEngine.hpp"
#define HAVE_DISCRETE 1
#endif
#if __has_include("../src/engine/PhaseFluxEngine.hpp")
#include "../src/engine/PhaseFluxEngine.hpp"
#define HAVE_PHASEFLUX 1
#endif
#if __has_include("../src/engine/FractalEngine.hpp")
#include "../src/engine/FractalEngine.hpp"
#define HAVE_FRACTAL 1
#endif
#include <cstdio>
#include <fstream>
#include <sstream>
#include <memory>

using namespace xf;
typedef std::vector<std::string> Tokens;

// a uniform face for the engines
struct Runner {
    virtual ~Runner() {}
    virtual void param(const Tokens &t) = 0;
    virtual void init(bool renew) {}
    virtual void reset() = 0;
    virtual bool tick(uint32_t t) = 0;
    virtual void update() {}
    virtual bool hasUpdate() { return true; }
    virtual void bar() {}
    virtual bool gate() = 0;
    virtual float cv() = 0;
    virtual void parentParam(const Tokens &t) {}
};

template <typename E> auto callRefInit(E &e, bool renew, int) -> decltype(e.refInit(renew), void()) { e.refInit(renew); }
template <typename E> void callRefInit(E &, bool, long) {}

template <typename E> struct R : Runner {
    E e;
    void init(bool renew) override { callRefInit(e, renew, 0); }
    R() { e.quirks = true; }
    void param(const Tokens &t) override { e.setRefParam(t); }
    void reset() override { e.reset(); }
    bool tick(uint32_t t) override { return e.tick(t); }
    void update() override { e.update(); }
    void bar() override { e.barBoundary(); }
    bool gate() override { return e.gateOutput; }
    float cv() override { return e.cvOutput; }
};

#ifdef HAVE_FRACTAL
// Fractal listens to a Note track (track 0, "p ..." lines)
struct FractalRunner : Runner {
    FractalEngine e;
    NoteEngine parent;
    FractalRunner() {
        e.quirks = true; parent.quirks = true;
        e.rngSeed = 0x6f7261 + 1;
        e.inlineSource = true;
    }
    void parentParam(const Tokens &t) override { parent.setRefParam(t); }
    void param(const Tokens &t) override { e.setRefParam(t); }
    void init(bool renew) override { e.refInit(renew); parent.reset(); }
    void reset() override { e.reset(); }
    bool tick(uint32_t t) override {
        if (t > 0 && t % 768 == 0) parent.barBoundary();
        bool a = parent.tick(t);
        e.srcGate = parent.gateOutput; e.srcCv = parent.cvOutput;
        bool b = e.tick(t);
        return a || b;
    }
    void update() override { parent.update(); e.update(); }
    bool gate() override { return e.gateOutput; }
    float cv() override { return e.cvOutput; }
};

#endif


int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: trace <engine> <scenario> <out>\n"); return 2; }
    std::string name = argv[1];
    std::unique_ptr<Runner> r;
    if (name == "note") r.reset(new R<NoteEngine>());
#ifdef HAVE_TUESDAY
    else if (name == "tuesday") r.reset(new R<TuesdayEngine>());
#endif
#ifdef HAVE_STOCHASTIC
    else if (name == "stochastic") r.reset(new R<StochasticEngine>());
#endif
#ifdef HAVE_INDEXED
    else if (name == "indexed") r.reset(new R<IndexedEngine>());
#endif
#ifdef HAVE_DISCRETE
    else if (name == "discrete") r.reset(new R<DiscreteEngine>());
#endif
#ifdef HAVE_PHASEFLUX
    else if (name == "phaseflux") r.reset(new R<PhaseFluxEngine>());
#endif
#ifdef HAVE_FRACTAL
    else if (name == "fractal") r.reset(new FractalRunner());
#endif
    else { fprintf(stderr, "unknown engine\n"); return 2; }

    long ticks = 2000; bool renew = false;
    std::ifstream in(argv[2]);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        Tokens t; std::string w;
        while (ss >> w) t.push_back(w);
        if (t.empty() || t[0] == "engine") continue;
        if (t[0] == "ticks") { ticks = std::stol(t[1]); continue; }
        if (t[0] == "renew") { renew = true; continue; }
        if (t[0] == "p") { r->parentParam(Tokens(t.begin() + 1, t.end())); continue; }
        r->param(t);
    }
    r->init(renew);
    r->reset();

    FILE *out = fopen(argv[3], "w");
    long lastExtra = 0, tick = 0;
    int lastGate = -1; float lastCv = -1000;
    bool all = getenv("XF_ALL") != nullptr;
    for (long ms = 0;; ms++) {
        // the simulator's clock: tick n happens at (n+1) * 10416 us (30 BPM)
        while ((tick + 1) * 10416 <= ms * 1000) {
            if (tick > 0 && tick % 768 == 0) r->bar();
            // after a tick that updated the CV, one extra update, at most every 25 ms
            if (r->tick(tick) && ms - lastExtra >= 25) { lastExtra = ms; r->update(); }
            tick++;
        }
        r->update();
        if (tick >= ticks) break;
        int g = r->gate() ? 1 : 0;
        float cv = r->cv();
        double f4 = std::floor(double(cv) * 10000 + 0.5) / 10000;
        if (all) fprintf(out, "ms %ld %ld %d %.4f\n", ms, std::max(0L, tick - 1), g, f4 == 0 ? 0.0 : f4);
        else if (g != lastGate || (g && cv != lastCv)) {
            fprintf(out, "%ld %d %.4f\n", tick - 1, g, f4 == 0 ? 0.0 : f4);
            lastGate = g; lastCv = cv;
        }
    }
    fclose(out);
    return 0;
}
