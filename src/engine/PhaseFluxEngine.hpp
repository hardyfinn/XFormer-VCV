// PHASEFLUX engine (firmware engine/PhaseFluxTrackEngine.cpp, model/PhaseFluxMath.cpp,
// model/PhaseFluxSequence.h)
//
// 16 stages on a 4x4 grid, walked in one of 8 paths. Each stage lasts Length x Divisor
// and fires a burst of 0-16 pulses inside that time. The temporal curve decides when
// the pulses fall, the pitch curve which note each pulse plays. Warp and Response bend
// both curves, Repeat and Window fold them, Mask thins the pulses, and two
// accumulators let pitch and pulse count drift from one pass to the next.
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int PF_STAGES = 16;
static const int PF_MAX_PULSES = 16;
static const int PF_MIN_PULSE_GATE = 6;

// the 8 walks across the 4x4 grid: slot -> stage
static const int PF_TRAVERSALS[8][16] = {
    {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12},
    {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3},
    {12, 13, 14, 15, 11, 10, 9, 8, 4, 5, 6, 7, 3, 2, 1, 0},
    {15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0},
    {0, 4, 8, 12, 13, 9, 5, 1, 2, 6, 10, 14, 15, 11, 7, 3},
    {12, 13, 14, 15, 11, 7, 3, 2, 1, 0, 4, 8, 9, 10, 6, 5},
    {9, 5, 6, 10, 8, 4, 0, 1, 2, 3, 7, 11, 15, 14, 13, 12},
    {12, 8, 13, 4, 9, 14, 0, 5, 10, 15, 1, 6, 11, 2, 7, 3}};
static const char *const PF_TRAVERSAL_NAMES[] = {"Snake", "Rows", "SnakeUp", "Cols", "ColSnake", "Spiral", "Inward", "Diag"};
// Mask: a set bit mutes that pulse
static const int PF_MASKS[] = {0x00, 0xAA, 0x49, 0x11, 0xB6, 0x55, 0x77, 0x01};
static const char *const PF_MASK_NAMES[] = {"Off", "1in2", "1in3", "1in4", "2in3", "2in4", "3in4", "1in8"};
// Pitch Rate (Global pitch mode): how fast the pitch curve runs against the stages
static const int PF_RATES[17][2] = {{1, 7}, {1, 5}, {1, 3}, {2, 5}, {3, 5}, {5, 7}, {7, 9}, {11, 12}, {16, 17}, {1, 1},
    {18, 17}, {13, 12}, {11, 9}, {9, 7}, {7, 5}, {5, 3}, {9, 5}};
static const int PF_REPEAT[] = {1, 2, 3, 5};
static const char *const PF_TEMPORAL_CURVE_NAMES[] = {"Linear", "Bell", "Bounce"};
static const char *const PF_PITCH_CURVE_NAMES[] = {"Ramp", "Bell", "Triangle", "Bounce"};
static const char *const PF_RANGE_NAMES[] = {"1/2 oct", "1 oct", "2 oct", "3 oct"};
static const char *const PF_DIRECTION_NAMES[] = {"Up", "Down", "Around"};
static const char *const PF_WINDOW_NAMES[] = {"Off", "Focus 70", "Focus 50", "Polar 70", "Polar 50"};
static const char *const PF_REPEAT_NAMES[] = {"1x", "2x", "3x", "5x"};

