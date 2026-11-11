// STOCHASTIC engine (engine/StochasticTrackEngine.cpp, StochasticGenerator.cpp,
// StochasticCache.cpp). A probabilistic phrase generator. Rhythm and melody are two
// separate "domains", each either LIVE (rolled fresh on every note) or LOOP (a stored
// pattern of up to 64 steps that repeats and can slowly evolve through Patience,
// Mutate and Jump). Playback reads a "cache": one cell per step holding duration,
// pitch, gate length, rest, legato and slide, rebuilt whenever a shaping knob moves.
#pragma once
#include "Common.hpp"
#include <string>
#include <functional>

namespace xf {

namespace st {
static const int STEP_COUNT = 64;
static const int USER_SCALE_SIZE = 32;
static const int MAX_BURST = 8;
static const int MIN_AUDIBLE_GATE = 6;
static const int MIN_CLUSTER_CELL = 12;
static const int MAX_CELL_DURATION = 4095;
static const int MIN_BURST_PARENT = 96;
static const int DURATION_LUT[8][2] = {{8, 1}, {4, 1}, {3, 1}, {2, 1}, {4, 3}, {1, 1}, {2, 3}, {1, 2}};
static const int BURST_COUNT_LUT[7] = {2, 3, 4, 5, 6, 7, 8};
static const int BURST_SPACING_LUT[5] = {2, 3, 4, 5, 6};
static const int CENTRALITY_MAX = 30;
enum { HOLD_FIT = 0, HOLD_OVER = 1, ROLL_FIT = 2, ROLL_OVER = 3 };
inline bool burstIsRoll(int m) { return m == ROLL_FIT || m == ROLL_OVER; }
inline bool burstIsFit(int m) { return m == HOLD_FIT || m == ROLL_FIT; }

// "how tonal" a scale degree is: root strongest, then fifth, thirds, quarters
inline int pitchCentrality(int degInOct, int N) {
    if (N <= 1) return 0;
    int halfWidth = std::max(1, N / 6);
    auto kernel = [&](int target, int weight) {
        int dist = std::abs(degInOct - target);
        int wrap = N - dist;
        if (wrap < dist) dist = wrap;
        int w = ((halfWidth - dist) * weight) / halfWidth;
        return w > 0 ? w : 0;
    };
    int b = kernel(0, CENTRALITY_MAX);
    if (N >= 2) b += kernel(N / 2, 20);
    if (N >= 3) b += kernel(N / 3, 10);
    if (N >= 3) b += kernel((2 * N) / 3, 10);
    if (N >= 4) b += kernel(N / 4, 5);
    if (N >= 4) b += kernel((3 * N) / 4, 5);
    return b;
}

// above 50 % Burst the hold mode is chosen per cell (deterministic per seed)
inline int burstHoldForCell(int burst, int configured, uint32_t seed, int cell) {
    if (burst <= 50) return configured;
    Random rng(cellSeed(seed, cell) ^ 0xB47570D1u);
    bool roll = rng.nextRange(2) != 0;
    bool fit = rng.nextRange(2) != 0;
    if (roll) return fit ? ROLL_FIT : ROLL_OVER;
    return fit ? HOLD_FIT : HOLD_OVER;
}

// libstdc++ std::sort, reproduced so equal-weight steps land in the same order as
// on the module on every platform (std::sort is not stable for more than 16 items)
template <typename T, typename L> void gccSort(std::vector<T> &a, L less) {
    auto swap = [&](int i, int j) { T t = a[i]; a[i] = a[j]; a[j] = t; };
    auto insertionSort = [&](int first, int last) {
        for (int i = first + 1; i < last; i++) {
            T val = a[i];
            if (less(val, a[first])) {
                for (int k = i; k > first; k--) a[k] = a[k - 1];
                a[first] = val;
            } else {
                int j = i;
                while (less(val, a[j - 1])) { a[j] = a[j - 1]; j--; }
                a[j] = val;
            }
        }
    };
    auto unguardedInsertion = [&](int first, int last) {
        for (int i = first; i < last; i++) {
            T val = a[i]; int j = i;
            while (less(val, a[j - 1])) { a[j] = a[j - 1]; j--; }
            a[j] = val;
        }
    };
    auto medianToFirst = [&](int r, int x, int y, int z) {
        if (less(a[x], a[y])) {
            if (less(a[y], a[z])) swap(r, y);
            else if (less(a[x], a[z])) swap(r, z);
            else swap(r, x);
        } else if (less(a[x], a[z])) swap(r, x);
        else if (less(a[y], a[z])) swap(r, z);
        else swap(r, y);
    };
    auto partition = [&](int first, int last, int pivot) {
        for (;;) {
            while (less(a[first], a[pivot])) first++;
            last--;
            while (less(a[pivot], a[last])) last--;
            if (!(first < last)) return first;
            swap(first, last);
            first++;
        }
    };
    std::function<void(int, int, int)> introLoop = [&](int first, int last, int depth) {
        while (last - first > 16) {
            if (depth == 0) {
                std::stable_sort(a.begin() + first, a.begin() + last, less);
                return;
            }
            depth--;
            int mid = first + ((last - first) >> 1);
            medianToFirst(first, first + 1, mid, last - 1);
            int cut = partition(first + 1, last, first);
            introLoop(cut, last, depth);
            last = cut;
        }
    };
    int n = (int)a.size();
    if (n < 2) return;
    introLoop(0, n, 2 * (int)std::floor(std::log2((double)n)));
    if (n > 16) { insertionSort(0, 16); unguardedInsertion(16, n); }
    else insertionSort(0, n);
}

struct StepContent {
    int durationIndex = 0, densityRank = 0, burstTails = 0, burstRate = 0;
    bool rest = false, legato = false, slide = false, rhythmValid = false;
    int degree = 0, octave = 0;
    bool melodyValid = false;
    void clearRhythm() { durationIndex = densityRank = burstTails = burstRate = 0; rest = legato = slide = rhythmValid = false; }
    void mergeRhythmFrom(const StepContent &e) {
        durationIndex = e.durationIndex; densityRank = e.densityRank; burstTails = e.burstTails; burstRate = e.burstRate;
        rest = e.rest; legato = e.legato; slide = e.slide; rhythmValid = e.rhythmValid;
    }
    void mergeMelodyFrom(const StepContent &e) { degree = e.degree; octave = e.octave; melodyValid = e.melodyValid; }
    void setDegree(int v) { degree = v & 0x7f; }
    void setOctave(int v) { octave = v & 0x1f; }
};

struct Sequence {
    int rhythmMode = 1, melodyMode = 1;     // 0 = Loop, 1 = Live
    int complexity = 50, contour = 0, repeatProb = 0, noteDuration = 5;
    int variation = 16, rest = 0, slide = 0, burstRate = 50, burstCount = 0;
    int burstHold = HOLD_OVER, sleep = 0, mutate = 0, jump = 0, range = 50;
    int degreeRotation = 0, maskRotation = 0, patienceMelody = 100, patienceRhythm = 100;
    int legatoProb = 0, marblesSpread = 50, marblesBias = 50, maskMelody = 100;
    int tiltMelody = 0, maskRhythm = 100, gateLength = 0, tiltRhythm = 50;
    int burst = 0, feel = 50, rotate = 0, size = 32, first = 0;
    int divisor = 12, clockMult = 100, scale = 1, rootNote = 0, scaleRotate = 0;
    int octave = 0, transpose = 0, cvUpdate = 0;
    int evolve = 0;   // 0 = every reset starts from the saved pattern
    int degreeTickets[USER_SCALE_SIZE] = {};
    int durationTickets[8] = {};
    StepContent steps[STEP_COUNT];
    bool rhythmValid = false, melodyValid = false;
    uint32_t rhythmSeed = 0, melodySeed = 0;
};

inline int pickDuration(const Sequence &seq, Random &rng) {
    const int entries = 8;
    int center = iclamp(seq.noteDuration, 0, 7);
    int V = iclamp(seq.variation, 0, 100);
    int mirror = 7 - center;
    int homeAmount = std::max(0, 100 - 2 * V);
    int absVMid = V > 50 ? V - 50 : 50 - V;
    int ticketAmount = std::max(0, 100 - 2 * absVMid);
    int exploreAmount = std::max(0, 2 * V - 100);
    int weights[8]; int total = 0;
    for (int i = 0; i < entries; i++) {
        int t = seq.durationTickets[i];
        if (t < 0) { weights[i] = 0; continue; }
        int dh = std::abs(i - center);
        int homeBell = (1 > dh ? 1 - dh : 0) * 10;
        int ticketW = t > 0 ? t : 1;
        int dm = std::abs(i - mirror);
        int mirrorBell = (2 > dm ? 2 - dm : 0) * 10;
        int exploreW = 5 + mirrorBell;
        int w = homeBell * homeAmount + ticketW * ticketAmount + exploreW * exploreAmount;
        weights[i] = w; total += w;
    }
    if (total <= 0) return center;
    uint32_t roll = rng.nextRange(total), sum = 0;
    for (int i = 0; i < entries; i++) { sum += weights[i]; if (roll < sum) return i; }
    return center;
}

// mask ranks: order steps long-to-short (plus a little seeded jitter)
inline void generateMaskRanks(Sequence &seq, int size, uint32_t seed) {
    Random rng(seed);
    if (size <= 0) return;
    size = std::min(size, STEP_COUNT);
    struct Item { int index; float weight; };
    std::vector<Item> items;
    for (int k = 0; k < size; k++) {
        int durSlot = iclamp(seq.steps[k].durationIndex, 0, 7);
        float axis = (float(durSlot) - 3.5f) / 3.5f;
        float jitter = (float(rng.nextRange(1000)) / 1000.f) * 0.2f;
        items.push_back({k, axis + jitter});
    }
    gccSort(items, [](const Item &a, const Item &b) { return a.weight < b.weight; });
    for (int k = 0; k < size; k++) seq.steps[items[k].index].densityRank = k & 0x3f;
}

// triangular weighted pick: knob 0 = first entry, 100 = last entry
template <int N> inline int pickFromLutTriangular(const int (&lut)[N], int knob, Random &rng) {
    int weights[N]; int total = 0;
    for (int i = 0; i < N; i++) {
        int center = N > 1 ? (i * 100) / (N - 1) : 50;
        int w = 50 - std::abs(knob - center);
        if (w < 0) w = 0;
        weights[i] = w; total += w;
    }
    if (total <= 0) return lut[rng.nextRange(N)];
    uint32_t roll = rng.nextRange(total), sum = 0;
    for (int i = 0; i < N; i++) { sum += weights[i]; if (roll < sum) return lut[i]; }
    return lut[N - 1];
}
inline int spacingIndex(int v) { for (int i = 0; i < 5; i++) if (BURST_SPACING_LUT[i] == v) return i; return -1; }
inline int pickBurstSpacingSlot(int knob, Random &rng) { return spacingIndex(pickFromLutTriangular(BURST_SPACING_LUT, knob, rng)); }

inline StepContent generateRhythmEvent(const Sequence &seq, Random &rng) {
    StepContent ev;
    ev.durationIndex = pickDuration(seq, rng);
    ev.densityRank = 0;
    ev.rest = (int)rng.nextRange(100) < seq.rest;
    ev.legato = false; ev.slide = false;
    ev.rhythmValid = true;
    ev.burstTails = 0; ev.burstRate = 0;
    if ((int)rng.nextRange(100) < seq.burst) {
        int total = pickFromLutTriangular(BURST_COUNT_LUT, seq.burstCount, rng);
        ev.burstTails = std::max(1, total - 1) & 7;
        int slot = spacingIndex(pickFromLutTriangular(BURST_SPACING_LUT, seq.burstRate, rng));
        ev.burstRate = slot < 0 ? 0 : slot;
    }
    return ev;
}

// effective ticket after Degree Rotation and Mask Rotation
inline int effectiveDegreeTicket(const Sequence &seq, int degree, int activeNotes) {
    int dRot = imod(seq.degreeRotation % activeNotes, activeNotes);
    int srcIdx = (degree - dRot + activeNotes * 2) % activeNotes;
    if (seq.degreeTickets[srcIdx] < 0) return -1;
    std::vector<int> nonNeg, eff(activeNotes);
    for (int i = 0; i < activeNotes; i++) {
        int idx = (i - dRot + activeNotes * 2) % activeNotes;
        eff[i] = seq.degreeTickets[idx];
        if (eff[i] >= 0) nonNeg.push_back(eff[i]);
    }
    int mRot = seq.maskRotation, cnt = (int)nonNeg.size();
    if (cnt > 1 && mRot != 0) {
        int rotSteps = imod(mRot % cnt, cnt), pos = 0;
        for (int i = 0; i < activeNotes; i++) {
            if (eff[i] >= 0) { eff[i] = nonNeg[(pos - rotSteps + cnt) % cnt]; pos++; }
        }
    }
    return eff[degree];
}

inline int recencyPenaltyMilli(int run) {
    if (run <= 0) return 1000;
    if (run == 1) return 550;
    if (run == 2) return 200;
    return 50;
}

// Bias + Spread shaping of a 0..1000 roll (Marbles-style)
inline int betaShapeMilli(int u, int biasKnob, int spreadKnob) {
    int bias = iclamp(biasKnob, 0, 100) * 10;
    int spread = iclamp(spreadKnob, 0, 100) * 10;
    if (u > 1000) u = 1000;
    if (spread < 500) {
        int pull = ((500 - spread) * 1000) / 500;
        return (bias * pull + u * (1000 - pull)) / 1000;
    }
    int bernPull = ((spread - 500) * 1000) / 500;
    int edge = u < bias ? 0 : 1000;
    return (u * (1000 - bernPull) + edge * bernPull) / 1000;
}

struct PitchState { int lastDegree = -1, lastClass = -1, classRunLength = 0; };

// The five-step pitch law: class roll with tickets + recency, repeat check
// (Complexity), octave candidates (Range), region roll (Bias/Spread), direction
// nudge (Contour). Returns an absolute scale degree.
inline int generateDegree(const Sequence &seq, int notesPerOctave, PitchState &state, Random &rng) {
    int N = iclamp(notesPerOctave, 1, USER_SCALE_SIZE);
    int range = iclamp(seq.range, 0, 100);
    int complexity = iclamp(seq.complexity, 0, 100);
    int contour = iclamp(seq.contour, -100, 100);
    std::vector<int> allowed, tickets;
    for (int c = 0; c < N; c++) {
        int t = effectiveDegreeTicket(seq, c, N);
        if (t >= 0) { allowed.push_back(c); tickets.push_back(t); }
    }
    if (allowed.empty()) return 0;

    int chosenClass = -1;
    for (int retry = 0; retry < 4; retry++) {
        std::vector<long> weights; long total = 0;
        for (size_t i = 0; i < allowed.size(); i++) {
            long base = tickets[i] > 0 ? tickets[i] : 1;
            long penalty = allowed[i] == state.lastClass ? recencyPenaltyMilli(state.classRunLength) : 1000;
            weights.push_back(base * penalty); total += base * penalty;
        }
        size_t picked = 0;
        if (total > 0) {
            uint32_t roll = rng.nextRange((uint32_t)total); long sum = 0;
            for (size_t i = 0; i < allowed.size(); i++) { sum += weights[i]; if ((long)roll < sum) { picked = i; break; } }
        }
        chosenClass = allowed[picked];
        if (chosenClass == state.lastClass && state.lastClass >= 0) {
            if ((int)rng.nextRange(100) < complexity) continue;
        }
        break;
    }
    if (chosenClass < 0) chosenClass = allowed[0];

    int currentOctave = state.lastDegree >= 0 ? state.lastDegree / N : 0;
    std::vector<int> cand;
    if (range > 50) {
        int width = iclamp(1 + ((range - 50) * 3) / 50, 1, 4);
        for (int k = 0; k < width; k++) cand.push_back(chosenClass + k * N);
    } else if (range < 50) {
        cand.push_back(chosenClass + currentOctave * N);
        if ((int)rng.nextRange(100) < (50 - range) * 2) {
            if (currentOctave - 1 >= 0) cand.push_back(chosenClass + (currentOctave - 1) * N);
            cand.push_back(chosenClass + (currentOctave + 1) * N);
        }
    } else {
        cand.push_back(chosenClass + currentOctave * N);
    }
    std::sort(cand.begin(), cand.end());

    int chosen = cand[0];
    if (cand.size() > 1) {
        int u = rng.nextRange(1001);
        int shaped = betaShapeMilli(u, seq.marblesBias, seq.marblesSpread);
        int ix = iclamp((shaped * (int(cand.size()) - 1) + 500) / 1000, 0, int(cand.size()) - 1);
        chosen = cand[ix];
    }
    if (contour != 0 && state.lastDegree >= 0 && cand.size() > 1) {
        int want = contour > 0 ? 1 : -1;
        int got = chosen > state.lastDegree ? 1 : (chosen < state.lastDegree ? -1 : 0);
        if (got != want && (int)rng.nextRange(100) < std::abs(contour)) {
            int best = -1, bestDist = 0;
            for (size_t i = 0; i < cand.size(); i++) {
                int d = cand[i] - state.lastDegree;
                int s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                if (s == want && (best < 0 || std::abs(d) < bestDist)) { best = (int)i; bestDist = std::abs(d); }
            }
            if (best >= 0) chosen = cand[best];
        }
    }
    int cls = chosen % N;
    if (cls == state.lastClass) state.classRunLength++;
    else { state.lastClass = cls; state.classRunLength = 0; }
    state.lastDegree = chosen;
    return chosen;
}

inline StepContent generateMelodyEvent(const Sequence &seq, int N, PitchState &state, Random &rng) {
    StepContent ev;
    int abs = generateDegree(seq, N, state, rng);
    ev.setDegree(abs % N);
    ev.setOctave(abs / N);
    ev.melodyValid = true;
    return ev;
}

inline void generateRhythm(Sequence &seq, uint32_t seed) {
    Random rng(seed);
    for (int i = 0; i < STEP_COUNT; i++) {
        if (i < seq.size) seq.steps[i].mergeRhythmFrom(generateRhythmEvent(seq, rng));
        else seq.steps[i].clearRhythm();
    }
    generateMaskRanks(seq, seq.size, seed ^ 0xdeadbeefu);
    seq.rhythmValid = true;
    seq.rhythmSeed = seed;
}

inline void generateMelody(Sequence &seq, int N, uint32_t seed) {
    Random rng(seed);
    PitchState state;
    for (int i = 0; i < STEP_COUNT; i++) {
        if (i < seq.size) seq.steps[i].mergeMelodyFrom(generateMelodyEvent(seq, N, state, rng));
        else seq.steps[i].melodyValid = false;
    }
    seq.melodyValid = true;
    seq.melodySeed = seed;
}

inline void mutateRhythmOne(Sequence &seq, Random &rng) {
    int size = seq.size;
    if (size <= 0) return;
    int first = iclamp(seq.first, 0, size - 1), last = iclamp(size - 1, first, size - 1);
    int i = first + rng.nextRange(last - first + 1);
    seq.steps[i].mergeRhythmFrom(generateRhythmEvent(seq, rng));
    generateMaskRanks(seq, size, seq.rhythmSeed ^ 0xdeadbeefu);
}

inline void mutateMelodyOne(Sequence &seq, int N, Random &rng) {
    int size = seq.size;
    if (size <= 0) return;
    int first = iclamp(seq.first, 0, size - 1), last = iclamp(size - 1, first, size - 1);
    int i = first + rng.nextRange(last - first + 1);
    PitchState ps;
    int abs = generateDegree(seq, N, ps, rng);
    int n = iclamp(N, 1, USER_SCALE_SIZE);
    StepContent m;
    m.setDegree(abs % n); m.setOctave(abs / n); m.melodyValid = true;
    seq.steps[i].mergeMelodyFrom(m);
}

struct BurstNote { int tickOffset, gateTicks, note; bool roll; };
// Repeat-path bursts: child notes inside the replayed event
inline std::vector<BurstNote> evaluateBurst(const Sequence &seq, const StepContent &ev, int N, int anchorNote, int durationTicks,
                                            Random &rng, uint32_t holdSeed, int holdCell) {
    std::vector<BurstNote> out;
    int count = ev.burstTails;
    bool roll = burstIsRoll(burstHoldForCell(seq.burst, seq.burstHold, holdSeed, holdCell));
    if (durationTicks < MIN_BURST_PARENT) count = 0;
    int slot = iclamp(ev.burstRate, 0, 4);
    float spacing = float(durationTicks) / float(BURST_SPACING_LUT[slot]);
    spacing = std::max(float(MIN_AUDIBLE_GATE + 1), spacing);
    for (int i = 0; i < MAX_BURST && i < count; i++) {
        int offset = (int)std::floor(float((i + 1) * spacing));
        int gate = std::max(MIN_AUDIBLE_GATE, (int)std::floor(float(spacing * 0.5f)));
        if (offset + gate >= durationTicks) {
            if (offset + MIN_AUDIBLE_GATE < durationTicks) {
                gate = durationTicks - offset - 1;
                if (gate < MIN_AUDIBLE_GATE) break;
            } else break;
        }
        PitchState ps;
        int note = roll ? generateDegree(seq, N, ps, rng) : anchorNote;
        out.push_back({offset, gate, note, roll});
    }
    return out;
}

inline int computeFeelScaleQ16(int feel, int naturalSum, int beatTicks) {
    const int ONE = 0x10000;
    if (feel >= 45 && feel <= 55) return ONE;
    if (naturalSum == 0 || beatTicks == 0) return ONE;
    float targetBeats = feel < 45 ? 4.f - float(45 - feel) / 45.f : 4.f + float(feel - 55) / 45.f;
    float targetTicks = targetBeats * float(beatTicks);
    float scale = targetTicks / float(naturalSum);
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 4.f) scale = 4.f;
    return (int)std::floor(float(float(scale * float(ONE)) + 0.5f));
}
inline int applyFeelScale(int ticks, int q16) { return (int)std::floor((double)ticks * q16 / 65536.0); }

struct Cell { int dur = 0, degree = 0, octave = 0, gateLen = 0; bool audible = true, slide = false, legato = false; };
struct Cache { int count = 0, cycleTicks = 0; Cell cells[STEP_COUNT]; };

inline int rebuildStepCache(Cache &cache, const Sequence &seq, int divisor, uint32_t seed, int N, bool capturedRhythm, bool capturedMelody) {
    cache.count = 0; cache.cycleTicks = 0;
    int size = std::max(0, std::min(seq.size, STEP_COUNT));
    if (size <= 0) return 0;
    auto lutTicks = [&](int idx) {
        const int *f = DURATION_LUT[iclamp(idx, 0, 7)];
        int t = (int)std::floor((double)divisor * f[0] / f[1]);
        return t > 0 ? t : 1;
    };
    int cycleTicks = 0, prevDur = 0, clusterRemaining = 0, clusterIdx = 0;
    int clusterDurs[MAX_BURST + 1] = {};
    auto curveFromKnob = [](int knob) {
        float k = float(std::max(0, std::min(100, knob)));
        float expo = (0.9163f * (k - 50.f)) / 50.f;
        return float(std::exp((double)expo));
    };
    auto buildFitCluster = [&](int total, int cells, float r) {
        if (cells <= 0) cells = 1;
        if (cells > MAX_BURST + 1) cells = MAX_BURST + 1;
        int budget = std::max(0, total - MIN_CLUSTER_CELL * cells);
        float w[MAX_BURST + 1], sum = 0, rk = 1;
        for (int k = 0; k < cells; k++) { w[k] = rk; sum = sum + rk; rk = rk * r; }
        if (sum <= 0) sum = 1;
        for (int k = 0; k < cells; k++) clusterDurs[k] = MIN_CLUSTER_CELL + (int)std::trunc(float(float(budget) * w[k]) / sum);
    };
    bool pickMelodyInCache = seq.melodyMode == 0 && !capturedMelody;
    bool pickRhythmInCache = seq.rhythmMode == 0 && !capturedRhythm;
    PitchState pitchState;
    int anchorDegree = 0, anchorOctave = 0;
    bool anchorLegato = false, anchorSlide = false, anchorAudible = true;

    for (int i = 0; i < size; i++) {
        const StepContent &ev = seq.steps[i];
        Random stepRng(cellSeed(seed, i));
        int cellDur;
        bool isTail = false;
        bool burstFit = burstIsFit(burstHoldForCell(seq.burst, seq.burstHold, seed, i));

        if (clusterRemaining > 0) {
            cellDur = clusterDurs[std::min(clusterIdx, MAX_BURST - 1)];
            clusterIdx++; clusterRemaining--; isTail = true;
        } else if (pickRhythmInCache) {
            cellDur = lutTicks(pickDuration(seq, stepRng));
            if (i == 0) prevDur = cellDur;
            if ((int)stepRng.nextRange(100) < seq.burst) {
                int total = iclamp(pickFromLutTriangular(BURST_COUNT_LUT, seq.burstCount, stepRng), 2, MAX_BURST + 1);
                if (burstFit) {
                    int maxFit = prevDur / MIN_CLUSTER_CELL;
                    if (maxFit >= 2) {
                        if (total > maxFit) total = maxFit;
                        buildFitCluster(prevDur, total, curveFromKnob(seq.burstRate));
                        cellDur = clusterDurs[0]; clusterIdx = 1; clusterRemaining = total - 1;
                    }
                } else {
                    int slot = pickBurstSpacingSlot(seq.burstRate, stepRng);
                    int cand = prevDur / BURST_SPACING_LUT[slot];
                    if (cand >= MIN_CLUSTER_CELL) {
                        for (int k = 0; k < total; k++) clusterDurs[k] = cand;
                        clusterIdx = 1; clusterRemaining = total - 1; cellDur = cand;
                    }
                }
            }
        } else {
            cellDur = lutTicks(ev.durationIndex);
            if (i == 0) prevDur = cellDur;
            if (ev.burstTails > 0) {
                int tails = iclamp(ev.burstTails, 1, MAX_BURST);
                int total = tails + 1;
                if (burstFit) {
                    int maxFit = prevDur / MIN_CLUSTER_CELL;
                    if (maxFit >= 2) {
                        if (total > maxFit) total = maxFit;
                        buildFitCluster(prevDur, total, curveFromKnob(seq.burstRate));
                        cellDur = clusterDurs[0]; clusterIdx = 1; clusterRemaining = total - 1;
                    }
                } else {
                    int slot = iclamp(ev.burstRate, 0, 4);
                    int cand = prevDur / BURST_SPACING_LUT[slot];
                    if (cand >= MIN_CLUSTER_CELL) {
                        for (int k = 0; k < total; k++) clusterDurs[k] = cand;
                        clusterIdx = 1; clusterRemaining = tails; cellDur = cand;
                    }
                }
            }
        }

        int slotDegree = 0, slotOctave = 0, notes = std::max(1, N), degAbs;
        if (pickMelodyInCache) {
            Random r(cellSeed(seq.melodySeed, i));
            degAbs = generateDegree(seq, N, pitchState, r);
            slotDegree = (degAbs % notes) & 0x7f;
            slotOctave = std::min(15, degAbs / notes) & 0xf;
        }
        int cellDegree, cellOctave;
        if (isTail) {
            if (burstIsRoll(burstHoldForCell(seq.burst, seq.burstHold, seq.melodySeed, i))) {
                Random tailRng(cellSeed(seq.melodySeed, i) ^ 0x7A11C0DEu);
                PitchState ps;
                degAbs = generateDegree(seq, N, ps, tailRng);
                cellDegree = (degAbs % notes) & 0x7f;
                cellOctave = std::min(15, degAbs / notes) & 0xf;
            } else { cellDegree = anchorDegree; cellOctave = anchorOctave; }
        } else if (pickMelodyInCache) {
            cellDegree = anchorDegree = slotDegree;
            cellOctave = anchorOctave = slotOctave;
        } else {
            cellDegree = anchorDegree = ev.degree & 0x7f;
            cellOctave = anchorOctave = std::min(15, ev.octave) & 0xf;
        }

        // gate length as 64ths of the cell, triangular around 50 %
        int pct = 50, spread = iclamp(seq.gateLength, 0, 100);
        if (spread > 0) {
            int tri = int(stepRng.nextRange(101) + stepRng.nextRange(101)) / 2;
            pct = 50 + ((tri - 50) * spread) / 100;
        }
        pct = iclamp(pct, 10, 100);
        int gateFrac = std::min(63, (pct * 64) / 100);

        bool cellLegato, cellSlide;
        if (isTail) { cellLegato = anchorLegato; cellSlide = anchorSlide; }
        else {
            int leg = iclamp(seq.legatoProb, 0, 100);
            cellLegato = leg > 0 && (int)stepRng.nextRange(100) < leg;
            int sl = iclamp(seq.slide, 0, 100);
            Random slideRng(cellSeed(seq.melodySeed, i) ^ 0x511DE51Du);
            cellSlide = sl > 0 && (int)slideRng.nextRange(100) < sl;
            anchorLegato = cellLegato; anchorSlide = cellSlide;
        }

        bool audible;
        if (isTail) audible = anchorAudible;
        else if (pickRhythmInCache) {
            int restKnob = iclamp(seq.rest, 0, 100);
            Random restRng(cellSeed(seq.rhythmSeed, i) ^ 0x5E57DEADu);
            audible = !(restKnob > 0 && (int)restRng.nextRange(100) < restKnob);
            anchorAudible = audible;
        } else { audible = !ev.rest; anchorAudible = audible; }

        if (cellDur > MAX_CELL_DURATION) cellDur = MAX_CELL_DURATION;
        Cell &c = cache.cells[i];
        c.dur = cellDur & 0xfff; c.degree = cellDegree; c.octave = cellOctave;
        c.gateLen = gateFrac; c.audible = audible; c.slide = cellSlide; c.legato = cellLegato;
        cache.count++;
        cycleTicks += cellDur;
        prevDur = cellDur;
    }
    cache.cycleTicks = cycleTicks;
    return cache.count;
}

inline float patienceLambda(int p) { return float(std::pow(2.0, (double)(float(float(iclamp(p, 0, 100)) * 7.f) / 99.f))); }
inline float poissonCdf(int k, float lambda) {
    if (k < 0) return 0;
    if (lambda <= 0) return 1;
    if (k > 200) k = 200;
    float term = float(std::exp(-(double)lambda)), sum = term;
    for (int i = 1; i <= k; i++) { term = term * (lambda / float(i)); sum = sum + term; }
    return sum > 1 ? 1 : sum;
}
inline float patienceProbability(int loops, int p) { return p >= 100 ? 0.f : poissonCdf(loops, patienceLambda(p)); }

inline int pickGateLength(int durationTicks, int spread, Random &rng) {
    int pct = 50;
    if (spread > 0) {
        int tri = int(rng.nextRange(101) + rng.nextRange(101)) / 2;
        pct = 50 + ((tri - 50) * spread) / 100;
    }
    pct = iclamp(pct, 10, 100);
    int t = (durationTicks * pct) / 100;
    if (t < MIN_AUDIBLE_GATE) t = MIN_AUDIBLE_GATE;
    if (t > durationTicks) t = durationTicks;
    return t;
}
} // namespace st

