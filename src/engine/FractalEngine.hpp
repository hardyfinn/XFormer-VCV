// FRACTAL engine (firmware engine/FractalTrackEngine.cpp, model/FractalSequence.h,
// model/FractalTrack.h)
//
// A phrase looper that grows. It listens to a source (here the GATE and CV inputs),
// sums up every section (one Divisor long) into one cell - pitch, how long the gate
// was held and where in the section it started - and writes the cells into a
// "trunk" of up to 128 cells. The trunk loops between Loop First and Loop Last.
// Branches are transformed copies of the trunk played after it, and ornaments add
// little flourishes (turns, runs, trills...) to gated cells.
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int FRACTAL_CELLS = 128;
enum { FR_OP_TRANSPOSE, FR_OP_REVERSE, FR_OP_INVERSE, FR_OP_RETINVERSE, FR_OP_ROTATE, FR_OP_COMPRESS, FR_OP_EXPAND, FR_OP_GATETHIN };
static const char *const FR_OP_NAMES[] = {"Transpose", "Reverse", "Inverse", "Retro Inv.", "Rotate", "Compress", "Expand", "Gate Thin"};
static const char *const FR_ORN_NAMES[] = {"Anticipation", "Suspension", "Syncopation", "Octave Up", "Fifth Up",
    "Half-Turn Tow.", "Half-Turn Away", "Run Toward", "Run Away", "Turn", "Arp Toward", "Arp Away",
    "Mordent Up", "Mordent Down", "Trill"};
static const char *const FR_ORDER_NAMES[] = {"Forward", "Reverse", "Pendulum", "Random", "Converge", "Diverge", "Page"};
static const uint32_t FR_RNG_SEED = 0x6f7261;

// ---- cell codec: bit 15 valid, bits 11-14 gate length (0 rest, 1 trig,
// 2-14 proportional, 15 tie), bits 0-10 signed pitch, 1024 = 60 semitones ----
inline int frEncode(float semi, int gateLen, bool valid) {
    int c = iclamp(lroundi(float(semi * 1024.f) / 60.f), -1024, 1023);
    int cell = (c & 0x7ff) | ((iclamp(gateLen, 0, 15) & 0xf) << 11);
    if (valid) cell |= 0x8000;
    return cell;
}
struct FrCell { float semi; int gateLen; bool valid; };
inline FrCell frDecode(int cell) {
    int c = cell & 0x7ff;
    if (c & 0x400) c -= 0x800;
    return {float(c * 60) / 1024.f, (cell >> 11) & 0xf, (cell & 0x8000) != 0};
}
inline int frGateLen(int high, int total) {
    if (high <= 0 || total <= 0) return 0;
    float f = float(double(high) / total);
    if (f >= 0.97f) return 15;
    if (f <= 0.18f) return 1;
    return iclamp(lroundi(f * 14.f), 2, 14);
}
inline int frOnset(int onsetTick, int total) {
    if (onsetTick < 0 || total <= 0) return 0;
    return iclamp(onsetTick * 16 / total, 0, 15);
}

// the module's scale, in float, with its volts -> degree snap
inline float frScaleVolts(const std::vector<int> &base, int note) {
    int n = (int)base.size(), oct = floorDiv(note, n);
    return float(oct) + float(base[note - oct * n]) * (1.f / 1536);
}
inline int frScaleFromVolts(const std::vector<int> &base, float volts) {
    volts = volts + 0.01f;
    int oct = (int)std::floor(volts);
    float frac = volts - float(oct);
    int idx = -1;
    for (int i = 0; i < (int)base.size(); i++) {
        if (frac < float(base[i]) * (1.f / 1536)) break;
        idx = i;
    }
    if (idx == -1) { idx = (int)base.size() - 1; oct--; }
    return oct * (int)base.size() + idx;
}
struct FrScale {
    const std::vector<int> *base;
    int rotate;
    FrScale(int index, int rot) {
        base = &scales()[iclamp(index, 0, scaleCount() - 1)].notes;
        rotate = imod(rot, (int)base->size());
    }
    float noteToVolts(int note) const {
        if (rotate == 0) return frScaleVolts(*base, note);
        return frScaleVolts(*base, note + rotate) - frScaleVolts(*base, rotate);
    }
    int noteFromVolts(float v) const {
        if (rotate == 0) return frScaleFromVolts(*base, v);
        return frScaleFromVolts(*base, v + frScaleVolts(*base, rotate)) - rotate;
    }
};