enum PfField {
    PF_PULSES, PF_LENGTH, PF_SKIP, PF_GATE, PF_BASE_PITCH, PF_PITCH_RANGE, PF_PITCH_DIR, PF_PITCH_CURVE,
    PF_PITCH_FLIPV, PF_PITCH_FLIPH, PF_PITCH_WARP, PF_PITCH_RESP, PF_T_CURVE, PF_T_FLIPV, PF_T_FLIPH,
    PF_T_WARP, PF_T_RESP, PF_MASK_MELODY, PF_TILT_MELODY, PF_PHASE_SHIFT, PF_MASK, PF_MASK_SHIFT,
    PF_ACC_STEP, PF_PULSE_ACC_STEP, PF_ACC_TRIG, PF_PULSE_ACC_TRIG, PF_T_REPEAT, PF_P_REPEAT, PF_T_WINDOW, PF_P_WINDOW,
    PF_FIELD_COUNT
};
struct PfFieldDef { const char *key; const char *label; int min, max, def; };
static const PfFieldDef PF_FIELDS[PF_FIELD_COUNT] = {
    {"pulseCount", "Pulses", 0, 16, 0}, {"length", "Length", 1, 127, 4}, {"skip", "Skip", 0, 1, 0},
    {"gateLength", "Gate", 0, 100, 50}, {"basePitch", "Base Pitch", -64, 63, 0}, {"pitchRange", "P.Range", 0, 3, 1},
    {"pitchDirection", "P.Dir", 0, 2, 0}, {"pitchCurve", "P.Curve", 0, 3, 0}, {"pitchFlipV", "P.Flip V", 0, 1, 0},
    {"pitchFlipH", "P.Flip H", 0, 1, 0}, {"pitchWarp", "P.Warp", -64, 63, 0}, {"pitchResponse", "P.Resp", -64, 63, 0},
    {"temporalCurve", "T.Curve", 0, 2, 0}, {"temporalFlipV", "T.Flip V", 0, 1, 0}, {"temporalFlipH", "T.Flip H", 0, 1, 0},
    {"temporalWarp", "T.Warp", -64, 63, 0}, {"temporalResponse", "T.Resp", -64, 63, 0},
    {"maskMelody", "Mel.Mask", 0, 100, 100}, {"tiltMelody", "Mel.Tilt", 0, 100, 0}, {"phaseShift", "Phase", 0, 7, 0},
    {"mask", "Mask", 0, 7, 0}, {"maskShift", "Mask Shift", 0, 7, 0}, {"accumulatorStep", "Note Acc", -16, 15, 0},
    {"pulseAccumStep", "Pulse Acc", -8, 7, 0}, {"accumulatorTrigger", "N.Acc Trig", 0, 1, 0},
    {"pulseAccumTrigger", "P.Acc Trig", 0, 1, 0}, {"temporalRepeat", "T.Repeat", 0, 3, 0},
    {"pitchRepeat", "P.Repeat", 0, 3, 0}, {"temporalWindow", "T.Window", 0, 4, 0}, {"pitchWindow", "P.Window", 0, 4, 0}};

struct PfStage {
    int v[PF_FIELD_COUNT];
    PfStage() { clear(); }
    void clear() { for (int i = 0; i < PF_FIELD_COUNT; i++) v[i] = PF_FIELDS[i].def; }
    int &operator[](int f) { return v[f]; }
    int operator[](int f) const { return v[f]; }
};

