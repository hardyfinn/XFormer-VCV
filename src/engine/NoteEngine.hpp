// NOTE engine (engine/NoteTrackEngine.cpp, Linear mode).
// A Metropolix-style step sequencer: every step has its own gate, probability,
// length, retrigger, note, variation and condition, plus PULSE COUNT (how many
// clock pulses the step lasts), GATE MODE (which of those pulses fire) and the
// ACCUMULATOR (a counter that transposes chosen steps a little further each time).
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int NOTE_STEP_COUNT = 64;
static const int CONDITION_LOOP = 7, CONDITION_NOTLOOP = 42, CONDITION_LAST = 77;
static const int COND_OFFSET[35] = {0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 7};
static const int COND_BASE[35] = {2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8};

inline std::string conditionName(int c) {
    static const char *const first[] = {"Off", "Fill", "!Fill", "Pre", "!Pre", "First", "!First"};
    if (c < 7) return first[c];
    bool inv = c >= CONDITION_NOTLOOP;
    int i = c - (inv ? CONDITION_NOTLOOP : CONDITION_LOOP);
    return std::string(inv ? "!" : "") + std::to_string(COND_OFFSET[i] + 1) + ":" + std::to_string(COND_BASE[i]);
}

// per-step fields, in this order (also the save order)
enum NoteField {
    NF_GATE, NF_GATE_PROB, NF_GATE_OFFSET, NF_SLIDE, NF_RETRIG, NF_RETRIG_PROB, NF_LENGTH,
    NF_LEN_VAR_RANGE, NF_LEN_VAR_PROB, NF_NOTE, NF_NOTE_VAR_RANGE, NF_NOTE_VAR_PROB,
    NF_CONDITION, NF_ACCUM, NF_PULSES, NF_GATE_MODE, NF_COUNT
};
struct FieldDef { const char *key; const char *label; int min, max, def; };
static const FieldDef NOTE_FIELDS[NF_COUNT] = {
    {"gate", "Gate", 0, 1, 0}, {"gateProbability", "Gate %", 0, 7, 7}, {"gateOffset", "Offset", 0, 7, 0},
    {"slide", "Slide", 0, 1, 0}, {"retrigger", "Ratchet", 0, 3, 0}, {"retriggerProbability", "Ratch %", 0, 7, 7},
    {"length", "Length", 0, 7, 3}, {"lengthVariationRange", "Len var", -7, 7, 0},
    {"lengthVariationProbability", "Len var %", 0, 7, 7}, {"note", "Note", -63, 63, 0},
    {"noteVariationRange", "Note var", -63, 63, 0}, {"noteVariationProbability", "Note var %", 0, 7, 7},
    {"condition", "Condition", 0, CONDITION_LAST - 1, 0}, {"accumulator", "Accum", -7, 7, 0},
    {"pulseCount", "Pulses", 0, 7, 0}, {"gateMode", "Gate mode", 0, 3, 0}};

struct NoteStep {
    int v[NF_COUNT];
    NoteStep() { for (int i = 0; i < NF_COUNT; i++) v[i] = NOTE_FIELDS[i].def; }
    bool isAccumulatorTrigger() const { return v[NF_ACCUM] != 0; }
};

enum { ACC_UP = 0, ACC_DOWN = 1, ACC_FREEZE = 2 };
enum { ACC_WRAP = 0, ACC_PENDULUM = 1, ACC_RANDOM = 2, ACC_HOLD = 3 };
enum { ACC_STEP = 0, ACC_GATE = 1, ACC_RETRIGGER = 2 };

