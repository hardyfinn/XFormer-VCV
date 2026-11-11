// ALGO engine (engine/TuesdayTrackEngine.cpp): 16 generative algorithms driven by
// Flow (the structure seed), Ornament (the detail seed) and Power (density).
// Each algorithm returns one abstract step (scale degree, octave, velocity, accent,
// slide, gate ratio, timing offset, trills); tick() then shapes it into gates.
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int TU_LOOP_LENGTHS[30] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                        19, 21, 24, 32, 35, 42, 48, 56, 64, 95, 96, 127, 128};
static const int TU_MASK_VALUES[14] = {2, 3, 5, 11, 19, 31, 43, 61, 89, 131, 197, 277, 409, 599};
static const char *const ALGO_NAMES[16] = {"Test", "TriTrance", "Stomper", "Markov", "Chip1", "Chip2", "Wobble",
                                           "ScaleWlk", "Window", "Minimal", "Ganz", "Blake", "Aphex", "Autechre", "StepWave", "Turing"};

// Music-Thing style Turing register (engine/generators/TuringRegister.h)
struct TuringRegister {
    uint32_t reg = 0xffffffffu;
    int length = 16;
    void seed(uint32_t v) { reg = v ? v : 0xffffffffu; }
    void setLength(Random &rng, int len) {
        len &= 0xff;
        if (len < 3) len = 3; else if (len > 32) len = 32;
        if (len > length) reg |= (rng.next() & 1u) << length;
        length = len;
    }
    uint32_t clock(Random &rng, int probability) {
        uint32_t sr = reg;
        if (probability >= 255 || (int)rng.nextRange(255) < probability) sr ^= 1u;
        uint32_t msb = 1u << (length - 1);
        if (sr & 1u) sr = (sr >> 1) | msb; else sr = (sr >> 1) & ~msb;
        if (!sr) sr = 1u << (length - 1);
        reg = sr;
        uint32_t mask = length >= 32 ? 0xffffffffu : ((1u << length) - 1u);
        return sr & mask;
    }
};

struct TuesdayParams {
    int algorithm = 1, flow = 4, ornament = 4, power = 12, start = 0, loopLength = 0;
    int glide = 50, trill = 50, stepTrill = 0, skew = 0, octave = 0, transpose = 0;
    int divisor = 12, clockMult = 100, scale = 1, rootNote = 0, scaleRotate = 0;
    int gateLength = 50, gateOffset = 0, maskParameter = 0, timeMode = 0, maskProgression = 0, rotate = 0;
};

struct TuesdayEngine {
    TuesdayParams p;
    bool quirks = false;
    int measureTicks = 768;

    // outputs
    bool gateOutput = false;
    double cvOutput = 0;
    int velocity = 100;           // MIDI-style 1..127 of the last note
    bool accent = false;
    bool slideActive = false;
    int displayStep = -1;
    bool stepFired = false;

    // algorithm state (each algorithm sets the fields it uses in initAlgorithm)
    struct State {
        int b1 = 0, b2 = 0, b3 = 0;
        int mode = 0, countDown = 0, lowNote = 0, lastNote = 0, lastOctave = 0, highNote[2] = {0, 0};
        int history1 = 0, history3 = 0, matrix[8][8][2] = {};
        uint32_t chordSeed = 0, rngSeed = 0;
        int base = 0, dir = 0;
        int chordScaler = 0, offset = 0, len = 0, timeMult = 0, deadTime = 0, idx = 0, chordLen = 0;
        uint32_t phase = 0, phaseSpeed = 0, phase2 = 0, phaseSpeed2 = 0;
        int lastWasHigh = 0;
        int pos = 0;
        uint32_t slowPhase = 0, fastPhase = 0;
        int noteMemory = 0, noteHistoryW = 0, ghostThreshold = 0, phaseRatio = 3;
        int burstLength = 0, silenceLength = 0, clickDensity = 0, silenceTimer = 0, burstTimer = 0, noteIndex = 0;
        uint32_t phaseA = 0, phaseB = 0, phaseC = 0;
        int noteHistoryG[3] = {0, 0, 0}, selectMode = 0, skipDecimator = 0, phraseSkipCount = 0, velocitySample = 0;
        int motif[4] = {0, 0, 0, 0};
        uint32_t breathPhase = 0;
        int breathPattern = 0, breathCycleLength = 4, subBassCountdown = 0;
        int track1[4] = {}, track2[3] = {}, track3[5] = {}, pos1 = 0, pos2 = 0, pos3 = 0;
        int pattern[8] = {}, rule_timer = 0, rule_sequence[8] = {}, rule_index = 0;
        int direction = 0, step_count = 0, current_step = 0, chromatic_offset = 0;
        bool is_stepped = true;
        int sweepSpeed = 0, testAccent = 0, testVelocity = 0, note = 0;
    } S;

    struct MicroGate { uint32_t tick; bool gate; double cvTarget; int velocity; bool slide; };
    SortedQueue<MicroGate> microGateQueue{32};
    Random rng, extraRng, uiRng;
    TuringRegister turingSR;

    int cachedFlow = -1, cachedOrnament = -1, cachedAlgorithm = -1, cachedLoopLength = -1;
    int stepIndex = 0, gateTicks = 0, coolDown = 0, coolDownMax = 0, microCoolDown = 0, microCoolDownMax = 0;
    int gateOffsetPct = 0, slide = 0;
    double cvTarget = 0, cvCurrent = 0, cvDelta = 0;
    int slideCountDown = 0;
    bool retriggerArmed = false;
    int retriggerCount = 0, retriggerPeriod = 0, retriggerLength = 0, retriggerVelocity = 0;
    double trillCvTarget = 0;
    int ratchetInterval = 0;
    int primeMaskCounter = 0, primeMaskState = 1, cachedPrimeParam = -1, cachedTimeMode = -1;
    int currentMaskArrayIndex = 0, cachedMaskProgression = -1;

    TuesdayEngine() { reset(); }