// ---- the curve maths (float, like the module) ----
static const float PF_TWO_PI = 2 * 3.1415926536f;
inline float pfClamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float pfPowerBend(float z, int knob) {
    float p = float(knob / 64.0);
    float e = (1 - p) / (1 + p);
    return float(std::pow(double(z), double(e)));
}
inline float pfBend(float x, int knob) { return knob == 0 ? x : pfPowerBend(pfClamp01(x), knob); }
enum { PF_CURVE_RAMP, PF_CURVE_BELL, PF_CURVE_TRIANGLE, PF_CURVE_BOUNCE };
inline float pfCurve(int kind, float x) {
    switch (kind) {
    case PF_CURVE_BELL: return 0.5f - 0.5f * float(std::cos(double(x * PF_TWO_PI)));
    case PF_CURVE_TRIANGLE: return (x < 0.5f ? x : 1.f - x) * 2;
    case PF_CURVE_BOUNCE: {
        if (!(x < 1)) return 0;
        float y = float(std::fmod(double(x * 3), 1.0));
        return (1 - y) * (1 - y);
    }
    }
    return x;
}
static const int PF_TEMPORAL_KIND[] = {PF_CURVE_RAMP, PF_CURVE_BELL, PF_CURVE_BOUNCE};
static const float PF_WIN_LO[] = {0, 0.15f, 0.25f, 0.35f, 0.25f};
static const float PF_WIN_HI[] = {1, 0.85f, 0.75f, 0.65f, 0.75f};
// Window: 1-2 keep the middle (Focus 70/50), 3-4 keep the outsides (Polarize 70/50)
inline bool pfInWindow(float phi, int w) {
    if (w == 1 || w == 2) return phi >= PF_WIN_LO[w] && phi <= PF_WIN_HI[w];
    if (w == 3 || w == 4) return phi <= PF_WIN_LO[w] || phi >= PF_WIN_HI[w];
    return true;
}
inline float pfHoldWindow(float phi, int w) {
    if (w == 1 || w == 2) return phi < PF_WIN_LO[w] ? PF_WIN_LO[w] : (phi > PF_WIN_HI[w] ? PF_WIN_HI[w] : phi);
    if (w == 3 || w == 4) {
        if (phi > PF_WIN_LO[w] && phi < PF_WIN_HI[w]) return phi < 0.5f ? PF_WIN_LO[w] : PF_WIN_HI[w];
    }
    return phi;
}
// Warp -> Repeat -> Window (hold) -> FlipH -> curve -> Response -> FlipV
inline float pfPitchValue(float phi, int R, int warp, bool flipH, int curve, bool flipV, int resp, int win) {
    float w = pfBend(phi, warp);
    float r = R > 1 ? float(w * R) - std::floor(float(w * R)) : w;
    float h = pfHoldWindow(r, win);
    float c = pfCurve(curve, flipH ? 1 - h : h);
    float v = pfBend(c, resp);
    return flipV ? 1 - v : v;
}
// where pulse i of n sits: warp the even spread, then split it into R repeats
inline void pfTemporalAlloc(int i, int n, int R, int warp, int &slot, float &frac) {
    float seed = n > 0 ? float(double(i) / n) : 0.f;
    float scaled = pfBend(seed, warp) * float(R);
    int s = iclamp((int)std::floor(scaled), 0, R - 1);
    slot = s; frac = scaled - float(s);
}
// how "tonal" a scale degree is (root, fifth, thirds...), 0..30
inline int pfCentrality(int d, int N) {
    if (N <= 1) return 0;
    int hw = std::max(1, N / 6);
    auto k = [&](int target, int weight) {
        int dist = std::abs(d - target);
        if (N - dist < dist) dist = N - dist;
        int w = (hw - dist) * weight / hw;
        return w > 0 ? w : 0;
    };
    int b = k(0, 30);
    if (N >= 2) b += k(N / 2, 20);
    if (N >= 3) b += k(N / 3, 10) + k(2 * N / 3, 10);
    if (N >= 4) b += k(N / 4, 5) + k(3 * N / 4, 5);
    return b;
}

// accumulator configs: scope 0 Local (per stage) / 1 Track (shared),
// order 0 Wrap / 1 Pendulum / 2 Hold / 3 Return-to-zero, polarity 0 Uni / 1 Bi
struct PfAccCfg {
    int scope, order, polarity, reset, pos, neg;
    PfAccCfg(int s = 0, int o = 0, int p = 0, int r = 0, int ps = 28, int n = 28) : scope(s), order(o), polarity(p), reset(r), pos(ps), neg(n) {}
};
static const char *const PF_ACC_ORDER_NAMES[] = {"Wrap", "Pendulum", "Hold", "Return"};
inline void pfAdvance(int *c, int *dirs, int idx, const PfAccCfg &cfg, int step) {
    if (step == 0) return;
    int lo, hi;
    if (cfg.polarity == 0) { if (step > 0) { lo = 0; hi = cfg.pos; } else { lo = -cfg.neg; hi = 0; } }
    else { lo = -cfg.neg; hi = cfg.pos; }
    int d = step > 0 ? 1 : -1, mag = std::abs(step), v = c[idx];
    switch (cfg.order) {
    case 0: {
        v += d * mag;
        int range = hi - lo + 1;
        if (range > 0) v = lo + (((v - lo) % range) + range) % range;
        break;
    }
    case 1:
        v += step * dirs[idx];
        if (v >= hi) { v = hi; dirs[idx] = -dirs[idx]; }
        else if (v <= lo) { v = lo; dirs[idx] = -dirs[idx]; }
        break;
    case 2:
        v += d * mag;
        if (d > 0 && v >= hi) v = hi;
        if (d < 0 && v <= lo) v = lo;
        break;
    case 3:
        v += d * mag;
        if (v > hi || v < lo) v = 0;
        break;
    }
    c[idx] = v;
}