struct StochasticEngine {
    st::Sequence p;
    bool quirks = false;
    int measureTicks = 768;

    bool gateOutput = false;
    float cvOutput = 0, cvTarget = 0;
    bool slideActive = false;
    int displayStep = -1;
    bool stepFired = false;

    st::Cache cache;
    bool capturedR = false, capturedM = false;
    bool hasUndoR = false, hasUndoM = false;
    uint32_t undoR = 0, undoM = 0;
    std::vector<int> shapingSnapshot;
    // the pattern a fresh reset starts from
    bool hasSaved = false;
    struct Saved { st::StepContent steps[st::STEP_COUNT]; bool rhythmValid, melodyValid; uint32_t rhythmSeed, melodySeed; bool capturedR, capturedM; } saved;

    int currentStep = 0, sleepRemaining = 0, loopCycleCount = 0, loopCycleCountMelody = 0, lastAppliedSize = 0;
    int lastDegree = -1;
    st::PitchState pitchState;
    int jumpRegister = 0;
    int eventElapsed = 0, eventDuration = 0;
    st::StepContent lastStepContent;
    bool lastStepContentValid = false;
    int lastStepIndex = 0;
    struct GateEv { uint32_t tick; bool gate; };
    struct CvEv { uint32_t tick; float cv; bool slide; };
    SortedQueue<GateEv> gateQueue{16};
    SortedQueue<CvEv> cvQueue{16};
    Random rng;
    bool refreshPending = false;

