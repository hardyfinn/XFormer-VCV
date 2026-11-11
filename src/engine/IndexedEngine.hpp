// INDEXED engine (firmware engine/IndexedTrackEngine.cpp, model/IndexedSequence.h)
//
// A list of up to 48 steps. Every step has its own duration (in ticks, times the
// Divisor), its own gate length and a scale-note index. Durations don't have to add
// up to a bar, so the pattern drifts against the beat. Two modulation inputs (A and
// B) can stretch durations, gates or transpose notes on chosen step groups.
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int INDEXED_MAX_STEPS = 48;
static const int INDEXED_MAX_DURATION = 1023;
static const int INDEXED_GATE_MAX = 1023;

struct IndexedStep {
    int duration = 0, gateLength = 0, noteIndex = 0, slide = 0, groupMask = 0;
    // the model keeps the gate inside the duration whenever either changes
    void setDuration(int v) { duration = iclamp(v, 0, INDEXED_MAX_DURATION); setGateLength(gateLength); }
    void setGateLength(int v) {
        v = iclamp(v, 0, INDEXED_GATE_MAX);
        if (duration == 0) v = 0;
        else if (v > duration) v = duration;
        gateLength = v;
    }
};

enum { IDX_TARGET_DURATION, IDX_TARGET_GATE, IDX_TARGET_NOTE };
enum { IDX_COMBINE_ATOB, IDX_COMBINE_MUX, IDX_COMBINE_MIN, IDX_COMBINE_MAX };
static const char *const IDX_TARGET_NAMES[] = {"Duration", "Gate", "Note"};
static const char *const IDX_COMBINE_NAMES[] = {"A to B", "Mux", "Min", "Max"};
static const char *const IDX_SOURCE_NAMES[] = {"Off", "Mod A", "Mod B"};

// targetGroups: 0 = all steps, 0x10 = steps in no group, else a mask of groups A-D
struct IndexedRoute { int targetGroups = 0, targetParam = IDX_TARGET_DURATION, amount = 100, source = 0; };

struct IndexedParams {
    int divisor = 1, clockMult = 100, loop = 1, runMode = 0, activeLength = 5, scale = 1, rootNote = 0;
    int scaleRotate = 0, firstStep = 0, syncMode = 0, resetMeasure = 0, octave = 0, transpose = 0;
    int routeCombine = 0;
    float modA = 0, modB = 0;    // -100..100 (the modulation inputs)
};

struct IndexedEngine {
    IndexedParams p;
    IndexedRoute routeA, routeB;
    IndexedStep steps[INDEXED_MAX_STEPS];
    int measureTicks = 768;
    bool quirks = false;

    // outputs
    bool gateOutput = false;
    float cvOutput = 0, cvTarget = 0;
    bool slideActive = false;
    bool retrigger = false;       // true for one tick when a new note follows straight on
    int displayStep = -1;
    bool stepFired = false;

    Random rng;
    SequenceState state;
    uint32_t curTick = 0;
    int stepsRemaining = 0, currentStepIndex = 0;
    double stepTimer = 0, gateTimer = 0;
    uint32_t effectiveStepDuration = 0;
    bool running = true, pendingTrigger = false, prevGate = false, retrig = false;
    float prevCv = 0;
    uint32_t lastFreeTickPos = 0;

    IndexedEngine() {
        // the module's empty pattern: 5 silent steps spread over one bar
        for (int i = 0; i < 5; i++) steps[i].setDuration(i < 3 ? 154 : 153);
        reset();
    }