struct PhaseFluxParams {
    int divisor = 12, clockMult = 100, cycleLength = 0, traversal = 0, pitchMode = 0, pitchRate = 9;
    int globalPhase = 0, warpNudge = 0, responseNudge = 0, cyclePhaseWarp = 0, pulseNudge = 0, lenNudge = 0;
    int resetMeasure = 0, firstStage = 0, lastStage = 15, scale = 1, rootNote = 0, scaleRotate = 0;
    int octave = 0, transpose = 0;
    PfAccCfg noteAcc{0, 0, 0, 0, 28, 28}, pulseAcc{0, 0, 0, 0, 16, 16};
};

struct PhaseFluxEngine {
    PhaseFluxParams p;
    PfStage stages[PF_STAGES];
    int measureTicks = 768;
    bool quirks = false;

    // outputs
    bool gateOutput = false;
    float cvOutput = 0, cvOutputTarget = 0;
    bool retrigger = false;
    bool stepFired = false;       // a pulse started this tick
    bool stageTrigger = false;    // a new stage started this tick
    int activeCell = 0, slotIdx = 0;
    float stagePhase = 0, pitchPhase = 0;

    uint32_t resetTickOffset = 0;
    bool firstTick = true, layoutDirty = true;
    uint32_t pulseFired = 0;
    int prevSlotIdx = -1;
    bool gateState = false;
    int gateTimer = 0;
    struct Pulse { int at, gate; float cv; };
    std::vector<Pulse> schedule;
    int noteAccV[PF_STAGES], noteDir[PF_STAGES], pulseAccV[PF_STAGES], pulseDir[PF_STAGES];
    int cycleCount = 0;
    uint32_t prevCycleTick = 0;
    int cum[PF_STAGES + 1];
    int cycleTicks = 0;
    std::vector<int> layoutKey;
    bool prevGate = false, retrig = false;
    float prevCv = 0;

    PhaseFluxEngine() {
        for (int i = 0; i < PF_STAGES; i++) { if (i < 4) stages[i][PF_PULSES] = 4; else stages[i][PF_SKIP] = 1; }
        reset();
    }