struct Accumulator {
    int mode = 1, direction = ACC_UP, order = ACC_WRAP;
    bool enabled = true;
    int triggerMode = ACC_STEP, minValue = 0, maxValue = 7, stepValue = 1;
    int currentValue = 0, pendulumDirection = 1;
    bool hasStarted = false;
    Random *rng = nullptr;   // AccumulatorOps' own generator (Random order)
    void reset() { currentValue = minValue; pendulumDirection = 1; hasStarted = false; }
    void tick() {
        if (!enabled) return;
        if (!hasStarted) { hasStarted = true; return; }   // the first tick only arms it
        int dir = direction == ACC_UP ? 1 : (direction == ACC_DOWN ? -1 : 0);
        int v = currentValue, mn = minValue, mx = maxValue, mag = stepValue & 0xff;
        if (dir == 0) return;
        switch (order) {
        case ACC_WRAP: {
            v += dir > 0 ? mag : -mag;
            int range = mx - mn + 1;
            if (range > 0) v = mn + imod(v - mn, range);
            break;
        }
        case ACC_PENDULUM:
            v += mag * pendulumDirection;
            if (v >= mx) { v = mx; pendulumDirection = -pendulumDirection; }
            else if (v <= mn) { v = mn; pendulumDirection = -pendulumDirection; }
            break;
        case ACC_RANDOM:
            v = mn == mx ? mn : mn + (int)rng->nextRange(mx - mn + 1);
            break;
        case ACC_HOLD:
            if (dir > 0) { v += mag; if (v >= mx) v = mx; }
            else { v -= mag; if (v <= mn) v = mn; }
            break;
        }
        currentValue = (int16_t)v;
    }
    // a step-value override: negative flips direction, then one tick
    void tickOverride(int sv) {
        if (sv == 1) { tick(); return; }
        if (sv == 0) return;
        int osv = stepValue, osd = direction;
        if (sv < 0) { direction = osd == ACC_UP ? ACC_DOWN : ACC_UP; stepValue = -sv; }
        else stepValue = sv;
        tick();
        stepValue = osv; direction = osd;
    }
};

struct NoteParams {
    int firstStep = 0, lastStep = 15, runMode = 0, divisor = 12, clockMult = 100, resetMeasure = 0;
    int scale = 1, rootNote = 0, scaleRotate = 0, octave = 0, transpose = 0, rotate = 0;
    int gateProbabilityBias = 0, retriggerProbabilityBias = 0, lengthBias = 0, noteProbabilityBias = 0;
    int cvUpdate = 0;
};

struct NoteEngine {
    NoteParams p;
    NoteStep steps[NOTE_STEP_COUNT];
    Accumulator acc;
    bool fill = false;
    bool quirks = false;          // reproduce the module's bar-line queue clear (tests only)
    int measureTicks = 768;

    // outputs
    bool gateOutput = false;
    float cvOutput = 0, cvTarget = 0;
    bool slideActive = false;
    // for the display
    int currentStep = -1, pulseCounter = 0;
    bool stepFired = false;       // true for one tick when a step starts with a gate

    struct GateEv { uint32_t tick; bool gate, accTick; float cv; bool trig; };
    struct CvEv { uint32_t tick; float cv; bool slide; };
    SortedQueue<GateEv> gateQueue{16};
    SortedQueue<CvEv> cvQueue{16};
    Random rng, accRng;
    SequenceState state;
    bool prevCondition = false, trigGateActive = false;
    uint32_t ms = 0, trigGateOff = 0;

    NoteEngine() { acc.rng = &accRng; reset(); }