    bool setParam(const std::string &name, double value) {
        if (name == "modA") { p.modA = clamp((float)value, -100.f, 100.f); return true; }
        if (name == "modB") { p.modB = clamp((float)value, -100.f, 100.f); return true; }
        int v = jsround(value);
        if (name.size() > 6 && name.compare(0, 5, "route") == 0 && (name[5] == 'A' || name[5] == 'B')) {
            IndexedRoute &r = name[5] == 'A' ? routeA : routeB;
            std::string k = name.substr(6);
            if (k == "Source") r.source = iclamp(v, 0, 2);
            else if (k == "Groups") r.targetGroups = iclamp(v, 0, 0x1f);
            else if (k == "Target") r.targetParam = iclamp(v, 0, 2);
            else if (k == "Amount") r.amount = iclamp(v, -200, 200);
            else return false;
            return true;
        }
        if (name == "activeLength") {
            p.activeLength = iclamp(v, 1, INDEXED_MAX_STEPS);
            if (p.firstStep >= p.activeLength) p.firstStep = p.activeLength - 1;
            return true;
        }
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"loop", &p.loop, 0, 1},
            {"runMode", &p.runMode, 0, 5}, {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1},
            {"rootNote", &p.rootNote, 0, 11}, {"scaleRotate", &p.scaleRotate, 0, 31},
            {"firstStep", &p.firstStep, 0, 47}, {"syncMode", &p.syncMode, 0, 1}, {"resetMeasure", &p.resetMeasure, 0, 128},
            {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -60, 60}, {"routeCombine", &p.routeCombine, 0, 3}};
        for (auto &r : ranges) {
            if (name != r.n) continue;
            v = iclamp(v, r.lo, r.hi);
            if (name == "firstStep") v = iclamp(v, 0, p.activeLength - 1);
            *r.f = v;
            return true;
        }
        return false;
    }

    // field: 0 duration, 1 gateLength, 2 noteIndex, 3 slide, 4 groupMask
    void setStep(int i, int field, int value) {
        if (i < 0 || i >= INDEXED_MAX_STEPS) return;
        IndexedStep &s = steps[i];
        switch (field) {
        case 0: s.setDuration(value); break;
        case 1: s.setGateLength(value); break;
        case 2: s.noteIndex = iclamp(value, -63, 63); break;
        case 3: s.slide = value ? 1 : 0; break;
        case 4: s.groupMask = value & 15; break;
        }
    }
    static int fieldIndex(const std::string &k) {
        const char *names[] = {"duration", "gateLength", "noteIndex", "slide", "groupMask"};
        for (int i = 0; i < 5; i++) if (k == names[i]) return i;
        return -1;
    }
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "step") { setStep(std::stoi(t[1]), fieldIndex(t[2]), jsround(std::stod(t[3]))); return true; }
        if (t[0] == "playMode" || t[0] == "slideTime" || t[0] == "cvUpdate") return true;
        return setParam(t[0], std::stod(t[1]));
    }

    void resetSequenceState() {
        state = SequenceState();
        stepsRemaining = std::max(0, p.activeLength - 1);
        currentStepIndex = 0;
        stepTimer = 0; gateTimer = 0;
        effectiveStepDuration = 0;
        cvOutput = 0; cvTarget = 0;
        running = true;
        slideActive = false;
        lastFreeTickPos = curTick;
        if (p.activeLength > 0) {
            state.advanceFree(p.runMode, 0, p.activeLength - 1, rng);
            currentStepIndex = state.step;
        }
        pendingTrigger = true;
    }

    void reset() {
        rng = Random(0);
        curTick = 0;
        prevGate = false; prevCv = 0; retrig = false;
        gateOutput = false;
        displayStep = -1;
        resetSequenceState();
    }
    void barBoundary() {}

    bool tick(uint32_t tick) {
        curTick = tick;
        stepFired = false;
        if (p.syncMode == 1) {
            uint32_t resetDivisor = p.resetMeasure * measureTicks;
            if (resetDivisor > 0 && tick % resetDivisor == 0) resetSequenceState();
        }
        if (!running) { emit(); return false; }
        if (pendingTrigger) { triggerStep(); pendingTrigger = false; }

        uint32_t stepDuration = effectiveStepDuration & 0xffff;
        float clockMult = float(p.clockMult) * 0.01f;
        if (stepDuration == 0) {
            // zero-length steps: rush through them
            advanceStep();
            triggerStep();
            emit();
            return false;
        }
        uint32_t tickPos = tick;
        double delta = tickPos >= lastFreeTickPos ? double(tickPos - lastFreeTickPos) : 0;
        lastFreeTickPos = tickPos;
        double scaled = delta * clockMult;
        if (effectiveStepDuration > 0) {
            double maxDelta = double(effectiveStepDuration) * 2;
            if (scaled > maxDelta) scaled = maxDelta;
        }
        if (gateTimer > 0) {
            gateTimer -= scaled;
            if (gateTimer <= 0) gateTimer = 0;
        }
        stepTimer += scaled;
        if (stepTimer >= stepDuration) {
            advanceStep();
            triggerStep();
        }
        emit();
        return false;
    }

    void update() { cvOutput = cvTarget; }

    void advanceStep() {
        int n = p.activeLength;
        if (n <= 0) return;
        if (!p.loop && stepsRemaining <= 0) { running = false; stepTimer = 0; return; }
        int skip = 0;
        while (skip < n) {
            state.advanceFree(p.runMode, 0, n - 1, rng);
            currentStepIndex = state.step;
            if (!p.loop) stepsRemaining--;
            int eff = (currentStepIndex + p.firstStep) % n;
            if (steps[eff].duration > 0) break;
            skip++;
            if (!p.loop && stepsRemaining <= 0) { running = false; stepTimer = 0; return; }
        }
        stepTimer = 0;
    }

    static bool stepMatches(const IndexedStep &step, const IndexedRoute &r) {
        int g = r.targetGroups;
        return g == 0 || (g == 0x10 && step.groupMask == 0) || (step.groupMask & g) != 0;
    }

    struct Vals { int duration, gate, note; };
    float amountFor(const IndexedRoute &r, int target, float cv, float scaleSize) const {
        if (target == IDX_TARGET_DURATION) return cv * (float(r.amount) * 0.01f);
        if (target == IDX_TARGET_NOTE) return cv * ((float(r.amount) * 0.01f) * scaleSize);
        return cv * float(r.amount);
    }
    // route modulation for one target (IndexedTrackEngine.cpp applyStepModulation)
    void applyMod(int target, float cvA, float cvB, bool aActive, bool bActive, bool combine, int scaleSize, Vals &v) const {
        float modA = aActive && routeA.targetParam == target ? amountFor(routeA, target, cvA, float(scaleSize)) : 0.f;
        float modB = bActive && routeB.targetParam == target ? amountFor(routeB, target, cvB, float(scaleSize)) : 0.f;
        float fin;
        if (combine && aActive && bActive) {
            int mode = p.routeCombine;
            if (mode == IDX_COMBINE_MUX) fin = 0.5f * (modA + modB);
            else if (mode == IDX_COMBINE_MIN) fin = std::min(modA, modB);
            else if (mode == IDX_COMBINE_MAX) fin = std::max(modA, modB);
            else fin = modA;
        } else fin = modA + modB;
        if (fin == 0) return;
        if (target == IDX_TARGET_DURATION) {
            v.duration = iclamp(lroundi(float(v.duration) * (1.f + fin)), 0, INDEXED_MAX_DURATION);
        } else if (target == IDX_TARGET_GATE) {
            int t = iclamp(v.gate + lroundi(fin), 0, INDEXED_GATE_MAX);
            if (v.duration > 0) t = std::min(t, v.duration);
            v.gate = t;
        } else {
            v.note = iclamp((int)std::trunc(float(v.note) + fin), -63, 64);
        }
    }

    ScaleView scaleView() const { return ScaleView(p.scale, p.scaleRotate); }
    float noteToVolts(int noteIndex) const {
        ScaleView s = scaleView();
        int shift = p.octave * s.notesPerOctave() + p.transpose;
        return float(float(s.noteToVolts(noteIndex + shift)) + float(p.rootNote) * (1.f / 12));
    }

    void triggerStep() {
        int eff = (currentStepIndex + p.firstStep) % p.activeLength;
        const IndexedStep &step = steps[eff];
        Vals v{step.duration, step.gateLength, step.noteIndex};
        bool aActive = routeA.source != 0 && stepMatches(step, routeA);
        bool bActive = routeB.source != 0 && stepMatches(step, routeB);
        if (aActive || bActive) {
            float cvA = p.modA * 0.01f, cvB = p.modB * 0.01f;
            float aVal = routeA.source == 1 ? cvA : cvB, bVal = routeB.source == 1 ? cvA : cvB;
            int size = scaleView().notesPerOctave();
            bool combine = p.routeCombine != IDX_COMBINE_ATOB && aActive && bActive && routeA.targetParam == routeB.targetParam;
            for (int target = 0; target < 3; target++) applyMod(target, aVal, bVal, aActive, bActive, combine, size, v);
        }
        if (v.duration == 0) v.gate = 0;
        else if (v.gate > v.duration) v.gate = v.duration;

        uint32_t effectiveDuration = uint32_t(v.duration * p.divisor);
        uint32_t gateTicks = uint32_t(v.gate * p.divisor);
        if (effectiveDuration > 0) gateTicks = std::min(gateTicks, effectiveDuration);
        if (gateTicks > 0 && gateTimer > 0 && !step.slide) retrig = true;   // a new note, not a tie
        gateTimer = gateTicks;
        effectiveStepDuration = effectiveDuration;
        if (gateTicks > 0) {
            cvTarget = noteToVolts(v.note);
            slideActive = step.slide != 0;
            stepFired = true;
        } else slideActive = false;
        displayStep = eff;
    }

    void emit() {
        bool g = running && gateTimer > 0;
        retrigger = g && retrig && prevGate;
        retrig = false;
        prevGate = g; prevCv = cvTarget;
        gateOutput = g;
    }

    // a brand-new module: 8 steps over one bar, a long-short rhythm with a small melody
    void initFresh() {
        int durs[] = {144, 48, 96, 96, 144, 48, 96, 96}, notes[] = {0, 2, 4, 2, 5, 4, 2, 7};
        for (int i = 0; i < INDEXED_MAX_STEPS; i++) {
            IndexedStep &s = steps[i];
            s = IndexedStep();
            if (i < 8) { s.setDuration(durs[i]); s.setGateLength(jsround(durs[i] * 0.6)); s.noteIndex = notes[i]; }
        }
        p.activeLength = 8;
        if (p.firstStep >= 8) p.firstStep = 0;
    }
};

} // namespace xf