    bool setParam(const std::string &name, double value) {
        int v = jsround(value);
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"cycleLength", &p.cycleLength, 0, 1},
            {"traversal", &p.traversal, 0, 7}, {"pitchMode", &p.pitchMode, 0, 1}, {"pitchRate", &p.pitchRate, 0, 16},
            {"globalPhase", &p.globalPhase, 0, 100}, {"warpNudge", &p.warpNudge, -64, 64},
            {"responseNudge", &p.responseNudge, -64, 64}, {"cyclePhaseWarp", &p.cyclePhaseWarp, -64, 64},
            {"pulseNudge", &p.pulseNudge, -15, 15}, {"lenNudge", &p.lenNudge, -64, 64}, {"resetMeasure", &p.resetMeasure, 0, 128},
            {"firstStage", &p.firstStage, 0, 15}, {"lastStage", &p.lastStage, 0, 15},
            {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1}, {"rootNote", &p.rootNote, 0, 11},
            {"scaleRotate", &p.scaleRotate, 0, 31}, {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -60, 60},
            {"noteAccScope", &p.noteAcc.scope, 0, 1}, {"noteAccOrder", &p.noteAcc.order, 0, 3},
            {"noteAccPolarity", &p.noteAcc.polarity, 0, 1}, {"noteAccReset", &p.noteAcc.reset, 0, 15},
            {"noteAccPos", &p.noteAcc.pos, 0, 255}, {"noteAccNeg", &p.noteAcc.neg, 0, 255},
            {"pulseAccScope", &p.pulseAcc.scope, 0, 1}, {"pulseAccOrder", &p.pulseAcc.order, 0, 3},
            {"pulseAccPolarity", &p.pulseAcc.polarity, 0, 1}, {"pulseAccReset", &p.pulseAcc.reset, 0, 15},
            {"pulseAccPos", &p.pulseAcc.pos, 0, 255}, {"pulseAccNeg", &p.pulseAcc.neg, 0, 255}};
        for (auto &r : ranges) {
            if (name != r.n) continue;
            v = iclamp(v, r.lo, r.hi);
            // the module keeps First <= Last
            if (name == "firstStage") v = std::min(v, p.lastStage);
            if (name == "lastStage") v = std::max(v, p.firstStage);
            *r.f = v;
            return true;
        }
        return false;
    }
    static int fieldIndex(const std::string &k) {
        for (int i = 0; i < PF_FIELD_COUNT; i++) if (k == PF_FIELDS[i].key) return i;
        return -1;
    }
    void setStage(int i, int f, int value) {
        if (f < 0 || f >= PF_FIELD_COUNT || i < 0 || i >= PF_STAGES) return;
        stages[i][f] = iclamp(value, PF_FIELDS[f].min, PF_FIELDS[f].max);
    }
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "stage") { setStage(std::stoi(t[1]), fieldIndex(t[2]), jsround(std::stod(t[3]))); return true; }
        return setParam(t[0], std::stod(t[1]));
    }

    void clearAcc(bool note, bool pulse) {
        for (int i = 0; i < PF_STAGES; i++) {
            if (note) { noteAccV[i] = 0; noteDir[i] = 1; }
            if (pulse) { pulseAccV[i] = 0; pulseDir[i] = 1; }
        }
    }

    void reset() {
        resetTickOffset = 0;
        firstTick = true;
        layoutDirty = true;
        pulseFired = 0;
        slotIdx = 0; prevSlotIdx = -1; activeCell = 0;
        stagePhase = 0; pitchPhase = 0;
        gateState = false; gateTimer = 0;
        cvOutput = 0; cvOutputTarget = 0;
        schedule.clear();
        clearAcc(true, true);
        cycleCount = 0; prevCycleTick = 0;
        for (int i = 0; i <= PF_STAGES; i++) cum[i] = 0;
        cycleTicks = 0;
        prevGate = false; prevCv = 0; retrig = false; gateOutput = false;
    }
    void barBoundary() {}

    std::vector<int> layoutSignature() const {
        std::vector<int> k = {p.divisor, p.clockMult, p.lenNudge, p.firstStage, p.lastStage, p.traversal, p.cycleLength};
        for (int i = 0; i < PF_STAGES; i++) { k.push_back(stages[i][PF_LENGTH]); k.push_back(stages[i][PF_SKIP]); }
        return k;
    }

    // the stages' start times in walk order (computeCumulativeTicks)
    void rebuildCumulativeTable() {
        const int *order = PF_TRAVERSALS[p.traversal];
        int unit = p.divisor * 4;
        bool fixed = p.cycleLength == 1;
        cum[0] = 0;
        for (int i = 0; i < PF_STAGES; i++) {
            int cell = order[i];
            const PfStage &s = stages[cell];
            int len = iclamp(s[PF_LENGTH] + p.lenNudge, 0, 127);
            if (cell < p.firstStage || cell > p.lastStage) len = 0;
            if (!fixed && s[PF_SKIP]) len = 0;
            cum[i + 1] = cum[i] + len * unit * 100 / p.clockMult;
        }
        int cycle = cum[PF_STAGES];
        if (cycle > 0) {
            // very short cycles are stretched to at least 1/32 bar
            int minCycle = measureTicks / 32;
            if (minCycle > 0 && cycle < minCycle) {
                for (int i = 1; i <= PF_STAGES; i++) cum[i] = (cum[i] * minCycle + cycle / 2) / cycle;
                cycle = cum[PF_STAGES];
            }
        }
        cycleTicks = cycle;
        layoutKey = layoutSignature();
        gateTimer = 0; gateState = false;
        pulseFired = 0; schedule.clear();
        prevSlotIdx = -1; prevCycleTick = 0;
        layoutDirty = false;
    }

    float degreeToVolts(const ScaleView &scale, int degree) const {
        return float(float(scale.noteToVolts(degree)) + float(p.rootNote) * (1.f / 12));
    }

    // the burst for the stage just entered (rebuildSchedule)
    void rebuildSchedule(int slotDur) {
        schedule.clear();
        if (slotDur <= 0) return;
        const PfStage &stage = stages[activeCell];
        if (stage[PF_SKIP]) return;

        const PfAccCfg &pulseCfg = p.pulseAcc, &noteCfg = p.noteAcc;
        int pIdx = pulseCfg.scope == 0 ? activeCell : 0;
        int nIdx = noteCfg.scope == 0 ? activeCell : 0;
        int pulseCount = iclamp(stage[PF_PULSES] + p.pulseNudge + pulseAccV[pIdx], 0, PF_MAX_PULSES);

        ScaleView scale(p.scale, p.scaleRotate);
        int N = scale.notesPerOctave();
        int tCurve = PF_TEMPORAL_KIND[stage[PF_T_CURVE]];
        int maskByte = PF_MASKS[stage[PF_MASK]];
        int tWarp = iclamp(stage[PF_T_WARP] + p.warpNudge, -64, 64);
        int tResp = iclamp(stage[PF_T_RESP] + p.responseNudge, -64, 64);

        bool global = p.pitchMode == 1;
        const PfStage &ps = global ? stages[0] : stage;
        int pCurve = ps[PF_PITCH_CURVE];
        int pWarp = iclamp(ps[PF_PITCH_WARP] + p.warpNudge, -64, 64);
        int pResp = iclamp(ps[PF_PITCH_RESP] + p.responseNudge, -64, 64);
        const int *rate = PF_RATES[p.pitchRate];
        float advance = global ? (float(rate[0]) / float(rate[1])) / float(slotDur) : 0.f;

        static const double rangeOcts[] = {0.5, 1, 2, 3};
        int rangeDeg = std::max(1, jsround(float(rangeOcts[stage[PF_PITCH_RANGE]] * N)));
        int baseDegree = stage[PF_BASE_PITCH] + noteAccV[nIdx] + p.octave * N + p.transpose;
        int tR = PF_REPEAT[stage[PF_T_REPEAT]];
        int pR = global ? 1 : PF_REPEAT[stage[PF_P_REPEAT]];
        int pWin = global ? 0 : stage[PF_P_WINDOW];

        struct Cand { float t, cv; };
        std::vector<Cand> cands;
        for (int i = 0; i < pulseCount; i++) {
            if ((maskByte >> ((i + stage[PF_MASK_SHIFT]) & 7)) & 1) continue;
            int aSlot; float aFrac;
            pfTemporalAlloc(i, pulseCount, tR, tWarp, aSlot, aFrac);
            if (!pfInWindow(aFrac, stage[PF_T_WINDOW])) continue;
            float tl = pfBend(pfCurve(tCurve, aFrac), tResp);
            if (stage[PF_T_FLIPV]) tl = 1 - tl;
            float tg = (float(aSlot) + tl) / float(tR);
            float ts = tg + float(stage[PF_PHASE_SHIFT] * 0.125);
            ts = float(std::fmod(double(ts), 1.0));
            if (ts < 0) ts = ts + 1;
            float trig = ts * float(slotDur);

            float phi;
            if (global) { phi = pitchPhase + trig * advance; phi = phi - std::floor(phi); }
            else phi = pulseCount == 1 ? 0.f : ts;
            float pv = pfPitchValue(phi, pR, pWarp, ps[PF_PITCH_FLIPH] != 0, pCurve, ps[PF_PITCH_FLIPV] != 0, pResp, pWin);
            float off;
            if (stage[PF_PITCH_DIR] == 0) off = pv * float(rangeDeg);
            else if (stage[PF_PITCH_DIR] == 1) off = -pv * float(rangeDeg);
            else off = (pv - 0.5f) * float(rangeDeg);
            int degree = baseDegree + lroundi(off);

            // Melody mask keeps the tonal degrees, Tilt the off-beat ones
            if (N > 0 && (stage[PF_MASK_MELODY] < 100 || stage[PF_TILT_MELODY] > 0)) {
                int c = pfCentrality(((degree % N) + N) % N, N);
                bool maskPass = c >= 30 * (100 - stage[PF_MASK_MELODY]) / 100;
                bool tiltPass = stage[PF_TILT_MELODY] > 0 && (30 - c) >= 30 * (100 - stage[PF_TILT_MELODY]) / 100;
                if (!maskPass && !tiltPass) continue;
            }
            cands.push_back({trig, degreeToVolts(scale, degree)});

            // Pulse-triggered accumulators move once per accepted pulse
            if (stage[PF_ACC_TRIG] == 1 && stage[PF_ACC_STEP] != 0)
                pfAdvance(noteAccV, noteDir, nIdx, noteCfg, stage[PF_ACC_STEP]);
            if (stage[PF_PULSE_ACC_TRIG] == 1 && stage[PF_PULSE_ACC_STEP] != 0)
                pfAdvance(pulseAccV, pulseDir, pIdx, pulseCfg, stage[PF_PULSE_ACC_STEP]);
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.t < b.t; });
        if (slotDur < PF_MIN_PULSE_GATE) return;

        for (size_t k = 0; k < cands.size(); k++) {
            int t = (int)std::trunc(cands[k].t);
            int next = k + 1 < cands.size() ? (int)std::trunc(cands[k + 1].t) : slotDur;
            int period = next - t;
            if (period <= 0) continue;
            int len = stage[PF_GATE];
            if (len == 0) continue;
            int g = len * period / 100;
            if (len == 1 && g < PF_MIN_PULSE_GATE) g = PF_MIN_PULSE_GATE;
            if (g < PF_MIN_PULSE_GATE) continue;
            if ((int)schedule.size() >= PF_MAX_PULSES) break;
            schedule.push_back({std::max(0, t) & 0xffff, g & 0xffff, cands[k].cv});
        }
        // temporal Flip H mirrors the burst in time
        if (stage[PF_T_FLIPH] && !schedule.empty()) {
            for (auto &s : schedule) s.at = std::max(0, slotDur - (s.at + s.gate)) & 0xffff;
            std::stable_sort(schedule.begin(), schedule.end(), [](const Pulse &a, const Pulse &b) { return a.at < b.at; });
        }
    }

    bool tick(uint32_t tick) {
        stepFired = false; stageTrigger = false;
        if (firstTick) { resetTickOffset = tick; firstTick = false; }
        if (layoutDirty || layoutSignature() != layoutKey) rebuildCumulativeTable();

        int cycle = cycleTicks;
        uint32_t rel = tick - resetTickOffset;
        if (cycle > 0) {
            if (p.globalPhase > 0) {
                float gp = float(p.globalPhase / 100.0);
                rel = (rel + (uint32_t)std::trunc(gp * float(cycle))) % cycle;
            }
            if (p.cyclePhaseWarp != 0) {
                float cp = float(double(rel % cycle) / cycle);
                rel = (uint32_t)std::trunc(pfBend(cp, p.cyclePhaseWarp) * float(cycle)) % cycle;
            }
        }
        // Reset Measure: back to the first stage, accumulators keep going
        if (p.resetMeasure > 0) {
            uint32_t rd = p.resetMeasure * measureTicks;
            if (rel % rd == 0 && rel != 0) {
                resetTickOffset = tick; rel = 0;
                pulseFired = 0; gateTimer = 0; gateState = false;
                prevSlotIdx = -1; prevCycleTick = 0;
            }
        }
        if (cycle <= 0) {
            // every stage skipped: silence
            gateState = false; gateTimer = 0;
            emit();
            return false;
        }

        const int *order = PF_TRAVERSALS[p.traversal];
        int cycleTick = rel % cycle;
        int slot = 0;
        for (int i = 0; i < PF_STAGES; i++) if (cycleTick >= cum[i] && cycleTick < cum[i + 1]) { slot = i; break; }
        slotIdx = slot;
        activeCell = order[slot];
        int slotTicks = cum[slot + 1] - cum[slot];
        stagePhase = slotTicks > 0 ? float(double(cycleTick - cum[slot]) / slotTicks) : 0.f;

        // a new pass through the cycle
        if ((uint32_t)cycleTick < prevCycleTick) {
            cycleCount++;
            pulseFired = 0;
            if (p.noteAcc.reset > 0 && cycleCount % p.noteAcc.reset == 0) clearAcc(true, false);
            if (p.pulseAcc.reset > 0 && cycleCount % p.pulseAcc.reset == 0) clearAcc(false, true);
        }
        prevCycleTick = cycleTick;

        if (slotIdx != prevSlotIdx) {
            if (prevSlotIdx >= 0) {
                // Stage-triggered accumulators move when their stage ends
                int done = order[prevSlotIdx];
                const PfStage &st = stages[done];
                if (!st[PF_SKIP]) {
                    if (st[PF_ACC_TRIG] == 0) pfAdvance(noteAccV, noteDir, p.noteAcc.scope == 1 ? 0 : done, p.noteAcc, st[PF_ACC_STEP]);
                    if (st[PF_PULSE_ACC_TRIG] == 0) pfAdvance(pulseAccV, pulseDir, p.pulseAcc.scope == 1 ? 0 : done, p.pulseAcc, st[PF_PULSE_ACC_STEP]);
                }
            }
            pulseFired = 0;
            prevSlotIdx = slotIdx;
            rebuildSchedule(slotTicks);
            stageTrigger = true;
        }

        int pos = cycleTick - cum[slotIdx];
        for (size_t k = 0; k < schedule.size(); k++) {
            if (pulseFired & (1u << k)) continue;
            if (pos < schedule[k].at) continue;
            pulseFired |= (1u << k);
            if (gateState) retrig = true;   // a new pulse over a held one: new note
            cvOutputTarget = schedule[k].cv;
            gateState = true;
            gateTimer = schedule[k].gate;
            stepFired = true;
        }
        if (gateTimer > 0) {
            gateTimer--;
            if (gateTimer == 0) gateState = false;
        }
        // the free-running pitch phase (Global pitch mode)
        if (slotTicks > 0) {
            const int *rate = PF_RATES[p.pitchRate];
            pitchPhase = pitchPhase + (float(rate[0]) / float(rate[1])) / float(slotTicks);
            pitchPhase = pitchPhase - std::floor(pitchPhase);
        }
        emit();
        return false;
    }

    void update() { cvOutput = cvOutputTarget; }

    void emit() {
        bool g = gateState;
        retrigger = g && retrig && prevGate;
        retrig = false;
        prevGate = g; prevCv = cvOutputTarget;
        gateOutput = g;
    }

    int noteAccFor(int cell) const { return noteAccV[p.noteAcc.scope == 0 ? cell : 0]; }

    // a brand-new module: four bursts that climb, fall, bloom and bounce
    void initFresh() {
        for (int i = 0; i < PF_STAGES; i++) { stages[i].clear(); if (i >= 4) stages[i][PF_SKIP] = 1; }
        PfStage *s = stages;
        s[0][PF_PULSES] = 4; s[0][PF_PITCH_CURVE] = 0; s[0][PF_PITCH_DIR] = 0;
        s[1][PF_PULSES] = 3; s[1][PF_PITCH_DIR] = 1; s[1][PF_BASE_PITCH] = 7; s[1][PF_T_CURVE] = 1;
        s[2][PF_PULSES] = 6; s[2][PF_PITCH_CURVE] = 1; s[2][PF_BASE_PITCH] = 2; s[2][PF_T_CURVE] = 1; s[2][PF_GATE] = 40;
        s[3][PF_PULSES] = 5; s[3][PF_PITCH_CURVE] = 3; s[3][PF_PITCH_DIR] = 2; s[3][PF_BASE_PITCH] = 4; s[3][PF_T_CURVE] = 2;
        layoutDirty = true;
    }
};

} // namespace xf