    // by name, with the firmware's ranges (used by the test harness)
    bool setParam(const std::string &name, double value) {
        if (name == "fill") { fill = value != 0; return true; }
        int v = jsround(value);
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"firstStep", &p.firstStep, 0, 63}, {"lastStep", &p.lastStep, 0, 63}, {"runMode", &p.runMode, 0, 5},
            {"divisor", &p.divisor, 1, 768}, {"clockMult", &p.clockMult, 50, 150}, {"resetMeasure", &p.resetMeasure, 0, 128},
            {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1}, {"rootNote", &p.rootNote, 0, 11}, {"scaleRotate", &p.scaleRotate, 0, 31},
            {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -100, 100}, {"rotate", &p.rotate, -64, 64},
            {"gateProbabilityBias", &p.gateProbabilityBias, -8, 8}, {"retriggerProbabilityBias", &p.retriggerProbabilityBias, -8, 8},
            {"lengthBias", &p.lengthBias, -8, 8}, {"noteProbabilityBias", &p.noteProbabilityBias, -8, 8},
            {"cvUpdate", &p.cvUpdate, 0, 1},
            {"accMode", &acc.mode, 0, 1}, {"accDirection", &acc.direction, 0, 2}, {"accOrder", &acc.order, 0, 3},
            {"accTrigger", &acc.triggerMode, 0, 2}, {"accMin", &acc.minValue, -100, 100}, {"accMax", &acc.maxValue, -100, 100},
            {"accStep", &acc.stepValue, 0, 100}};
        if (name == "accEnabled") { acc.enabled = v != 0; return true; }
        for (auto &r : ranges) {
            if (name != r.n) continue;
            v = iclamp(v, r.lo, r.hi);
            if (name == "firstStep") v = iclamp(v, 0, p.lastStep);
            if (name == "lastStep") v = iclamp(v, p.firstStep, 63);
            *r.f = v;
            if (name == "accMin") acc.reset();
            return true;
        }
        return false;
    }
    // test harness: "step <i> <field> <v>" or "<param> <v>"
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "step") { setStep(std::stoi(t[1]), fieldIndex(t[2]), jsround(std::stod(t[3]))); return true; }
        if (t[0] == "playMode" || t[0] == "slideTime") return true;   // always Aligned / CV only
        return setParam(t[0], std::stod(t[1]));
    }
    static int fieldIndex(const std::string &key) {
        for (int i = 0; i < NF_COUNT; i++) if (key == NOTE_FIELDS[i].key) return i;
        return -1;
    }

    void setStep(int i, int f, int value) {
        if (i < 0 || i >= NOTE_STEP_COUNT || f < 0 || f >= NF_COUNT) return;
        steps[i].v[f] = iclamp(value, NOTE_FIELDS[f].min, NOTE_FIELDS[f].max);
    }

    int effectiveDivisor() const {
        float cm = float(p.clockMult) * 0.01f;
        return std::max(1, jsround(float(float(p.divisor * 4) / cm)));
    }

    // full reset (Reset input): also restarts the random generators
    void reset() {
        rng = Random(0);
        accRng = Random(0);
        softReset();
    }
    // the module's own reset (Reset Measure): state only, randomness carries on
    void softReset() {
        state.reset();
        currentStep = -1;
        prevCondition = false;
        pulseCounter = 0;
        gateOutput = false;
        trigGateActive = false;
        cvOutput = 0; cvTarget = 0;
        slideActive = false;
        gateQueue.clear(); cvQueue.clear();
        acc.reset();
    }
    void barBoundary() { if (quirks) { gateQueue.clear(); cvQueue.clear(); } }

    ScaleView scaleView() const { return ScaleView(p.scale, p.scaleRotate); }

    float evalStepNote(const NoteStep &step, int bias, const ScaleView &scale) {
        int note = step.v[NF_NOTE] + p.octave * scale.notesPerOctave() + p.transpose;
        if (acc.enabled && (acc.mode == 1 || step.isAccumulatorTrigger())) note += acc.currentValue;
        int prob = iclamp(step.v[NF_NOTE_VAR_PROB] + bias, -1, 7);
        if ((int)rng.nextRange(8) <= prob) {
            int r = step.v[NF_NOTE_VAR_RANGE];
            int offset = r == 0 ? 0 : (int)rng.nextRange(std::abs(r) + 1);
            if (r < 0) offset = -offset;
            note = iclamp(note + offset, -63, 63);
        }
        return float(float(scale.noteToVolts(note)) + float(p.rootNote * float(1.0 / 12)));
    }

    bool evalCondition(const NoteStep &step, int iteration, bool fillOn, bool &prev) {
        int c = step.v[NF_CONDITION];
        switch (c) {
        case 0: return true;
        case 1: prev = fillOn; return prev;
        case 2: prev = !fillOn; return prev;
        case 3: return prev;
        case 4: return !prev;
        case 5: prev = iteration == 0; return prev;
        case 6: prev = iteration != 0; return prev;
        }
        if (c >= CONDITION_LOOP && c < CONDITION_LAST) {
            bool inv = c >= CONDITION_NOTLOOP;
            int idx = c - (inv ? CONDITION_NOTLOOP : CONDITION_LOOP);
            prev = iteration % COND_BASE[idx] == COND_OFFSET[idx];
            if (inv) prev = !prev;
            return prev;
        }
        return true;
    }

    // one engine tick (1/192 quarter note). Returns true when the pitch changed.
    bool tick(uint32_t tick) {
        stepFired = false;
        int divisor = effectiveDivisor();
        uint32_t resetDivisor = p.resetMeasure * measureTicks;
        uint32_t relativeTick = resetDivisor == 0 ? tick : tick % resetDivisor;
        if (relativeTick == 0) softReset();

        if (relativeTick % divisor == 0) {
            if (state.step < 0) {
                state.advanceFree(p.runMode, p.firstStep, p.lastStep, rng);
                pulseCounter = 0;
            } else {
                // a step lasts (Pulse Count + 1) clock pulses before the sequence moves on
                int pc = steps[rotateStep(state.step, p.firstStep, p.lastStep, p.rotate)].v[NF_PULSES];
                if (pulseCounter > pc) {
                    pulseCounter = 0;
                    state.advanceAligned(relativeTick / divisor, p.runMode, p.firstStep, p.lastStep, rng);
                }
            }
            pulseCounter++;
            triggerStep(tick, divisor);
        }

        bool updated = false;
        while (!gateQueue.empty() && tick >= gateQueue.front().tick) {
            GateEv ev = gateQueue.pop();
            gateOutput = ev.gate;
            if (ev.gate && ev.trig) { trigGateActive = true; trigGateOff = ms + 6; }
            if (ev.accTick && acc.enabled && acc.triggerMode == ACC_RETRIGGER) {
                acc.tick();
                if (ev.gate && ev.cv != 0) { cvTarget = ev.cv; slideActive = false; updated = true; }
            }
        }
        while (!cvQueue.empty() && tick >= cvQueue.front().tick) {
            CvEv ev = cvQueue.pop();
            cvTarget = ev.cv;
            slideActive = ev.slide;
            updated = true;
        }
        return updated;
    }

    // once per millisecond: ends the fixed 6 ms trigger of Length-0 steps
    void update() {
        if (trigGateActive && ms >= trigGateOff) { gateOutput = false; trigGateActive = false; }
        cvOutput = cvTarget;
        ms++;
    }

    void triggerStep(uint32_t tick, int divisor) {
        currentStep = rotateStep(state.step, p.firstStep, p.lastStep, p.rotate);
        const NoteStep &step = steps[currentStep];
        bool prev = prevCondition;
        ScaleView scale = scaleView();

        // accumulator, Step trigger: once per step (first pulse only)
        if (step.isAccumulatorTrigger() && pulseCounter == 1) {
            if (step.v[NF_CONDITION] != 0) {
                bool tmp = prevCondition;
                if (!evalCondition(step, state.iteration, fill, tmp)) return;
            }
            if (acc.enabled && acc.triggerMode == ACC_STEP) acc.tickOverride(step.v[NF_ACCUM]);
        }

        uint32_t gateOffset = (uint32_t)std::floor(divisor * step.v[NF_GATE_OFFSET] / 8.0);
        bool stepGate = step.v[NF_GATE] && (int)rng.nextRange(8) <= iclamp(step.v[NF_GATE_PROB] + p.gateProbabilityBias, -1, 7);
        if (stepGate) {
            stepGate = evalCondition(step, state.iteration, fill, prev);
            prevCondition = prev;
        }

        if (stepGate) {
            int gm = step.v[NF_GATE_MODE], pc = step.v[NF_PULSES];
            bool fire;
            if (gm == 1 || gm == 2) fire = pulseCounter == 1;                       // First / Hold
            else if (gm == 3) fire = pulseCounter == 1 || pulseCounter == pc + 1;   // First+Last
            else fire = true;                                                       // All

            if (fire) {
                stepFired = true;
                if (step.isAccumulatorTrigger() && acc.enabled && acc.triggerMode == ACC_GATE) acc.tickOverride(step.v[NF_ACCUM]);

                if (step.v[NF_LENGTH] == 0) {
                    // Length 0 = a short fixed trigger
                    gateQueue.pushReplace({tick + gateOffset, true, false, 0.f, true});
                } else {
                    int length = iclamp(step.v[NF_LENGTH] + p.lengthBias, 0, 7) + 1;
                    if ((int)rng.nextRange(8) <= step.v[NF_LEN_VAR_PROB]) {
                        int r = step.v[NF_LEN_VAR_RANGE];
                        int off = r == 0 ? 0 : (int)rng.nextRange(std::abs(r) + 1);
                        if (r < 0) off = -off;
                        length = iclamp(length + off, 0, 8);
                    }
                    uint32_t stepLength = (uint32_t)std::floor(divisor * length / 8.0);
                    if (gm == 2) stepLength = divisor * (pc + 1);                   // Hold: one gate over all pulses

                    int retrig = (int)rng.nextRange(8) <= iclamp(step.v[NF_RETRIG_PROB] + p.retriggerProbabilityBias, -1, 7) ? step.v[NF_RETRIG] + 1 : 1;
                    bool shouldTickAcc = step.isAccumulatorTrigger() && acc.enabled && acc.triggerMode == ACC_RETRIGGER;
                    float cv;
                    if (retrig > 1) {
                        uint32_t retrigLength = divisor / retrig, retrigOffset = 0;
                        int idx = 0;
                        float baseCv = 0;
                        if (shouldTickAcc) baseCv = evalStepNote(step, p.noteProbabilityBias, scale);
                        while (retrig-- > 0 && retrigOffset <= stepLength) {
                            cv = 0;
                            if (shouldTickAcc) {
                                Accumulator t = acc;
                                for (int k = 0; k < idx; k++) t.tick();
                                cv = float(baseCv + float(scale.noteToVolts(t.currentValue)));
                            }
                            gateQueue.pushReplace({tick + gateOffset + retrigOffset, true, shouldTickAcc, cv, false});
                            gateQueue.pushReplace({tick + gateOffset + retrigOffset + (retrigLength >> 1), false, false, 0.f, false});
                            retrigOffset += retrigLength;
                            idx++;
                        }
                    } else {
                        cv = 0;
                        if (shouldTickAcc) {
                            cv = evalStepNote(step, p.noteProbabilityBias, scale);
                            cv = float(cv + float(scale.noteToVolts(acc.currentValue)));
                        }
                        gateQueue.pushReplace({tick + gateOffset, true, shouldTickAcc, cv, false});
                        gateQueue.pushReplace({tick + gateOffset + stepLength, false, false, 0.f, false});
                    }
                }
            }
        }

        if (stepGate || p.cvUpdate == 1) {
            float note = evalStepNote(step, p.noteProbabilityBias, scale);
            cvQueue.push({tick + gateOffset, note, step.v[NF_SLIDE] == 1});
        }
    }

    // a brand-new module: a simple 8-note line on every other step
    void initFresh() {
        static const int notes[8] = {0, 2, 4, 7, 9, 7, 4, 2};
        for (int i = 0; i < NOTE_STEP_COUNT; i++) steps[i] = NoteStep();
        for (int i = 0; i < 16; i++) { steps[i].v[NF_GATE] = (i % 2 == 0) ? 1 : 0; steps[i].v[NF_NOTE] = notes[(i >> 1) % 8]; }
        p.lastStep = 15;
    }
    void clearSteps() { for (int i = 0; i < NOTE_STEP_COUNT; i++) steps[i] = NoteStep(); }
};

} // namespace xf