// the read order inside the loop for Converge / Diverge / Page
inline int frOrderMap(int mode, int step, int len) {
    if (len <= 0) return 0;
    int s = imod(step, len);
    if (mode == 4) { int half = s / 2; return s % 2 == 0 ? half : len - 1 - half; }
    if (mode == 5) {
        int c = (len - 1) / 2, k = (s + 1) / 2;
        return s % 2 == 0 ? iclamp(c - k, 0, len - 1) : iclamp(c + k, 0, len - 1);
    }
    if (mode == 6) {
        int pages = (len + 3) / 4;
        return iclamp((s % pages) * 4 + s / pages, 0, len - 1);
    }
    return s;
}
// Forward, Reverse, Pendulum, Random -> run modes; the rest walk forward and are remapped
inline int frRunMode(int mode) { return mode == 1 ? 1 : (mode == 2 ? 2 : (mode == 3 ? 4 : 0)); }

// which segment the n-th part of the walk plays: the trunk, then branches whose
// Path bit is 0 going out, then branches whose bit is 1 coming back
inline int frRouteOf(int path, int n, int r) {
    if (r == 0) return 0;
    int seen = 0;
    for (int b = 1; b <= n; b++) if (!((path >> (b - 1)) & 1) && ++seen == r) return b;
    for (int b = n; b >= 1; b--) if (((path >> (b - 1)) & 1) && ++seen == r) return b;
    return 0;
}

struct FractalParams {
    int divisor = 12, clockMult = 100, resetMeasure = 0, loopFirst = 0, loopLast = 15, rotate = 0, orderMode = 0;
    int recordSkip = 0, recordFirst = 0, recordLast = 15, ornFirst = 0, ornLast = 15;
    int recordMode = 0, cadence = 1, fidelity = 1, record = 0;
    int branchCount = 0, path = 0, branchSeed = 0, branchPool = 0xff, ornRate = 0, ornIntensity = 0;
    int scale = 1, rootNote = 0, scaleRotate = 0, lock = 0, octave = 0, transpose = 0, quantize = 0, trackDelay = 0;
};

struct FractalEngine {
    FractalParams p;
    int trunk[FRACTAL_CELLS] = {}, onset[FRACTAL_CELLS] = {};
    struct Branch { int kind = 0, t = 0, k = 0, fx100 = 100, n = 2; };
    Branch branches[7];
    int measureTicks = 768;
    bool quirks = false;
    uint32_t rngSeed = FR_RNG_SEED;
    // the source: capture and replay run in the same tick, in the module's order
    bool inlineSource = true;
    bool srcGate = false;
    float srcCv = 0;

    // outputs
    bool gateOutput = false;
    float cvOutput = 0;
    bool stepFired = false;
    bool cellWritten = false;     // a cell was recorded this tick (at lastWritten)
    int lastWritten = -1;
    int recordPos = 0, readPos = 0, segment = 0;
    int lastOrnament = -1, shownOrnament = -1, ornamentCount = 0;
    int queuedSegment = -1, queuedOrnament = -1;

    Random rng{FR_RNG_SEED};
    int skipLeft = 0;
    bool wasArmed = false;
    int globalPos = 0;
    SequenceState order;
    long branchHash = -1;
    int branchCountCache = -1;
    bool gate = false;
    float cv = 0;
    struct GateEv { uint32_t t; bool g; };
    struct CvEv { uint32_t t; float cv; };
    std::vector<GateEv> gateEv;
    std::vector<CvEv> cvEv;
    size_t gateHead = 0, cvHead = 0;
    struct DelayEv { int cv, gateLen; bool orn; int left, readPos; };
    std::vector<DelayEv> delay;
    int lastDir = 1;
    int obsTicks = 0, obsHigh = 0, obsOnset = -1;
    float obsEdgeCv = 0, obsLastCv = 0;
    bool obsAny = false, obsPrevGate = false;
    bool eventHasRef = false;
    int eventRefSemi = 0, eventInhibit = 0;
    bool prevGate = false;