    StochasticEngine() { reset(); }

    ScaleView scaleView() const { return ScaleView(p.scale, p.scaleRotate); }
    int N() const { return scaleView().notesPerOctave(); }
    int last() const { return p.size - 1; }
    int effectiveDivisor() const {
        float cm = float(p.clockMult) * 0.01f;
        return std::max(1, jsround(float(float(p.divisor * 4) / cm)));
    }

    bool setParam(const std::string &name, double value) {
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"rhythmMode", &p.rhythmMode, 0, 1}, {"melodyMode", &p.melodyMode, 0, 1}, {"complexity", &p.complexity, 0, 100},
            {"contour", &p.contour, -100, 100}, {"repeatProb", &p.repeatProb, 0, 100}, {"noteDuration", &p.noteDuration, 0, 7},
            {"variation", &p.variation, 0, 100}, {"rest", &p.rest, 0, 100}, {"slide", &p.slide, 0, 100},
            {"burstRate", &p.burstRate, 0, 100}, {"burstCount", &p.burstCount, 0, 100}, {"burstHold", &p.burstHold, 0, 3},
            {"sleep", &p.sleep, 0, 100}, {"mutate", &p.mutate, 0, 100}, {"jump", &p.jump, 0, 100}, {"range", &p.range, 0, 100},
            {"degreeRotation", &p.degreeRotation, -32, 32}, {"maskRotation", &p.maskRotation, -32, 32},
            {"patienceMelody", &p.patienceMelody, 0, 100}, {"patienceRhythm", &p.patienceRhythm, 0, 100},
            {"legatoProb", &p.legatoProb, 0, 100}, {"marblesSpread", &p.marblesSpread, 0, 100}, {"marblesBias", &p.marblesBias, 0, 100},
            {"maskMelody", &p.maskMelody, 0, 100}, {"tiltMelody", &p.tiltMelody, 0, 100}, {"maskRhythm", &p.maskRhythm, 0, 100},
            {"gateLength", &p.gateLength, 0, 100}, {"tiltRhythm", &p.tiltRhythm, 0, 100}, {"burst", &p.burst, 0, 100},
            {"feel", &p.feel, 0, 100}, {"rotate", &p.rotate, -64, 64}, {"size", &p.size, 2, 64}, {"first", &p.first, 0, 63},
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1},
            {"rootNote", &p.rootNote, 0, 11}, {"scaleRotate", &p.scaleRotate, 0, 31}, {"octave", &p.octave, -10, 10},
            {"transpose", &p.transpose, -100, 100}, {"cvUpdate", &p.cvUpdate, 0, 1}, {"evolve", &p.evolve, 0, 1}};
        for (auto &r : ranges) {
            if (name != r.n) continue;
            int v = iclamp(jsround(value), r.lo, r.hi);
            if (name == "first") v = iclamp(v, 0, p.size - 1);
            *r.f = v;
            if (name == "size") p.first = iclamp(p.first, 0, p.size - 1);
            if (name == "size" || name == "first") syncWindowEdit();
            return true;
        }
        if (name.rfind("degreeTicket", 0) == 0) {
            p.degreeTickets[iclamp(std::stoi(name.substr(12)), 0, 31)] = iclamp(jsround(value), -1, 100);
            refreshStepCache(); return true;
        }
        if (name.rfind("durationTicket", 0) == 0) {
            p.durationTickets[iclamp(std::stoi(name.substr(14)), 0, 7)] = iclamp(jsround(value), -1, 100);
            refreshStepCache(); return true;
        }
        return false;
    }
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "degreeTicket" || t[0] == "durationTicket") return setParam(t[0] + t[1], std::stod(t[2]));
        return setParam(t[0], std::stod(t[1]));
    }
    void refInit(bool renew) { if (renew) refreshLoopSources(); }

    void refreshStepCache() {
        st::rebuildStepCache(cache, p, effectiveDivisor(), p.rhythmSeed, N(), capturedR, capturedM);
    }

    void reset() {
        if (hasSaved && !p.evolve) restoreSaved();
        currentStep = p.first;
        sleepRemaining = 0;
        loopCycleCount = 0; loopCycleCountMelody = 0;
        lastAppliedSize = 0;
        lastDegree = -1;
        pitchState = st::PitchState();
        jumpRegister = 0;
        eventElapsed = 0; eventDuration = 0;
        gateOutput = false; slideActive = false;
        cvOutput = 0; cvTarget = 0;
        lastStepContent = st::StepContent(); lastStepContentValid = false; lastStepIndex = 0;
        gateQueue.clear(); cvQueue.clear();
        rng = Random(0x12345678);
        refreshPending = false;
        refreshStepCache();
    }

    // every bar line the firmware re-selects the pattern: Repeat forgets the last
    // event and the cache is rebuilt (quirks: it also drops pending gate-offs)
    void barBoundary() {
        if (quirks) { gateQueue.clear(); cvQueue.clear(); }
        lastStepContentValid = false;
        refreshStepCache();
    }

    // reset every N bars (firmware resetMeasure): back to First, counts as a cycle end
    void measureReset() {
        bool preempted = currentStep != p.first;
        currentStep = p.first;
        eventElapsed = 0; eventDuration = 0;
        if (preempted) rollCycleEndHooks();
    }

    bool tick(uint32_t tick) {
        stepFired = false;
        int divisor = effectiveDivisor();
        eventElapsed++;
        if (eventDuration == 0 || eventElapsed >= eventDuration) {
            eventElapsed = 0;
            if (sleepRemaining > 0) sleepRemaining--;
            else triggerStep(tick, divisor);
        }
        bool updated = false;
        while (!gateQueue.empty() && tick >= gateQueue.front().tick) gateOutput = gateQueue.pop().gate;
        while (!cvQueue.empty() && tick >= cvQueue.front().tick) {
            CvEv ev = cvQueue.pop();
            cvTarget = ev.cv;
            slideActive = ev.slide;
            updated = true;
        }
        return updated;
    }
    void update() { cvOutput = cvTarget; }

    void ensureLoopSources() {
        bool regen = false;
        if (p.rhythmMode == 0 && !p.rhythmValid) { st::generateRhythm(p, rng.next()); regen = true; }
        if (p.melodyMode == 0 && !p.melodyValid) { st::generateMelody(p, N(), rng.next()); jumpRegister = 0; regen = true; }
        if (regen) refreshStepCache();
    }

    std::vector<int> shapingKnobs() const {
        return {p.noteDuration, p.variation, p.burst, p.gateLength, p.complexity, p.contour,
                p.marblesBias, p.marblesSpread, p.burstCount, p.burstRate, p.burstHold, p.range};
    }

    void triggerStep(uint32_t tick, int divisor) {
        using namespace st;
        std::vector<int> cur = shapingKnobs();
        if (cur != shapingSnapshot) { refreshPending = true; shapingSnapshot = cur; }
        if (refreshPending) { refreshStepCache(); refreshPending = false; }

        ScaleView scale = scaleView();
        int Nn = scale.notesPerOctave();
        ensureLoopSources();

        if (currentStep < p.first || currentStep > last()) {
            currentStep = p.first;
            gateQueue.clear(); cvQueue.clear();
            gateOutput = false;
        }
        int stepIndex = currentStep;
        if (p.rotate != 0) {
            int win = last() - p.first + 1;
            if (win > 0) {
                int off = (currentStep - p.first + p.rotate) % win;
                if (off < 0) off += win;
                stepIndex = p.first + off;
            }
        }
        displayStep = stepIndex;

        StepContent event = p.steps[stepIndex];
        StepContent evl;
        bool useRepeat = p.repeatProb > 0 && lastStepContentValid && (int)rng.nextRange(100) < p.repeatProb;
        if (useRepeat) {
            evl = lastStepContent;
        } else {
            if (p.rhythmMode == 1) {
                StepContent rh = generateRhythmEvent(p, rng);
                evl.mergeRhythmFrom(rh);
                p.steps[stepIndex].mergeRhythmFrom(rh); refreshPending = true;   // live shadow
            } else evl.mergeRhythmFrom(event);
            if (p.melodyMode == 1) {
                StepContent me = generateMelodyEvent(p, Nn, pitchState, rng);
                evl.mergeMelodyFrom(me);
                lastDegree = pitchState.lastDegree;
                p.steps[stepIndex].mergeMelodyFrom(me); refreshPending = true;
            } else {
                evl.mergeMelodyFrom(event);
                lastDegree = evl.degree + evl.octave * Nn;
            }
            lastStepContent = evl;
            lastStepContentValid = true;
            lastStepIndex = stepIndex;
        }

        int pDurIdx = evl.durationIndex, pDegree = evl.degree, pOctave = evl.octave;
        bool pRest = evl.rest, pSlide = evl.slide, pLegato = evl.legato;
        int cellDur = 0, gateFrac = 32;
        if (!useRepeat && stepIndex < cache.count) {
            const Cell &cell = cache.cells[stepIndex];
            cellDur = cell.dur ? cell.dur : 1;
            gateFrac = cell.gateLen;
            pDegree = cell.degree; pOctave = cell.octave;
            pRest = !cell.audible; pSlide = cell.slide; pLegato = cell.legato;
            lastStepContent.legato = pLegato;
            lastStepContent.slide = pSlide;
        }
        int mult;
        if (cellDur > 0) mult = cellDur;
        else {
            const int *fr = DURATION_LUT[iclamp(pDurIdx, 0, 7)];
            mult = std::max(1, (divisor * fr[0]) / fr[1]);
        }
        mult = std::max(1, applyFeelScale(mult, computeFeelScaleQ16(p.feel, cache.cycleTicks, CONFIG_PPQN)));
        int durationTicks = mult;
        eventDuration = durationTicks;

        int note = pDegree + (pOctave + jumpRegister + p.octave) * Nn + p.transpose;
        float finalCv = float(float(scale.noteToVolts(note)) + float(p.rootNote * float(1.0 / 12)));

        // Mask/Tilt Rhythm (Loop rhythm only): thin out long or short notes
        bool maskRPass = true;
        if (p.rhythmMode == 0 && p.maskRhythm < 100) {
            int psize = std::max(1, p.size), denom = psize > 1 ? psize - 1 : 1;
            int rank = useRepeat ? evl.densityRank : p.steps[stepIndex].densityRank;
            int rankMilli = std::min(1000, (rank * 1000) / denom);
            uint32_t salt = cellSeed(p.rhythmSeed, stepIndex);
            int saltMilli = int(((salt >> 24) * 1000) / 255);
            int tiltSigned = p.tiltRhythm - 50;
            int tiltMag = std::abs(tiltSigned) * 2;
            int eff = (tiltMag * rankMilli + (100 - tiltMag) * saltMilli) / 100;
            int maskMilli = p.maskRhythm * 10;
            maskRPass = tiltSigned <= 0 ? eff < maskMilli : (1000 - eff) < maskMilli;
        }
        // Mask/Tilt Melody (Loop melody only): keep tonal (or anti-tonal) degrees
        bool maskMPass = true;
        if (p.melodyMode == 0 && (p.maskMelody < 100 || p.tiltMelody > 0)) {
            int NN = Nn > 0 ? Nn : 1;
            int dio = pDegree % NN; if (dio < 0) dio += NN;
            int cen = pitchCentrality(dio, NN);
            int mT = (CENTRALITY_MAX * (100 - p.maskMelody)) / 100;
            int tT = (CENTRALITY_MAX * (100 - p.tiltMelody)) / 100;
            maskMPass = cen >= mT || (p.tiltMelody > 0 && (CENTRALITY_MAX - cen) >= tT);
        }
        bool isRest = pRest || !maskRPass || !maskMPass || !evl.rhythmValid || !evl.melodyValid;

        std::vector<BurstNote> bursts;
        if (useRepeat && !isRest) bursts = evaluateBurst(p, evl, Nn, note, durationTicks, rng, p.melodySeed, lastStepIndex);

        if (!isRest) {
            stepFired = true;
            cvQueue.push({tick, finalCv, pSlide});
            gateQueue.push({tick, true});
            if (!pLegato && bursts.empty()) {
                int gateLen;
                if (useRepeat) gateLen = pickGateLength(durationTicks, p.gateLength, rng);
                else gateLen = iclamp((durationTicks * gateFrac) / 64, MIN_AUDIBLE_GATE, durationTicks);
                if (gateLen > durationTicks) gateLen = durationTicks;
                gateQueue.push({tick + gateLen, false});
            }
            for (auto &b : bursts) {
                uint32_t childTick = tick + b.tickOffset;
                uint32_t lowTick = childTick > tick + 2 ? childTick - 2 : tick;
                int childNote = b.note;
                if (b.roll) childNote += (jumpRegister + p.octave) * Nn + p.transpose;
                float childCv = float(float(scale.noteToVolts(childNote)) + float(p.rootNote * float(1.0 / 12)));
                gateQueue.push({lowTick, false});
                cvQueue.push({childTick, childCv, pSlide});
                gateQueue.push({childTick, true});
                gateQueue.push({childTick + b.gateTicks, false});
            }
        } else {
            gateQueue.push({tick, false});
            if (p.cvUpdate == 1) cvQueue.push({tick, finalCv, false});
        }

        currentStep++;
        if (currentStep > last()) {
            currentStep = p.first;
            rollCycleEndHooks();
            if (p.size != lastAppliedSize) { refreshStepCache(); lastAppliedSize = p.size; }
        }
    }

    void rollPatience() {
        if (p.rhythmMode == 0 && p.patienceRhythm < 100) {
            loopCycleCount = (loopCycleCount + 1) & 0xffff;
            float pR = st::patienceProbability(loopCycleCount, p.patienceRhythm);
            if (rng.nextRange(10000) < uint32_t(std::trunc(float(pR * 10000.f)))) { p.rhythmValid = false; loopCycleCount = 0; }
        } else if (p.patienceRhythm >= 100) loopCycleCount = 0;
        if (p.melodyMode == 0 && p.patienceMelody < 100) {
            loopCycleCountMelody = (loopCycleCountMelody + 1) & 0xffff;
            float pM = st::patienceProbability(loopCycleCountMelody, p.patienceMelody);
            if (rng.nextRange(10000) < uint32_t(std::trunc(float(pM * 10000.f)))) { p.melodyValid = false; loopCycleCountMelody = 0; }
        } else if (p.patienceMelody >= 100) loopCycleCountMelody = 0;
    }

    // end of a pattern cycle: Jump (octave walk), Sleep, Patience, Mutate
    void rollCycleEndHooks() {
        if (p.jump > 0 && (int)rng.nextRange(100) < p.jump) {
            int dir = rng.nextRange(2) == 0 ? -1 : 1;
            if (jumpRegister + dir > 2) dir = -1;
            else if (jumpRegister + dir < -2) dir = 1;
            jumpRegister += dir;
        }
        if (p.sleep > 0) sleepRemaining = (p.sleep * 4 + 5) / 10;
        rollPatience();
        if (p.mutate > 0 && (int)rng.nextRange(100) < p.mutate) {
            bool rE = p.rhythmMode == 0 && p.rhythmValid, mE = p.melodyMode == 0 && p.melodyValid;
            if (rE && mE) { if (rng.nextRange(2) == 0) st::mutateRhythmOne(p, rng); else st::mutateMelodyOne(p, N(), rng); }
            else if (rE) st::mutateRhythmOne(p, rng);
            else if (mE) st::mutateMelodyOne(p, N(), rng);
            refreshStepCache();
        }
    }

    void syncWindowEdit() {
        int first = p.first, lst = std::max(first, last());
        if (currentStep < first || currentStep > lst) {
            currentStep = first;
            eventElapsed = eventDuration;
        }
        gateQueue.clear(); cvQueue.clear();
        gateOutput = false;
        refreshStepCache();
    }

    // ---- the buttons ----
    void refreshLoopSources() {
        st::generateRhythm(p, rng.next());
        st::generateMelody(p, N(), rng.next());
        loopCycleCount = 0; loopCycleCountMelody = 0; jumpRegister = 0;
        refreshStepCache();
        save();
    }
    // returns true when the mode switched Live -> Loop
    bool newRhythm() {
        bool switched = false;
        if (p.rhythmMode == 1) {          // Live: capture what just played
            p.rhythmValid = true; p.rhythmMode = 0; capturedR = true; loopCycleCount = 0; switched = true;
        } else {                          // Loop: brand new rhythm (undo-able)
            undoR = p.rhythmSeed; hasUndoR = true;
            st::generateRhythm(p, rng.next());
            capturedR = false; loopCycleCount = 0;
        }
        refreshStepCache();
        save();
        return switched;
    }
    bool newMelody() {
        bool switched = false;
        if (p.melodyMode == 1) {
            p.melodyValid = true; p.melodyMode = 0; capturedM = true;
            loopCycleCountMelody = 0; jumpRegister = 0; switched = true;
        } else {
            undoM = p.melodySeed; hasUndoM = true;
            st::generateMelody(p, N(), rng.next());
            capturedM = false; loopCycleCountMelody = 0; jumpRegister = 0;
        }
        refreshStepCache();
        save();
        return switched;
    }
    void undoRhythm() {
        if (!hasUndoR) return;
        st::generateRhythm(p, undoR); hasUndoR = false;
        loopCycleCount = 0; refreshStepCache(); save();
    }
    void undoMelody() {
        if (!hasUndoM) return;
        st::generateMelody(p, N(), undoM); hasUndoM = false;
        loopCycleCountMelody = 0; jumpRegister = 0; refreshStepCache(); save();
    }

    void save() {
        for (int i = 0; i < st::STEP_COUNT; i++) saved.steps[i] = p.steps[i];
        saved.rhythmValid = p.rhythmValid; saved.melodyValid = p.melodyValid;
        saved.rhythmSeed = p.rhythmSeed; saved.melodySeed = p.melodySeed;
        saved.capturedR = capturedR; saved.capturedM = capturedM;
        hasSaved = true;
    }
    void restoreSaved() {
        for (int i = 0; i < st::STEP_COUNT; i++) p.steps[i] = saved.steps[i];
        p.rhythmValid = saved.rhythmValid; p.melodyValid = saved.melodyValid;
        p.rhythmSeed = saved.rhythmSeed; p.melodySeed = saved.melodySeed;
        capturedR = saved.capturedR; capturedM = saved.capturedM;
    }
    void initFresh() { refreshLoopSources(); }
};

} // namespace xf