    bool setParam(const std::string &name, double value) {
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"algorithm", &p.algorithm, 0, 15}, {"flow", &p.flow, 0, 16}, {"ornament", &p.ornament, 0, 16},
            {"power", &p.power, 0, 16}, {"start", &p.start, 0, 16}, {"loopLength", &p.loopLength, 0, 29},
            {"glide", &p.glide, 0, 100}, {"trill", &p.trill, 0, 100}, {"stepTrill", &p.stepTrill, 0, 100},
            {"skew", &p.skew, -8, 8}, {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -11, 11},
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1},
            {"rootNote", &p.rootNote, 0, 11}, {"scaleRotate", &p.scaleRotate, 0, 31}, {"gateLength", &p.gateLength, 0, 100},
            {"gateOffset", &p.gateOffset, 0, 100}, {"maskParameter", &p.maskParameter, 0, 15}, {"timeMode", &p.timeMode, 0, 3},
            {"maskProgression", &p.maskProgression, 0, 3}, {"rotate", &p.rotate, -127, 127}};
        for (auto &r : ranges) if (name == r.n) { *r.f = iclamp(jsround(value), r.lo, r.hi); return true; }
        return false;
    }
    bool setRefParam(const std::vector<std::string> &t) { return setParam(t[0], std::stod(t[1])); }
    void barBoundary() {}

    int actualLoopLength() const { int i = p.loopLength; return (i >= 0 && i < 30) ? TU_LOOP_LENGTHS[i] : 16; }
    int rotateValue() const { int len = actualLoopLength(); return len > 0 ? iclamp(p.rotate, -(len - 1), len - 1) : 0; }
    int effectiveDivisor() const {
        float clockMult = float(p.clockMult) * 0.01f;
        double d = p.divisor * (CONFIG_PPQN / CONFIG_SEQUENCE_PPQN);
        return std::max(1, jsround(float(d / clockMult)));
    }

    void initAlgorithm() {
        int flow = p.flow, ornament = p.ornament, algorithm = p.algorithm;
        uint32_t flowSeed = uint32_t(flow - 1) << 4;
        uint32_t ornamentSeed = uint32_t(ornament - 1) << 4;
        uint32_t extraSeed = ornamentSeed + 0x9e3779b9u;
        uiRng = Random(uint32_t(flow * 37 + ornament * 101));
        cachedFlow = flow; cachedOrnament = ornament; cachedAlgorithm = algorithm;
        cachedLoopLength = p.loopLength;
        cachedPrimeParam = -1;

        switch (algorithm) {
        case 1: // TRITRANCE
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.b1 = rng.next() & 0x7; S.b2 = rng.next() & 0x7;
            S.b3 = extraRng.next() & 0x15;
            if (S.b3 >= 7) S.b3 -= 7; else S.b3 = 0;
            S.b3 -= 4;
            break;
        case 2: // STOMPER
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.mode = (extraRng.next() % 7) * 2;
            S.countDown = 0;
            S.lowNote = rng.next() % 3;
            S.lastNote = S.lowNote; S.lastOctave = 0;
            S.highNote[0] = rng.next() % 7; S.highNote[1] = rng.next() % 5;
            break;
        case 3: // MARKOV
            rng = Random(flowSeed);
            S.history1 = rng.next() & 0x7; S.history3 = rng.next() & 0x7;
            for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) { S.matrix[i][j][0] = rng.next() % 8; S.matrix[i][j][1] = rng.next() % 8; }
            break;
        case 4: // CHIPARP 1
            rng = Random(flowSeed);
            S.chordSeed = rng.next(); S.rngSeed = S.chordSeed;
            S.base = rng.next() % 3; S.dir = (rng.next() >> 7) % 2;
            break;
        case 5: // CHIPARP 2
            rng = Random(flowSeed);
            initChip2();
            S.chordLen = 3 + (flow >> 2);
            break;
        case 6: // WOBBLE
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.phase = 0; S.phaseSpeed = 0x08000000; S.phase2 = 0; S.lastWasHigh = 0; S.phaseSpeed2 = 0x02000000;
            break;
        case 7: // SCALEWALKER
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.pos = 0;
            break;
        case 8: // WINDOW
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            initWindow();
            break;
        case 9: // MINIMAL
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.burstLength = 2 + (rng.next() % 7);
            S.silenceLength = 4 + (flow % 13);
            S.clickDensity = (ornament * 16) & 0xff;
            S.mode = 0; S.silenceTimer = S.silenceLength; S.burstTimer = 0; S.noteIndex = 0;
            break;
        case 10: // GANZ
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            initGanz(flow);
            break;
        case 11: // BLAKE
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            for (int i = 0; i < 4; i++) S.motif[i] = rng.next() % 7;
            S.breathPhase = rng.next() << 16;
            S.breathPattern = (flow >> 2) % 4;
            S.breathCycleLength = 4 + ((ornament >> 2) % 4);
            S.subBassCountdown = 0;
            break;
        case 12: // APHEX
            rng = Random(flowSeed);
            initAphexTracks();
            S.pos1 = (ornament * 1) % 4; S.pos2 = (ornament * 2) % 3; S.pos3 = (ornament * 3) % 5;
            break;
        case 13: // AUTECHRE
            rng = Random(flowSeed);
            // seeded here so the pattern repeats exactly after every reset
            extraRng = Random(extraSeed);
            initAutechrePattern();
            break;
        case 14: // STEPWAVE
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            S.direction = 0; S.step_count = 3 + (rng.next() % 5);
            S.current_step = 0; S.chromatic_offset = 0; S.is_stepped = true;
            break;
        case 15: // TURING
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            turingSR.seed(flowSeed);
            turingSR.setLength(rng, 2 + flow * 2);
            break;
        default: // 0 = TEST
            S.mode = (flow - 1) >> 3;
            S.sweepSpeed = (flow - 1) & 0x3;
            S.testAccent = (ornament - 1) >> 3;
            S.testVelocity = int(uint32_t(ornament - 1) << 4) & 0xff;
            S.note = 0;
            rng = Random(flowSeed); extraRng = Random(extraSeed);
            break;
        }
    }
    void initChip2() {
        S.rngSeed = rng.next();
        S.chordScaler = (rng.next() % 3) + 2;
        S.offset = rng.next() % 5;
        S.len = ((rng.next() & 0x3) + 1) * 2;
        S.timeMult = rng.nextBinary() ? (rng.nextBinary() ? 1 : 0) : 0;
        S.deadTime = 0; S.idx = 0;
        S.dir = rng.nextBinary() ? (rng.nextBinary() ? 1 : 0) : 0;
    }
    void initWindow() {
        S.slowPhase = rng.next() << 16; S.fastPhase = rng.next() << 16;
        S.noteMemory = rng.next() & 0x7; S.noteHistoryW = rng.next() & 0x7;
        S.ghostThreshold = rng.next() & 0x1f; S.phaseRatio = 3 + (rng.next() & 0x3);
    }
    void initGanz(int flow) {
        S.phaseA = rng.next() << 16; S.phaseB = rng.next() << 16; S.phaseC = rng.next() << 16;
        for (int i = 0; i < 3; i++) S.noteHistoryG[i] = rng.next() % 7;
        S.selectMode = rng.next() % 4;
        S.skipDecimator = flow >> 2;
        S.phraseSkipCount = 0;
        S.velocitySample = 128 + (extraRng.next() & 0x7F);
    }
    void initAphexTracks() {
        for (int i = 0; i < 4; ++i) S.track1[i] = rng.next() % 12;
        for (int i = 0; i < 3; ++i) S.track2[i] = rng.next() % 3;
        for (int i = 0; i < 5; ++i) S.track3[i] = (rng.next() % 8 == 0) ? (int)(rng.next() % 5) : 0;
    }
    void initAutechrePattern() {
        for (int i = 0; i < 8; ++i) {
            uint32_t r = rng.next() % 4;
            int v = (r == 0) ? 12 : (r == 1 ? 24 : 0);
            if (rng.nextBinary()) v += (rng.next() % 5) * 2;
            S.pattern[i] = v;
        }
        S.rule_timer = 8 + (p.flow * 4);
        Random tempRng(extraRng.next());
        for (int i = 0; i < 8; ++i) S.rule_sequence[i] = tempRng.next() % 5;
        S.rule_index = 0;
    }

    // back to the start of the phrase
    void reset() {
        cachedFlow = -1; cachedOrnament = -1; cachedLoopLength = -1;
        stepIndex = 0; displayStep = -1; gateTicks = 0;
        coolDown = coolDownMax = microCoolDown = microCoolDownMax = 0;
        gateOffsetPct = 0; slide = 0;
        cvTarget = cvCurrent = cvDelta = 0; slideCountDown = 0;
        microGateQueue.clear();
        retriggerArmed = false; retriggerCount = 0; retriggerPeriod = 0; retriggerLength = 0;
        trillCvTarget = 0; ratchetInterval = 0;
        gateOutput = false; cvOutput = 0;
        primeMaskCounter = 0; primeMaskState = 1; cachedPrimeParam = -1; cachedTimeMode = -1;
        currentMaskArrayIndex = 0; cachedMaskProgression = -1;
        S = State();
        initAlgorithm();
    }

    // Reseed: a fresh random variation with the same knob settings
    void reseed() {
        stepIndex = 0; coolDown = 0;
        rng = Random(rng.next());
        extraRng = Random(extraRng.next());
        switch (p.algorithm) {
        case 0: S.note = 0; break;
        case 1:
            S.b1 = rng.next() & 0x7; S.b2 = rng.next() & 0x7;
            S.b3 = extraRng.next() & 0x15;
            if (S.b3 >= 7) S.b3 -= 7; else S.b3 = 0;
            S.b3 -= 4;
            break;
        case 2:
            S.mode = (extraRng.next() % 7) * 2; S.countDown = 0;
            S.lowNote = rng.next() % 3; S.lastNote = S.lowNote; S.lastOctave = 0;
            S.highNote[0] = rng.next() % 7; S.highNote[1] = rng.next() % 5;
            break;
        case 3:
            S.history1 = rng.next() & 0x7; S.history3 = rng.next() & 0x7;
            for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) { S.matrix[i][j][0] = rng.next() % 8; S.matrix[i][j][1] = rng.next() % 8; }
            break;
        case 4:
            S.chordSeed = rng.next(); S.rngSeed = S.chordSeed;
            S.base = rng.next() % 3; S.dir = (rng.next() >> 7) % 2;
            break;
        case 5: initChip2(); S.chordLen = 3 + (p.flow >> 2); break;
        case 6: S.phase = 0; S.phaseSpeed = 0x08000000; S.phase2 = 0; S.lastWasHigh = 0; S.phaseSpeed2 = 0x02000000; break;
        case 7: S.pos = 0; break;
        case 8: initWindow(); break;
        case 9:
            S.burstLength = 2 + (rng.next() % 7); S.silenceLength = 4 + (cachedFlow % 13);
            S.clickDensity = (cachedOrnament * 16) & 0xff; S.mode = 0;
            S.silenceTimer = S.silenceLength; S.burstTimer = 0; S.noteIndex = 0;
            break;
        case 10: initGanz(cachedFlow); break;
        case 11:
            for (int i = 0; i < 4; i++) S.motif[i] = rng.next() % 7;
            S.breathPhase = rng.next() << 16; S.breathPattern = (cachedFlow >> 2) % 4;
            S.breathCycleLength = 4 + ((cachedOrnament >> 2) % 4); S.subBassCountdown = 0;
            break;
        case 12: initAphexTracks(); break;
        case 13: initAutechrePattern(); break;
        case 14:
            S.direction = 0; S.step_count = 3 + (rng.next() % 5);
            S.current_step = 0; S.chromatic_offset = 0; S.is_stepped = true;
            break;
        case 15: turingSR.seed(rng.next()); break;
        }
    }

    struct Ctx { int divisor, stepsPerBeat; bool isBeatStart; int tpb, loopLength, effectiveLoopLength, rotatedStep, ornament, subdivisions; };
    Ctx calculateContext() {
        Ctx c;
        c.divisor = effectiveDivisor();
        c.stepsPerBeat = c.divisor > 0 ? CONFIG_PPQN / c.divisor : 0;
        c.isBeatStart = c.stepsPerBeat > 0 && (stepIndex % c.stepsPerBeat) == 0;
        int stepTicks = c.divisor;
        c.tpb = std::max(1, (192 + stepTicks / 2) / stepTicks);
        c.loopLength = actualLoopLength();
        c.effectiveLoopLength = c.loopLength > 0 ? c.loopLength : 32;
        int rot = c.loopLength > 0 ? rotateValue() : 0;
        c.rotatedStep = c.loopLength > 0 ? ((stepIndex + rot + c.loopLength) % c.loopLength) : stepIndex;
        c.ornament = p.ornament;
        c.subdivisions = 4;
        if (c.ornament >= 5 && c.ornament <= 8) c.subdivisions = 3;
        else if (c.ornament >= 9 && c.ornament <= 12) c.subdivisions = 5;
        else if (c.ornament >= 13) c.subdivisions = 7;
        return c;
    }

    struct Result {
        int note = 0, octave = 0, velocity = 0;
        bool accent = false, slide = false;
        int gateRatio = 75, gateOffset = 0, trillCount = 1, beatSpread = 0, polyCount = 0;
        int noteOffsets[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        bool isSpatial = true;
    };

    Result generateTest(const Ctx &) {
        Result r; r.velocity = 255; r.gateRatio = 75;
        if ((int)rng.nextRange(100) < p.glide) r.slide = true;
        if (S.mode == 0) { r.octave = stepIndex % 5; r.note = 0; }
        else { r.octave = 0; r.note = S.note; S.note = (S.note + 1) % 12; }
        r.velocity = S.testVelocity;
        return r;
    }
    Result generateTritrance(const Ctx &) {
        Result r; r.velocity = 255;
        uint32_t gateLenRnd = rng.nextRange(100);
        if (gateLenRnd < 40) r.gateRatio = 50 + rng.nextRange(4) * 12;
        else if (gateLenRnd < 70) r.gateRatio = 100 + rng.nextRange(4) * 25;
        else r.gateRatio = 200 + rng.nextRange(9) * 25;
        if ((int)rng.nextRange(100) < p.glide) r.slide = true;
        int phase = (stepIndex + S.b2) % 3;
        switch (phase) {
        case 0:
            if (extraRng.nextBinary() && extraRng.nextBinary()) {
                S.b3 = extraRng.next() & 0x15;
                if (S.b3 >= 7) S.b3 -= 7; else S.b3 = 0;
                S.b3 -= 4;
            }
            r.octave = 0; r.note = S.b3 + 4;
            r.gateOffset = iclamp(10 - (int)rng.nextRange(10), 0, 100);
            break;
        case 1:
            r.octave = 1; r.note = S.b3 + 4;
            if (rng.nextBinary()) S.b2 = rng.next() & 0x7;
            r.gateOffset = iclamp(25 + (int)rng.nextRange(10), 0, 100);
            break;
        case 2:
            r.octave = 2; r.note = S.b1; r.velocity = 255; r.accent = true;
            if (rng.nextBinary()) S.b1 = (rng.next() >> 5) & 0x7;
            r.gateOffset = iclamp(40 + (int)rng.nextRange(10), 0, 100);
            r.trillCount = 3;
            r.noteOffsets[0] = -2; r.noteOffsets[1] = -1; r.noteOffsets[2] = 0;
            break;
        }
        if (!r.accent) r.velocity = rng.nextRange(256) / 2;
        return r;
    }
    Result generateMarkov(const Ctx &) {
        Result r; r.velocity = 255; r.gateRatio = 75;
        int idx = rng.nextBinary() ? 1 : 0;
        int newNote = S.matrix[S.history1][S.history3][idx];
        S.history1 = S.history3; S.history3 = newNote;
        r.note = newNote;
        r.octave = rng.nextBinary() ? 1 : 0;
        if (rng.nextRange(100) < 50) r.gateRatio = 100 + rng.nextRange(4) * 25; else r.gateRatio = 75;
        if (rng.nextBinary() && rng.nextBinary()) r.slide = true;
        r.accent = true;
        r.velocity = rng.nextRange(256) / 2 + 40;
        if (rng.nextBinary()) r.gateOffset = iclamp(10 - (int)rng.nextRange(10), 0, 100);
        else r.gateOffset = iclamp(15 + (int)rng.nextRange(10), 0, 100);
        if (rng.nextRange(256) < 20) { r.trillCount = 2; r.noteOffsets[0] = 0; r.noteOffsets[1] = 0; r.gateRatio = 50; }
        return r;
    }
    Result generateWobble(const Ctx &c) {
        Result r; r.velocity = 255;
        S.phaseSpeed = 0xFFFFFFFFu / uint32_t(std::max(1, c.effectiveLoopLength));
        S.phaseSpeed2 = 0xCFFFFFFFu / uint32_t(std::max(1, c.effectiveLoopLength / 4));
        S.phase += S.phaseSpeed;
        S.phase2 += S.phaseSpeed2;
        int rawPhase;
        if ((int)rng.nextRange(256) >= p.ornament * 16) {
            rawPhase = (S.phase2 >> 27) & 0x1F;
            r.note = rawPhase % 7; r.octave = 1 + rawPhase / 7;
            if (S.lastWasHigh == 0 && rng.nextRange(256) >= 200) r.slide = true;
            S.lastWasHigh = 1;
        } else {
            rawPhase = (S.phase >> 27) & 0x1F;
            r.note = rawPhase % 7; r.octave = rawPhase / 7;
            if (S.lastWasHigh == 1 && rng.nextRange(256) >= 200) r.slide = true;
            S.lastWasHigh = 0;
        }
        r.velocity = extraRng.nextRange(256) / 4;
        if (rng.nextRange(256) >= 50) r.accent = true;
        r.gateOffset = iclamp(int(((S.phase + S.phase2) >> 28) % 30), 0, 100);
        return r;
    }
    Result generateStomper(const Ctx &) {
        Result r; r.velocity = 255; r.gateRatio = 75;
        int accentoffs = 0, veloffset = 0;
        if (S.countDown > 0) {
            r.gateRatio = S.countDown * 25;
            r.velocity = 0;
            S.countDown--;
            r.note = S.lastNote; r.octave = S.lastOctave;
        } else {
            if (S.mode >= 14) S.mode = (extraRng.next() % 7) * 2;
            S.lowNote = rng.next() % 3;
            S.highNote[0] = rng.next() % 7;
            S.highNote[1] = rng.next() % 5;
            veloffset = 100;
            const int maxticklen = 2;
            switch (S.mode) {
            case 10: r.octave = 1; r.note = S.highNote[rng.next() % 2]; S.mode++; break;
            case 11:
                r.octave = 0; r.note = S.lowNote; r.slide = true;
                if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                S.mode = 14; break;
            case 12: r.octave = 0; r.note = S.lowNote; S.mode++; break;
            case 13:
                r.octave = 1; r.note = S.highNote[rng.next() % 2]; r.slide = true;
                if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                S.mode = 14; break;
            case 4: case 5:
                accentoffs = 100; r.octave = 0; r.note = S.lowNote;
                if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                S.mode = 14; break;
            case 0: case 1:
                r.octave = S.lastOctave; r.note = S.lastNote; veloffset = 0;
                if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                S.mode = 14; break;
            case 6: case 7: case 8: case 9: case 2: case 3:
                if (S.mode == 6 || S.mode == 7 || S.mode == 9 || S.mode == 2) accentoffs = 100;
                if (S.mode == 8 || S.mode == 3) {
                    r.octave = 1; r.note = S.highNote[rng.next() % 2];
                    if (S.mode == 8) S.mode++; else if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                    if (S.mode == 3) S.mode = 14;
                } else {
                    r.octave = 0; r.note = S.lowNote;
                    if (extraRng.nextBinary()) S.countDown = extraRng.next() % maxticklen;
                    S.mode = 14;
                }
                break;
            }
            S.lastNote = r.note; S.lastOctave = r.octave;
            r.velocity = extraRng.nextRange(256) / 4 + veloffset;
            if ((int)rng.nextRange(256) >= 50 + accentoffs) r.accent = true;
            r.beatSpread = 18;
        }
        return r;
    }
    Result generateChipArp1(const Ctx &c) {
        Result r; r.velocity = 255; r.gateRatio = 75;
        int chordpos = stepIndex % c.tpb;
        if (chordpos == 0) { r.trillCount = 3; r.noteOffsets[0] = 0; r.noteOffsets[1] = 2; r.noteOffsets[2] = 4; }
        if (chordpos == 0) {
            r.accent = true;
            S.rngSeed = S.chordSeed;
            if (rng.nextRange(256) >= 0x20) S.base = rng.next() % 3;
            if (rng.nextRange(256) >= 0xF0) S.dir = (rng.next() >> 7) % 2;
        }
        if (S.dir == 1) chordpos = c.tpb - chordpos - 1;
        Random chipRng(S.rngSeed);
        if (chipRng.nextRange(256) >= 0x20) r.accent = true;
        if (chipRng.nextRange(256) >= 0x80) r.slide = true;
        if (chipRng.nextRange(256) >= 0xD0) {
            r.velocity = 0;
        } else {
            r.note = chordpos * 2 + S.base;
            r.octave = 0;
            int rnd = (chipRng.next() >> 7) % 3;
            r.gateRatio = (4 + 6 * rnd) * 16;
        }
        int extraVel = c.rotatedStep == 0 ? 127 : 0;
        r.velocity = rng.nextRange(256) / 2 + extraVel;
        r.gateOffset = iclamp(stepIndex % 7, 0, 100);
        return r;
    }
    Result generateChipArp2(const Ctx &) {
        Result r; r.velocity = 255; r.gateRatio = 75; r.octave = 0;
        if (S.deadTime > 0) {
            S.deadTime--;
            r.velocity = 0;
        } else {
            int deadTimeAdd = 0;
            if (S.idx == S.chordLen) {
                S.idx = 0;
                S.len = (S.len - 1) & 0xff;
                r.accent = true;
                r.trillCount = 4;
                for (int i = 0; i < 4; i++) r.noteOffsets[i] = i * S.chordScaler;
                if (rng.nextRange(256) >= 200) deadTimeAdd = 1 + (rng.next() % 3);
                if (S.len == 0) {
                    S.rngSeed = rng.next();
                    S.chordScaler = (rng.next() % 3) + 2;
                    S.offset = rng.next() % 5;
                    S.len = ((rng.next() & 0x3) + 1) * 2;
                    if (rng.nextBinary()) S.dir = rng.nextBinary() ? 1 : 0;
                }
            }
            int scaleidx = S.idx % S.chordLen;
            if (S.dir) scaleidx = S.chordLen - scaleidx - 1;
            r.note = scaleidx * S.chordScaler + S.offset;
            S.idx = (S.idx + 1) & 0xff;
            S.deadTime = S.timeMult;
            if (S.deadTime > 0) r.gateRatio = (1 + S.timeMult) * 100;
            S.deadTime = (S.deadTime + deadTimeAdd) & 0xff;
        }
        Random chip2Rng(S.rngSeed);
        r.velocity = chip2Rng.nextRange(256);
        return r;
    }
    Result generateAphex(const Ctx &c) {
        Result r; r.velocity = 255;
        int modifierStep = stepIndex;
        if (c.subdivisions != 4) modifierStep = (stepIndex * c.subdivisions) / 4;
        int pos1 = c.rotatedStep % 4, pos2 = modifierStep % 3, pos3 = c.rotatedStep % 5;
        r.note = S.track1[pos1]; r.octave = 0; r.gateRatio = 75; r.velocity = 180;
        int modifier = S.track2[pos2];
        if (modifier == 1) { r.gateRatio = 25; r.velocity = 80; r.beatSpread = 60; }
        else if (modifier == 2) { r.gateRatio = 100; r.slide = true; }
        int bass = S.track3[pos3];
        if (bass > 0) { r.note = bass; r.octave = -1; r.gateRatio = 90; r.velocity = 255; r.accent = true; }
        int polyCollision = ((pos1 * 7) ^ (pos2 * 5) ^ (pos3 * 3)) % 12;
        if (polyCollision > 9) { r.beatSpread = 100; r.velocity = 255; }
        int polyRhythmFactor = (pos1 + pos2 + pos3) % 12;
        r.gateOffset = iclamp(polyRhythmFactor * 8, 0, 100);
        return r;
    }
    static int i8(int v) { v &= 0xff; return v > 127 ? v - 256 : v; }
    Result generateAutechre(const Ctx &c) {
        Result r; r.velocity = 255;
        int flow = p.flow;
        if (c.isBeatStart && c.subdivisions != 4) {
            r.beatSpread = 20 + flow * 5;
            r.polyCount = c.subdivisions;
            r.isSpatial = true;
            for (int i = 0; i < c.subdivisions && i < 8; i++) {
                int patternNote = S.pattern[(c.rotatedStep + i) % 8] % 12;
                int baseNote = S.pattern[c.rotatedStep % 8] % 12;
                r.noteOffsets[i] = i8(patternNote - baseNote);
            }
        }
        int patternVal = S.pattern[c.rotatedStep % 8];
        r.note = patternVal % 12;
        r.octave = patternVal / 12;
        r.gateOffset = iclamp((int)rng.nextRange(11), 0, 100);
        S.rule_timer--;
        r.velocity = 160; r.gateRatio = 75;
        if (S.rule_timer <= 0) {
            int rule = S.rule_sequence[S.rule_index];
            int intensity = p.power / 2;
            if (r.velocity > 0) { r.velocity = 255; r.accent = true; }
            int *P = S.pattern, t;
            if (rule == 0) { t = P[7]; for (int i = 7; i > 0; --i) P[i] = P[i - 1]; P[0] = t; }
            else if (rule == 1) { for (int i = 0; i < 4; ++i) { t = P[i]; P[i] = P[7 - i]; P[7 - i] = t; } }
            else if (rule == 2) { for (int i = 0; i < 8; ++i) { int o = P[i] / 12; int n = P[i] % 12; n = (6 - (n - 6) + 12) % 12; P[i] = i8(n + o * 12); } }
            else if (rule == 3) { for (int i = 0; i < 8; i += 2) { t = P[i]; P[i] = P[i + 1]; P[i + 1] = t; } }
            else if (rule == 4) { for (int i = 0; i < 8; ++i) { int o = P[i] / 12; int n = ((P[i] % 12) + intensity) % 12; P[i] = i8(n + o * 12); } }
            S.rule_timer = 8 + cachedFlow * 4;
            S.rule_index = (S.rule_index + 1) % 8;
        }
        return r;
    }
    Result generateStepwave(const Ctx &c) {
        Result r; r.velocity = 255;
        int flow = p.flow, ornament = p.ornament, dp;
        if (c.isBeatStart && c.subdivisions != 4) {
            r.beatSpread = 20 + flow * 5;
            r.polyCount = c.subdivisions;
            r.isSpatial = ornament >= 9;
            dp = iclamp((16 - flow) * 6, 10, 90);
            S.direction = (int)rng.nextRange(100) < dp ? -1 : 1;
            S.step_count = c.subdivisions;
            for (int i = 0; i < c.subdivisions && i < 8; i++) r.noteOffsets[i] = i * S.direction;
            r.slide = true;
        }
        dp = iclamp((16 - flow) * 6, 10, 90);
        int dir = (int)rng.nextRange(100) < dp ? -1 : 1;
        int stepSize = 2 + (stepIndex % 2);
        r.note = (stepIndex * dir * stepSize) % 7;
        if (r.note < 0) r.note += 7;
        r.velocity = 200;
        if ((int)rng.nextRange(100) < 20 + flow * 3) { r.octave = 2; r.velocity = 255; r.accent = true; }
        else r.octave = 0;
        if ((int)rng.nextRange(100) < p.glide) { r.slide = true; r.gateRatio = 100; }
        else r.gateRatio = 75;
        r.gateOffset = iclamp((int)rng.nextRange(7), 0, 100);
        return r;
    }
    Result generateScalewalker(const Ctx &c) {
        Result r; r.velocity = 255;
        int direction = (p.flow <= 7) ? -1 : 1;
        r.note = S.pos; r.octave = 0;
        r.velocity = (100 + p.power * 10) & 0xff;
        r.gateRatio = 75;
        bool shouldFirePolyGate = false;
        if (c.subdivisions != 4 && c.stepsPerBeat > 0) {   // (divisors over a beat: the firmware divides by zero here)
            int beatPosition = stepIndex % c.stepsPerBeat;
            int gateIndex = (beatPosition * c.subdivisions) / c.stepsPerBeat;
            int nextGateIndex = ((beatPosition + 1) * c.subdivisions) / c.stepsPerBeat;
            if (gateIndex != nextGateIndex) {
                shouldFirePolyGate = true;
                r.polyCount = 1; r.beatSpread = 100; r.isSpatial = true; r.slide = true;
                r.noteOffsets[0] = gateIndex * direction;
            }
        }
        int stepTrillCount = 1;
        if (p.stepTrill > 0) stepTrillCount = iclamp(1 + (p.stepTrill * 3) / 100, 1, 4);
        if (shouldFirePolyGate) {
            if (stepTrillCount > 1) {
                int baseOffset = r.noteOffsets[0];
                r.polyCount = stepTrillCount; r.beatSpread = 0; r.isSpatial = false;
                for (int i = 0; i < stepTrillCount && i < 8; i++) r.noteOffsets[i] = baseOffset + i * direction;
                r.slide = p.glide > 50;
            }
        } else if (stepTrillCount > 1) {
            r.polyCount = stepTrillCount; r.beatSpread = 0; r.isSpatial = false;
            for (int i = 0; i < stepTrillCount && i < 8; i++) r.noteOffsets[i] = i * direction;
            r.slide = p.glide > 50;
        } else if (c.subdivisions == 4) {
            r.polyCount = 0; r.beatSpread = 0; r.isSpatial = false; r.noteOffsets[0] = 0;
        } else {
            r.polyCount = 0; r.velocity = 0;
        }
        S.pos = (S.pos + direction + 7) % 7;
        return r;
    }
    Result generateMinimal(const Ctx &) {
        Result r; r.velocity = 0;
        if (S.mode == 0) {
            if (S.silenceTimer > 0) S.silenceTimer--;
            if (S.silenceTimer == 0) { S.mode = 1; S.burstTimer = S.burstLength; S.burstLength = 2 + (rng.next() % 7); }
        } else {
            if (S.burstTimer > 0) S.burstTimer--;
            if (S.burstTimer == 0) { S.mode = 0; S.silenceTimer = S.silenceLength; S.silenceLength = 4 + (p.flow % 13); }
        }
        if (S.mode == 0) {
            r.velocity = 0; r.note = 0; r.octave = 0; r.gateRatio = 25;
        } else {
            bool shouldPlay = true;
            if (S.clickDensity > 0) shouldPlay = (int)(rng.next() & 0xFF) < S.clickDensity;
            if (shouldPlay) {
                r.note = S.noteIndex % 7;
                r.octave = (S.noteIndex / 7) % 3;
                r.velocity = 128 + (extraRng.next() & 0x7F);
                r.gateRatio = iclamp(25 + p.gateLength / 4, 25, 50);
                int glideProb = p.glide / 5, trillProb = p.stepTrill / 10;
                r.slide = (int)(extraRng.next() % 100) < glideProb;
                if ((int)(extraRng.next() % 100) < trillProb) r.trillCount = 2 + (extraRng.next() % 3);
                else r.trillCount = 1;
                r.gateOffset = extraRng.next() % 6;
                S.noteIndex = (S.noteIndex + 1) % 21;
            } else {
                r.velocity = 0; r.note = 0; r.octave = 0; r.gateRatio = 25;
            }
        }
        return r;
    }
    Result generateWindow(const Ctx &c) {
        Result r; r.velocity = 0;
        uint32_t slowIncrement = c.effectiveLoopLength > 0 ? 0xFFFFFFFFu / uint32_t(c.effectiveLoopLength) : 0x08000000u;
        int fastDivisor = std::max(1, c.effectiveLoopLength / S.phaseRatio);
        uint32_t fastIncrement = c.effectiveLoopLength > 0 ? 0xFFFFFFFFu / uint32_t(fastDivisor) : 0x20000000u;
        uint32_t oldSlowPhase = S.slowPhase;
        S.slowPhase += slowIncrement;
        S.fastPhase += fastIncrement;
        if (stepIndex % 96 == 0 && stepIndex > 0) S.phaseRatio = 3 + (rng.next() & 0x3);
        if (stepIndex % 64 == 0 && stepIndex > 0) S.ghostThreshold = rng.next() & 0x1f;
        bool isAccent = S.slowPhase < oldSlowPhase;
        bool isGhost = (int)((S.fastPhase >> 27) & 0x1F) > S.ghostThreshold;
        int voiceA = (S.fastPhase >> 28) & 1;
        if (isAccent) {
            r.note = S.noteMemory;
            r.octave = 2 + (extraRng.next() % 2);
            r.velocity = 100 + (extraRng.next() & 0x3F);
            r.gateRatio = 150 + (extraRng.next() % 26);
            r.slide = true;
        } else if (isGhost) {
            r.note = (S.fastPhase >> 25) & 0x7;
            r.octave = 0;
            r.velocity = 5 + (extraRng.next() & 0x1F);
            r.gateRatio = 50 + (extraRng.next() % 26);
            r.slide = false;
        } else if (voiceA) {
            Random markovRng(extraRng.next() + uint32_t(S.noteHistoryW) + uint32_t(S.noteMemory));
            int step = int(markovRng.next() % 3) - 1;
            int newNote = (S.noteMemory + step + 7) % 7;
            S.noteHistoryW = S.noteMemory; S.noteMemory = newNote;
            r.note = newNote; r.octave = 0;
            r.velocity = 40 + (extraRng.next() % 86);
            r.gateRatio = 75;
            r.slide = (extraRng.next() % 100) < 25;
        } else {
            r.note = (S.fastPhase >> 25) & 0x7;
            r.octave = 0;
            r.velocity = 40 + (extraRng.next() % 86);
            r.gateRatio = 75;
            r.slide = false;
        }
        return r;
    }
    Result generateBlake(const Ctx &c) {
        Result r; r.velocity = 0;
        uint32_t breathIncrement = c.effectiveLoopLength > 0 ? 0xFFFFFFFFu / uint32_t(c.effectiveLoopLength * 8) : 0x02000000u;
        S.breathPhase += breathIncrement;
        if (stepIndex % 96 == 0 && stepIndex > 0) S.breathPattern = rng.next() % 4;
        if (stepIndex % 64 == 0 && stepIndex > 0) S.breathCycleLength = 4 + (rng.next() % 4);
        int breathPos = stepIndex % S.breathCycleLength;
        enum { REAL, GHOST, WHISPER };
        int art = REAL;
        switch (S.breathPattern) {
        case 0: art = breathPos == 3 ? GHOST : REAL; break;
        case 1: art = breathPos == 1 ? WHISPER : (breathPos == 3 ? GHOST : REAL); break;
        case 2: art = breathPos == 0 ? REAL : (breathPos == 2 ? WHISPER : GHOST); break;
        case 3: art = breathPos < 2 ? REAL : (breathPos == 2 ? GHOST : WHISPER); break;
        }
        r.note = S.motif[stepIndex % 4];
        r.octave = 0;
        if (S.subBassCountdown > 0) {
            S.subBassCountdown--;
            r.octave = -1; r.note = 0; r.velocity = 255; r.accent = true; r.gateRatio = 100;
            r.slide = S.subBassCountdown == 0;
            return r;
        }
        if (c.isBeatStart) {
            int dropChance = p.power * 6;
            if ((int)(rng.next() % 100) < dropChance) S.subBassCountdown = 2 + (rng.next() % 3);
        }
        switch (art) {
        case REAL: {
            int breathBias = (S.breathPhase >> 24) & 0xFF;
            r.velocity = 128 + breathBias / 2;
            r.gateRatio = 75;
            if ((extraRng.next() % 100) < 25) r.octave = 1;
            break;
        }
        case GHOST: r.octave = 2; r.velocity = 20 + (extraRng.next() & 0x0F); r.gateRatio = 40; break;
        case WHISPER: r.velocity = 0; r.gateRatio = 25; break;
        }
        r.gateOffset = extraRng.next() % 5;
        return r;
    }
    Result generateGanz(const Ctx &c) {
        Result r; r.velocity = 0;
        uint32_t incrementA = c.effectiveLoopLength > 0 ? 0xFFFFFFFFu / uint32_t(c.effectiveLoopLength) : 0x08000000u;
        uint32_t oldPhaseA = S.phaseA;
        S.phaseA += incrementA;
        S.phaseB += incrementA * 5u;
        S.phaseC += incrementA * 7u;
        if (stepIndex % 96 == 0 && stepIndex > 0) S.selectMode = rng.next() % 4;
        int tupletPos = (S.phaseB >> 27) % 5;
        if (!(tupletPos <= 2)) { r.velocity = 0; r.gateRatio = 25; return r; }
        if (S.phraseSkipCount > 0) { S.phraseSkipCount--; r.velocity = 0; return r; }
        if ((int)(S.phaseC >> 28) < S.skipDecimator * 2) {
            S.phraseSkipCount = 1 + (rng.next() % 4);
            r.velocity = 0;
            return r;
        }
        int selectedNote = 0;
        switch (S.selectMode) {
        case 0: selectedNote = (S.noteHistoryG[0] + (int(extraRng.next() % 3) - 1) + 7) % 7; break;
        case 1: selectedNote = (S.phaseA >> 25) % 7; break;
        case 2: selectedNote = S.noteHistoryG[stepIndex % 3]; break;
        case 3: selectedNote = (((S.phaseA >> 24) ^ (S.phaseC >> 24)) & 0xFF) % 7; break;
        }
        S.noteHistoryG[2] = S.noteHistoryG[1];
        S.noteHistoryG[1] = S.noteHistoryG[0];
        S.noteHistoryG[0] = selectedNote;
        r.note = selectedNote;
        r.octave = (S.phaseC >> 29) % 3;
        if (stepIndex % 8 == 0) S.velocitySample = 64 + (extraRng.next() & 0xBF);
        r.velocity = S.velocitySample;
        if (S.phaseA < oldPhaseA) {
            r.accent = true; r.velocity = 255; r.gateRatio = 125; r.slide = true;
        } else {
            r.gateRatio = 60 + tupletPos * 15;
            r.slide = (int)(extraRng.next() % 100) < p.ornament * 6;
        }
        r.gateOffset = ((S.phaseB >> 26) & 0x1F) % 20;
        return r;
    }
    Result generateTuring(const Ctx &) {
        Result r; r.velocity = 255;
        r.gateRatio = iclamp(50 + p.gateLength * 3, 50, 400);
        if ((int)rng.nextRange(100) < p.glide) r.slide = true;
        int prob = ((p.power * 255) / 16) & 0xff;
        uint32_t v = turingSR.clock(rng, prob);
        uint32_t range = 1 + p.ornament;
        int idx = v % range;
        r.note = idx % 12;
        r.octave = idx / 12;
        return r;
    }

    Result generateStep() {
        Ctx c = calculateContext();
        switch (p.algorithm) {
        case 1: return generateTritrance(c);
        case 2: return generateStomper(c);
        case 3: return generateMarkov(c);
        case 4: return generateChipArp1(c);
        case 5: return generateChipArp2(c);
        case 6: return generateWobble(c);
        case 7: return generateScalewalker(c);
        case 8: return generateWindow(c);
        case 9: return generateMinimal(c);
        case 10: return generateGanz(c);
        case 11: return generateBlake(c);
        case 12: return generateAphex(c);
        case 13: return generateAutechre(c);
        case 14: return generateStepwave(c);
        case 15: return generateTuring(c);
        default: return generateTest(c);
        }
    }

    double scaleToVolts(int noteIndex, int octave) const {
        ScaleView scale(p.scale, p.scaleRotate);
        int totalDegree = noteIndex + octave * scale.notesPerOctave() + p.transpose;
        return scale.noteToVolts(totalDegree) + p.rootNote / 12.0 + p.octave;
    }
    static int midiVelocity(int v, bool acc) {
        if (acc) return 127;
        return iclamp(jsround((v & 0xff) * 127 / 255.0), 1, 127);
    }

    void setGate(bool on) { gateOutput = on; }

    bool tick(uint32_t tick) {
        stepFired = false;
        if (cachedAlgorithm != p.algorithm || cachedFlow != p.flow || cachedOrnament != p.ornament || cachedLoopLength != p.loopLength)
            initAlgorithm();

        int divisor = effectiveDivisor();

        // start delay, extended by the mask's prime value
        long extendedStartTicks = long(p.start) * divisor;
        int maskParameter = p.maskParameter;
        if (maskParameter > 0 && maskParameter < 15) extendedStartTicks += TU_MASK_VALUES[(maskParameter - 1) % 14];
        if ((long)tick < extendedStartTicks) { setGate(false); return false; }
        tick -= (uint32_t)extendedStartTicks;

        // prime-number mask: hides ticks from the algorithm to carve rests and polyrhythms
        bool tickAllowed = true;
        int maskParam = p.maskParameter, timeMode = p.timeMode, maskProgression = p.maskProgression;
        if (cachedPrimeParam != maskParam || cachedTimeMode != timeMode || cachedMaskProgression != maskProgression) {
            primeMaskCounter = 0;
            primeMaskState = 1;
            currentMaskArrayIndex = (maskParam > 0 && maskParam < 15) ? (maskParam - 1) % 14 : 0;
            cachedPrimeParam = maskParam; cachedTimeMode = timeMode; cachedMaskProgression = maskProgression;
        }
        if (maskParam == 0) {
            tickAllowed = true;
        } else if (maskParam == 15) {
            tickAllowed = false;
        } else {
            int effectiveParam = TU_MASK_VALUES[currentMaskArrayIndex % 14];
            int inc = maskProgression == 1 ? 1 : (maskProgression == 2 ? 5 : 7);
            if (timeMode == 0) {
                primeMaskCounter++;
                if (primeMaskCounter >= effectiveParam) {
                    primeMaskState = 1 - primeMaskState;
                    if (maskProgression > 0) currentMaskArrayIndex = (currentMaskArrayIndex + inc) % 14;
                    primeMaskCounter = 0;
                }
                tickAllowed = primeMaskState == 1;
            } else {
                int q = 192, targetDuration;
                if (timeMode == 1) targetDuration = q - effectiveParam;
                else if (timeMode == 2) targetDuration = (q * 3) / 2 - effectiveParam;
                else if (timeMode == 3) targetDuration = q * 3 - effectiveParam;
                else targetDuration = effectiveParam;
                primeMaskCounter++;
                if (primeMaskCounter <= effectiveParam) tickAllowed = false;
                else if (primeMaskCounter <= effectiveParam + targetDuration) tickAllowed = true;
                else {
                    if (maskProgression > 0) currentMaskArrayIndex = (currentMaskArrayIndex + inc) % 14;
                    primeMaskCounter = 0;
                    tickAllowed = false;
                }
            }
        }
        if (!tickAllowed) {
            setGate(false);
            cvOutput = cvCurrent;
            return false;
        }

        // finite loop: restart the seeds every N steps so the phrase repeats
        int loopLength = actualLoopLength();
        uint32_t resetDivisor = loopLength > 0 ? uint32_t(loopLength * divisor) : 0;
        uint32_t relativeTick = resetDivisor == 0 ? tick : tick % resetDivisor;
        if (resetDivisor > 0 && relativeTick == 0) { initAlgorithm(); stepIndex = 0; }
        bool stepTrigger = (relativeTick % divisor) == 0;

        // play the scheduled micro gates (notes, trills, polyrhythm hits)
        while (!microGateQueue.empty() && tick >= microGateQueue.front().tick) {
            MicroGate ev = microGateQueue.pop();
            if (ev.gate) { cvOutput = ev.cvTarget; velocity = ev.velocity; slideActive = ev.slide; }
            setGate(ev.gate);
        }
        if (gateTicks > 0) { gateTicks--; if (gateTicks == 0) setGate(false); }
        if (slideCountDown > 0) {
            cvCurrent += cvDelta;
            slideCountDown--;
            if (slideCountDown == 0) cvCurrent = cvTarget;
            cvOutput = cvCurrent;
        }
        if (!stepTrigger) return false;

        stepIndex = relativeTick / divisor;
        displayStep = stepIndex;

        Result result = generateStep();

        // timing offset: knob 0% = straight, 50% = algorithm's own feel, 100% = doubled
        gateOffsetPct = iclamp((result.gateOffset * p.gateOffset * 2) / 100, 0, 100);

        // trills requested by the algorithm fire with Step Trill probability
        if (result.trillCount > 1 && p.stepTrill > 0) {
            if ((int)uiRng.nextRange(100) < p.stepTrill) {
                result.polyCount = result.trillCount;
                result.isSpatial = false;
                result.beatSpread = 0;
            }
        }

        // POWER = density, Skew bends it across the loop
        int power = p.power, skew = p.skew;
        if (loopLength > 0 && skew != 0) {
            int currentPos = stepIndex % loopLength;
            int off = -skew + (2 * skew * currentPos) / std::max(1, loopLength - 1);
            power = std::max(0, std::min(16, power + off));
        }
        int baseCooldown = 17 - power;
        if (baseCooldown < 1) baseCooldown = 1;
        coolDownMax = baseCooldown;
        microCoolDownMax = baseCooldown;
        if (coolDown > 0) { coolDown--; if (coolDown > coolDownMax) coolDown = coolDownMax; }
        if (microCoolDown > 0) { microCoolDown--; if (microCoolDown > microCoolDownMax) microCoolDown = microCoolDownMax; }

        int vel = result.velocity & 0xff;
        int midiVel = midiVelocity(vel, result.accent);
        retriggerArmed = false;

        if (result.polyCount > 0) {
            // micro-sequencing: N gates inside the step, each with its own pitch
            int tupleN = result.polyCount;
            int spacing = divisor / tupleN;
            int baseGateLen = (spacing * result.gateRatio) / 100;
            int gateLen = (baseGateLen * p.gateLength) / 50;
            if (gateLen < 1) gateLen = 1;
            int gateOffsetTicks = (divisor * gateOffsetPct) / 100;
            uint32_t baseTick = tick + gateOffsetTicks;
            bool stepAllowed = false;
            if (vel == 0 || power == 0) stepAllowed = false;
            else if (result.accent) stepAllowed = true;
            else if (coolDown == 0) { stepAllowed = true; coolDown = coolDownMax; }
            else if (vel / 16 >= coolDown) { stepAllowed = true; coolDown = coolDownMax; }
            if (stepAllowed) {
                int velDensity = vel / 16;
                for (int i = 0; i < tupleN; i++) {
                    bool microAllowed = false;
                    if (microCoolDown == 0) { microAllowed = true; microCoolDown = microCoolDownMax; }
                    else if (velDensity >= microCoolDown * 2) { microAllowed = true; microCoolDown = microCoolDownMax; }
                    if (microAllowed) {
                        double volts = scaleToVolts(result.note + result.noteOffsets[i], result.octave);
                        uint32_t at = baseTick + i * spacing;
                        microGateQueue.push({at, true, volts, midiVel, result.slide && p.glide > 0});
                        microGateQueue.push({at + (uint32_t)gateLen, false, volts, 0, false});
                        stepFired = true;
                    }
                }
            }
            retriggerArmed = true;
        } else if (result.beatSpread > 0) {
            // old-style ratchet: the gate fires twice more, a whole tone up
            int chance = (result.beatSpread * p.trill) / 100;
            if ((int)uiRng.nextRange(100) < chance) {
                retriggerArmed = true;
                retriggerCount = 2;
                retriggerPeriod = divisor / 3;
                retriggerLength = retriggerPeriod / 2;
                ratchetInterval = 0;
                retriggerVelocity = midiVel;
            }
        }

        slide = 0;
        if (result.slide && p.glide > 0) slide = 1 + p.glide / 30;

        // density gate: accents always play, otherwise cooldown or loud velocity
        bool densityGate = false, accentActive = false;
        if (vel == 0 || power == 0) densityGate = false;
        else if (result.accent) { densityGate = true; accentActive = true; }
        else if (coolDown == 0) densityGate = true;
        else if (vel / 16 >= coolDown) densityGate = true;

        double noteVolts = scaleToVolts(result.note, result.octave);
        if (densityGate) {
            coolDown = coolDownMax;
            int gOffTicks = (divisor * gateOffsetPct) / 100;
            int baseLen = (divisor * result.gateRatio) / 100;
            int gateLengthTicks = (baseLen * p.gateLength) / 50;
            if (gateLengthTicks < 1) gateLengthTicks = 1;
            if (accentActive) gateLengthTicks = (gateLengthTicks * 3) / 2;
            if (!retriggerArmed) {
                microGateQueue.push({tick + gOffTicks, true, noteVolts, midiVel, slide > 0});
                microGateQueue.push({tick + gOffTicks + gateLengthTicks, false, noteVolts, 0, false});
            }
            if (slide > 0) {
                cvTarget = noteVolts;
                int slideTicks = slide * 12;
                cvDelta = (cvTarget - cvCurrent) / slideTicks;
                slideCountDown = slideTicks;
            } else {
                cvCurrent = noteVolts;
                cvOutput = noteVolts;
                slideCountDown = 0;
            }
            if (retriggerArmed) { gateTicks = retriggerLength; trillCvTarget = noteVolts; }
            accent = result.accent;
            stepFired = true;
        } else if (vel > 0) {
            cvCurrent = noteVolts;
            cvOutput = noteVolts;
        }
        return true;
    }

    // once per millisecond: glides and the old-style ratchets (Trill)
    void update() {
        if (slideCountDown > 0) {
            slideCountDown--;
            cvCurrent += cvDelta;
            if (!retriggerArmed) cvOutput = cvCurrent;
        }
        if (retriggerArmed) {
            if (gateOutput) {
                if (gateTicks > 0) gateTicks--;
                else { setGate(false); gateTicks = retriggerPeriod - retriggerLength; }
            } else {
                if (gateTicks > 0) gateTicks--;
                else if (retriggerCount > 0) {
                    retriggerCount--;
                    gateTicks = retriggerLength;
                    cvOutput += (ratchetInterval != 0 ? ratchetInterval : 2) / 12.0;
                    velocity = retriggerVelocity ? retriggerVelocity : 100;
                    setGate(true);
                } else {
                    retriggerArmed = false;
                }
            }
        }
    }
};

} // namespace xf