    FractalEngine() { reset(); }

    // test harness: the module's constructor runs self-checks that draw from the
    // ornament dice before the track ever plays; replay those draws
    void refInit(bool) {
        rng = Random(rngSeed);
        for (int i = 0; i < 52; i++) rng.nextRange(100);
        for (int i = 0; i < 200; i++) {
            rng.nextRange(100);
            uint32_t o = rng.nextRange(15);
            if (o != 1 && o != 2) break;
        }
    }

    bool setParam(const std::string &name, double value) {
        int v = jsround(value);
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"resetMeasure", &p.resetMeasure, 0, 128},
            {"loopFirst", &p.loopFirst, 0, 127}, {"loopLast", &p.loopLast, 0, 127}, {"rotate", &p.rotate, -64, 64},
            {"orderMode", &p.orderMode, 0, 6}, {"recordSkip", &p.recordSkip, 0, 15}, {"recordFirst", &p.recordFirst, 0, 127},
            {"recordLast", &p.recordLast, 0, 127}, {"ornFirst", &p.ornFirst, 0, 127}, {"ornLast", &p.ornLast, 0, 127},
            {"recordMode", &p.recordMode, 0, 1}, {"cadence", &p.cadence, 0, 1}, {"fidelity", &p.fidelity, 0, 1},
            {"record", &p.record, 0, 1}, {"branchCount", &p.branchCount, 0, 7}, {"path", &p.path, 0, 255},
            {"branchSeed", &p.branchSeed, 0, 65535}, {"branchPool", &p.branchPool, 0, 255}, {"ornRate", &p.ornRate, 0, 100},
            {"ornIntensity", &p.ornIntensity, 0, 100}, {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1},
            {"rootNote", &p.rootNote, 0, 11}, {"scaleRotate", &p.scaleRotate, 0, 31}, {"lock", &p.lock, 0, 1},
            {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -100, 100}, {"quantize", &p.quantize, 0, 1},
            {"trackDelay", &p.trackDelay, 0, 16}};
        for (auto &r : ranges) if (name == r.n) { *r.f = iclamp(v, r.lo, r.hi); return true; }
        return false;
    }
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "sourceA") return true;
        return setParam(t[0], std::stod(t[1]));
    }

    void clearEvents() { gateEv.clear(); cvEv.clear(); gateHead = 0; cvHead = 0; }

    // transport start / Reset: back to the start of the loop; the recorded trunk stays
    void reset() {
        // the module rolls the same ornaments every time the song plays from the start
        if (!quirks) rng = Random(rngSeed);
        recordPos = p.recordFirst;
        skipLeft = 0;
        wasArmed = false;
        readPos = p.loopFirst;
        segment = 0;
        globalPos = 0;
        order = SequenceState();
        branchHash = -1;
        gate = false; cv = 0;
        clearEvents();
        delay.clear();
        lastOrnament = -1; lastDir = 1;
        queuedSegment = -1; queuedOrnament = -1;
        resetObservation();
        obsLastCv = 0;
        eventHasRef = false; eventRefSemi = 0; eventInhibit = 0;
        prevGate = false; gateOutput = false; cvOutput = 0;
    }
    void clearTrunk() {
        for (int i = 0; i < FRACTAL_CELLS; i++) { trunk[i] = 0; onset[i] = 0; }
        recordPos = p.recordFirst; skipLeft = 0; wasArmed = false;
        readPos = p.loopFirst; globalPos = 0; order = SequenceState();
        gate = false; clearEvents(); delay.clear();
        lastOrnament = -1;
        resetObservation();
        eventHasRef = false; eventInhibit = 0;
    }
    void barBoundary() {}

    int effectiveDivisor() const {
        float cm = float(p.clockMult) * 0.01f;
        if (cm <= 0) cm = 1;
        return std::max(1, lroundi(float(p.divisor * 4) / cm));
    }

    bool tick(uint32_t tick) {
        stepFired = false; cellWritten = false;
        int div = effectiveDivisor();
        uint32_t rd = p.resetMeasure * measureTicks;
        uint32_t rel = rd == 0 ? tick : tick % rd;
        bool atReset = rd > 0 && tick % rd == 0;
        if (inlineSource) {
            if (atReset) recordPos = p.recordFirst;
            if (rel % div == 0) captureBoundary();
            replayTick(tick, div, rel, atReset);
            observe(srcGate, srcCv, div);
        } else replayTick(tick, div, rel, atReset);
        drain(tick);
        emit();
        return false;
    }
    void replayTick(uint32_t tick, int div, uint32_t rel, bool atReset) {
        if (atReset) { readPos = p.loopFirst; globalPos = 0; order = SequenceState(); }
        if (rel % div != 0) return;
        if (queuedSegment >= 0) {
            int n = iclamp(p.branchCount, 0, 7);
            for (int i = 0; i <= n; i++) {
                if (frRouteOf(p.path, n, i) == queuedSegment) { globalPos = i * loopLen(); order = SequenceState(); break; }
            }
            queuedSegment = -1;
        }
        replaySection(tick, div);
    }

    bool capturing() const { return p.record && !p.lock; }

    void captureBoundary() {
        bool armed = p.record != 0;
        if (armed && !wasArmed) { skipLeft = 0; resetObservation(); }
        wasArmed = armed;
        if (capturing() && p.cadence == 0) captureSection();
        resetObservation();
    }
    void resetObservation() {
        obsTicks = 0; obsHigh = 0; obsOnset = -1; obsEdgeCv = 0;
        obsAny = false; obsPrevGate = false;
    }
    // one tick of listening: gate time, first onset and its pitch; Event cadence
    // writes a cell whenever the incoming pitch moves by about a semitone
    void observe(bool g, float inCv, int div) {
        if (!capturing()) return;
        float semi = inCv * 12.f;
        obsLastCv = inCv;
        if (g) {
            obsHigh = (obsHigh + 1) & 0xffff;
            obsAny = true;
            if (!obsPrevGate && obsOnset < 0) { obsOnset = obsTicks; obsEdgeCv = inCv; }
        }
        if (eventInhibit > 0) eventInhibit--;
        if (p.cadence == 1) {
            if (!eventHasRef) {
                if (g) { eventRefSemi = lroundi(semi); eventHasRef = true; }
            } else if (std::fabs(semi - float(eventRefSemi)) >= 1.f - 1.f / 3 && eventInhibit == 0) {
                eventRefSemi = lroundi(semi);
                eventInhibit = std::max(1, div / 8);
                commitCell(semi, g ? 8 : 0, 0);
                advanceRecordPos();
            }
        }
        obsPrevGate = g;
        obsTicks = (obsTicks + 1) & 0xffff;
    }
    void captureSection() {
        if (skipLeft > 0) { skipLeft--; return; }
        bool feel = p.fidelity == 1;
        int total = std::max(1, obsTicks);
        int gl = frGateLen(obsHigh, total);
        float c = (feel && obsAny) ? obsEdgeCv : obsLastCv;
        int on = (feel && obsOnset >= 0) ? frOnset(obsOnset, total) : 0;
        // Latch keeps what was there when the section was silent (overdub)
        if (!(p.recordMode == 1 && gl == 0)) commitCell(c * 12.f, gl, on);
        advanceRecordPos();
        skipLeft = p.recordSkip;
    }
    void commitCell(float semi, int gl, int on) {
        trunk[recordPos] = frEncode(semi, gl, true);
        onset[recordPos] = on & 0xf;
        cellWritten = true; lastWritten = recordPos;
    }
    void advanceRecordPos() {
        int first = p.recordFirst, last = std::max(first, p.recordLast);
        if (recordPos < first || recordPos > last) { recordPos = first; return; }
        recordPos = recordPos >= last ? first : recordPos + 1;
    }

    // ---- the loop and its branches ----
    int loopLen() const { return std::max(p.loopFirst, p.loopLast) - p.loopFirst + 1; }
    int trunkIndex(int within) const {
        int len = loopLen(), s = imod(within, len);
        if (p.rotate != 0) s = (s + (p.rotate % len) + len) % len;
        return iclamp(p.loopFirst + s, 0, FRACTAL_CELLS - 1);
    }
    float inversionCenter() const {
        int c = trunk[iclamp(p.loopFirst, 0, FRACTAL_CELLS - 1)];
        return (c & 0x8000) ? frDecode(c).semi : 0.f;
    }
    long branchSignature() const { return long(uint32_t(p.branchSeed * 131 + p.branchPool * 17 + loopLen()) & 0x7fffffff); }
    // one transform per branch, drawn from the Pool with the Seed (same seed, same chain)
    void rebuildBranches() {
        int len = loopLen();
        std::vector<int> elig;
        for (int k = 0; k < 8; k++) if ((p.branchPool >> k) & 1) elig.push_back(k);
        if (elig.empty()) elig.push_back(FR_OP_TRANSPOSE);
        Random r(p.branchSeed);
        int n = iclamp(p.branchCount, 0, 7);
        for (int s = 1; s <= n; s++) {
            Branch &b = branches[s - 1];
            b.kind = elig[r.nextRange(elig.size())];
            b.t = 0; b.k = 0; b.fx100 = 100; b.n = 2;
            if (b.kind == FR_OP_TRANSPOSE) { static const int mags[] = {5, 7, 12}; int mag = mags[r.nextRange(3)]; b.t = (r.nextBinary() ? -1 : 1) * mag; }
            else if (b.kind == FR_OP_ROTATE) b.k = len > 1 ? 1 + (int)r.nextRange(len - 1) : 0;
            else if (b.kind == FR_OP_COMPRESS) b.fx100 = r.nextRange(2) ? 67 : 50;
            else if (b.kind == FR_OP_EXPAND) b.fx100 = r.nextRange(2) ? 200 : 150;
            else if (b.kind == FR_OP_GATETHIN) b.n = 2 + (int)r.nextRange(3);
        }
        branchHash = branchSignature();
        branchCountCache = n;
    }
    FrCell segmentCell(int seg, int pos) const {
        int len = loopLen();
        pos = imod(pos, len);
        if (seg == 0) return frDecode(trunk[trunkIndex(pos)]);
        const Branch &tf = branches[seg - 1];
        if (tf.kind == FR_OP_REVERSE) return segmentCell(seg - 1, len - 1 - pos);
        if (tf.kind == FR_OP_RETINVERSE) {
            FrCell c = segmentCell(seg - 1, len - 1 - pos);
            c.semi = 2 * inversionCenter() - c.semi;
            return c;
        }
        if (tf.kind == FR_OP_ROTATE) return segmentCell(seg - 1, pos + tf.k);
        FrCell c = segmentCell(seg - 1, pos);
        if (tf.kind == FR_OP_TRANSPOSE) c.semi = c.semi + float(tf.t);
        else if (tf.kind == FR_OP_INVERSE) c.semi = 2 * inversionCenter() - c.semi;
        else if (tf.kind == FR_OP_COMPRESS || tf.kind == FR_OP_EXPAND) {
            float ctr = inversionCenter();
            c.semi = ctr + float(lroundi(((c.semi - ctr) * float(tf.fx100)) * 0.01f));
        } else if (tf.kind == FR_OP_GATETHIN && (pos + 1) % tf.n == 0) c.gateLen = 0;
        return c;
    }

    void replaySection(uint32_t tick, int div) {
        if (branchSignature() != branchHash || iclamp(p.branchCount, 0, 7) != branchCountCache) rebuildBranches();
        int n = iclamp(p.branchCount, 0, 7), len = loopLen(), total = len * (1 + n);
        int gp = globalPos % total;
        int seg = frRouteOf(p.path, n, iclamp(gp / len, 0, n));
        int rm = frRunMode(p.orderMode);
        order.advanceFree(rm, 0, len - 1, rng);
        int within = frOrderMap(p.orderMode, order.step, len);
        SequenceState next = order;
        next.advanceFree(rm, 0, len - 1, rng);
        int nextWithin = frOrderMap(p.orderMode, next.step, len);

        FrCell cell = segmentCell(seg, within), nextCell = segmentCell(seg, nextWithin);
        int readIdx = trunkIndex(within);
        bool gated = cell.valid && cell.gateLen > 0;
        bool inZone = readIdx >= p.ornFirst && readIdx <= p.ornLast;
        bool ornOk = gated && (inZone || queuedOrnament >= 0);
        int onsetTicks = p.fidelity == 1 ? onset[readIdx] * div / 16 : 0;
        segment = seg;
        if (p.trackDelay <= 0) {
            readPos = readIdx;
            scheduleSection(tick + onsetTicks, div, cell.semi, nextCell.semi, cell.valid ? cell.gateLen : 0, ornOk);
        } else {
            // Track Delay: play what was read Delay sections ago (a canon with itself)
            drainDelay(tick, div);
            if (delay.size() < 31) delay.push_back({iclamp(lroundi(cell.semi * 256.f), -32768, 32767),
                (cell.valid ? cell.gateLen : 0) & 0xf, ornOk, iclamp(p.trackDelay - 1, 0, 15), readIdx & 0x7f});
        }
        globalPos = ((gp + 1) % total) & 0xffff;
    }
    void drainDelay(uint32_t tick, int div) {
        std::vector<DelayEv> pending;
        pending.swap(delay);
        for (auto &e : pending) {
            if (e.left == 0) {
                float semi = float(e.cv) * (1.f / 256);
                readPos = e.readPos;
                scheduleSection(tick, div, semi, semi, e.gateLen, e.orn);
            } else { e.left--; delay.push_back(e); }
        }
    }

    // ---- notes and ornaments ----
    FrScale scale() const { return FrScale(p.scale, p.scaleRotate); }
    float quantizeSemi(float semi) const {
        if (!p.quantize) return semi;
        FrScale s = scale();
        return s.noteToVolts(s.noteFromVolts(semi * (1.f / 12))) * 12.f;
    }
    float stepDegrees(float semi, int steps) const {
        FrScale s = scale();
        return s.noteToVolts(s.noteFromVolts(semi * (1.f / 12)) + steps) * 12.f;
    }
    float playbackCv(float semi) const {
        float c = semi * (1.f / 12);
        c = c + float(p.octave);
        return c + float(p.transpose) * (1.f / 12);
    }
    // 2-step ornaments always, 4-step from Intensity 40, the trill from 75
    int rollOrnament(int rate, int intensity) {
        if (queuedOrnament >= 0) { int q = queuedOrnament; queuedOrnament = -1; lastOrnament = q; return q; }
        if (rate <= 0) return -1;
        if ((int)rng.nextRange(100) >= rate) return -1;
        if (p.lock) return lastOrnament;
        int pool = 7 + (intensity >= 40 ? 7 : 0) + (intensity >= 75 ? 1 : 0);
        int o = (int)rng.nextRange(pool);
        lastOrnament = o;
        return o;
    }
    void pushCv(uint32_t at, float c) { if (cvEv.size() < 10) cvEv.push_back({at, c}); }
    void pushGate(uint32_t at, bool g) { if (gateEv.size() < 18) gateEv.push_back({at, g}); }
    // times are 32-bit unsigned like the module's, so a too-short note wraps to "already over"
    void note(float semi, uint32_t at, uint32_t dur, uint32_t gap = 1) {
        pushCv(at, playbackCv(semi));
        pushGate(at, true);
        pushGate(at + (dur > gap ? dur - gap : 1), false);
    }
    void scheduleSection(uint32_t tick, int div, float main, float nextSemi, int gateLen, bool ornOk) {
        clearEvents();
        main = quantizeSemi(main);
        nextSemi = quantizeSemi(nextSemi);
        if (gateLen <= 0) { pushGate(tick, false); return; }
        uint32_t full = std::max(1, div * gateLen / 15), e = std::max(1, div / 8);
        int name = ornOk ? rollOrnament(p.ornRate, p.ornIntensity) : -1;
        if (name < 0) { note(main, tick, full, 0); return; }
        shownOrnament = name;
        ornamentCount++;
        int dir;
        float ant;
        if (p.lock) { dir = lastDir; ant = stepDegrees(main, dir); }
        else { dir = nextSemi >= main ? 1 : -1; lastDir = dir; ant = nextSemi; }
        auto grace = [&](float semi) { note(main, tick, full - e); note(semi, tick + (full - e), e); };
        uint32_t s;
        switch (name) {
        case 0: note(ant, tick, e); note(main, tick + e, full - e); break;              // Anticipation
        case 1: note(main, tick, full); break;                                          // Suspension
        case 2: note(main, tick + e, full - e); break;                                  // Syncopation
        case 3: grace(main + 12.f); break;                                              // Octave up
        case 4: grace(stepDegrees(main, 4)); break;                                     // Fifth up
        case 5: grace(stepDegrees(main, dir)); break;                                   // Half-turn toward
        case 6: grace(stepDegrees(main, -dir)); break;                                  // Half-turn away
        case 9:                                                                         // Turn
            note(main, tick, e); note(stepDegrees(main, 1), tick + e, e);
            note(stepDegrees(main, -1), tick + 2 * e, e); note(main, tick + 3 * e, full - 3 * e); break;
        case 12: case 13: {                                                             // Mordents
            int d = name == 12 ? 1 : -1;
            note(main, tick, e); note(stepDegrees(main, d), tick + e, e); note(main, tick + 2 * e, full - 2 * e); break;
        }
        case 7: case 8: {                                                               // Runs
            int rd = name == 7 ? dir : -dir;
            s = std::max(1, div / 5);
            note(main, tick, s);
            for (int k = 1; k <= 4; k++) note(stepDegrees(main, rd * k), tick + k * s, s);
            break;
        }
        case 10: case 11: {                                                             // Arpeggios
            int ad = name == 10 ? dir : -dir;
            static const int degs[] = {0, 2, 4};
            s = std::max(1, div / 4);
            for (int k = 0; k < 3; k++) note(stepDegrees(main, ad * degs[k]), tick + k * s, s);
            break;
        }
        case 14: {                                                                      // Trill
            float up = stepDegrees(main, 1);
            s = std::max(1, div / 8);
            for (int k = 0; k < 8; k++) note((k & 1) ? up : main, tick + k * s, s);
            break;
        }
        default: note(main, tick, full, 0);
        }
    }
    void drain(uint32_t tick) {
        while (cvHead < cvEv.size() && tick >= cvEv[cvHead].t) { cv = cvEv[cvHead].cv; cvHead++; }
        bool fired = false;
        while (gateHead < gateEv.size() && tick >= gateEv[gateHead].t) { gate = gateEv[gateHead].g; gateHead++; fired = true; }
        if (fired && gate) stepFired = true;
    }

    void update() { cvOutput = cv; }
    void emit() { prevGate = gate; gateOutput = gate; cvOutput = cv; }

    // a brand-new module: a 16-cell phrase to play with before you record your own
    void initFresh() {
        static const int notes[] = {0, 3, 7, 10, 12, 10, 7, 5, 3, 5, 7, 3, 0, -2, 0, 7};
        static const int gates[] = {8, 4, 8, 4, 12, 0, 8, 4, 8, 4, 8, 0, 15, 4, 8, 4};
        for (int i = 0; i < FRACTAL_CELLS; i++) { trunk[i] = 0; onset[i] = 0; }
        for (int i = 0; i < 16; i++) trunk[i] = frEncode(float(notes[i]), gates[i], true);
    }
};

} // namespace xf
