// DISCRETE MAP engine (firmware engine/DiscreteMapTrackEngine.cpp)
//
// A voltage sweeps up and down a range (an internal saw or triangle ramp, or an
// outside input). 32 stages sit on that range, each with a threshold, a direction
// (Rise, Fall, Both or Off) and a note. When the sweep crosses a stage's threshold in
// its direction, that stage's note plays. The Scanner flips stage directions as it is
// swept across them.
#pragma once
#include "Common.hpp"
#include <string>

namespace xf {

static const int DM_STAGES = 32;
enum { DM_RISE = 0, DM_FALL = 1, DM_OFF = 2, DM_BOTH = 3 };
enum { DM_SRC_SAW = 0, DM_SRC_TRIANGLE = 1, DM_SRC_EXTERNAL = 2 };
static const float DM_RAMP_MIN = -5, DM_RAMP_MAX = 5;
static const char *const DM_DIR_NAMES[] = {"Rise", "Fall", "Off", "Both"};
static const char *const DM_SRC_NAMES[] = {"Saw", "Triangle", "Input"};

struct DmStage {
    int threshold = 0, direction = DM_OFF, noteIndex = 0;
    // Rise -> Fall -> Off -> Both -> Rise
    void cycleDirection() {
        static const int order[] = {DM_RISE, DM_FALL, DM_OFF, DM_BOTH};
        int k = 0;
        for (int i = 0; i < 4; i++) if (order[i] == direction) k = i;
        direction = order[(k + 1) % 4];
    }
};

struct DiscreteParams {
    int clockSource = 0, syncMode = 0, divisor = 192, clockMult = 100, gateLength = 0, loop = 1;
    int resetMeasure = 8, thresholdMode = 0, scale = 1, rootNote = 0, scaleRotate = 0, octave = 0;
    int transpose = 0, cvUpdate = 0;
    int playMode = 1;   // 0 = Aligned (sweep locked to the Reset Measure grid), 1 = Free
    float rangeHigh = 5, rangeLow = -5, input = 0, scanner = 0;
};

struct DiscreteEngine {
    DiscreteParams p;
    DmStage stages[DM_STAGES];
    int measureTicks = 768;
    bool quirks = false;

    // outputs
    bool gateOutput = false;
    float cvOutput = 0, targetCv = 0;
    bool retrigger = false;
    bool stepFired = false;
    int activeStage = -1;
    float rampValue = DM_RAMP_MIN, currentInput = DM_RAMP_MIN;

    float lengthTh[DM_STAGES] = {}, posTh[DM_STAGES] = {};
    float rampPhase = 0, prevInput = 0;
    uint32_t resetTickOffset = 0;
    bool prevLoop = true, running = true, thresholdsDirty = true;
    int gateTimer = 0, lastScannerSegment = -1;
    float prevRangeHigh = 5, prevRangeLow = -5;
    int prevThresholdMode = 0, sampledOctave = 0, sampledTranspose = 0, sampledRoot = 0;
    bool extOnceArmed = false, extOnceDone = false;
    float extMin = 0, extMax = 0;
    bool prevGate = false, retrig = false;
    float prevCv = 0;

    DiscreteEngine() { clearThresholds(); reset(); }

    // the module's default "fret" layout: thresholds interleaved across 4 pages
    void clearThresholds() {
        float step = float(200.0 / 31);
        for (int i = 0; i < DM_STAGES; i++) {
            int page = (i >> 3) + 1, button = i % 8;
            int g = button * 4 + (page - 1);
            float value = -100.f + float(g) * step;
            stages[i].threshold = iclamp((int)std::trunc(value + 0.5f), -100, 100);
        }
        thresholdsDirty = true;
    }

    bool setParam(const std::string &name, double value) {
        struct F { const char *n; float *f; float lo, hi; };
        F floats[] = {{"rangeHigh", &p.rangeHigh, -5, 5}, {"rangeLow", &p.rangeLow, -5, 5},
                      {"input", &p.input, -5, 5}, {"scanner", &p.scanner, 0, 34}};
        for (auto &f : floats) if (name == f.n) { *f.f = float(clamp(value, double(f.lo), double(f.hi))); return true; }
        struct R { const char *n; int *f; int lo, hi; };
        R ranges[] = {
            {"clockSource", &p.clockSource, 0, 2}, {"syncMode", &p.syncMode, 0, 1}, {"divisor", &p.divisor, 1, 768},
            {"clockMult", &p.clockMult, 50, 150}, {"gateLength", &p.gateLength, 0, 100}, {"loop", &p.loop, 0, 1},
            {"resetMeasure", &p.resetMeasure, 0, 128}, {"thresholdMode", &p.thresholdMode, 0, 1},
            {"scale", &p.scale, 0, quirks ? 13 : scaleCount() - 1}, {"rootNote", &p.rootNote, 0, 11},
            {"scaleRotate", &p.scaleRotate, 0, 31}, {"octave", &p.octave, -10, 10}, {"transpose", &p.transpose, -60, 60},
            {"cvUpdate", &p.cvUpdate, 0, 1}, {"playMode", &p.playMode, 0, 1}};
        int v = jsround(value);
        for (auto &r : ranges) if (name == r.n) { *r.f = iclamp(v, r.lo, r.hi); return true; }
        return false;
    }

    // field: 0 threshold (-100..100), 1 direction (0..3), 2 noteIndex (-63..63)
    void setStage(int i, int field, int value) {
        if (i < 0 || i >= DM_STAGES) return;
        DmStage &s = stages[i];
        if (field == 0) s.threshold = iclamp(value, -100, 100);
        else if (field == 1) s.direction = iclamp(value, 0, 3);
        else if (field == 2) s.noteIndex = iclamp(value, -63, 63);
        thresholdsDirty = true;
    }
    bool setRefParam(const std::vector<std::string> &t) {
        if (t[0] == "stage") {
            int f = t[2] == "threshold" ? 0 : t[2] == "direction" ? 1 : t[2] == "noteIndex" ? 2 : -1;
            setStage(std::stoi(t[1]), f, jsround(std::stod(t[3])));
            return true;
        }
        if (t[0] == "slewTime" || t[0] == "pluck" || t[0] == "offset") return true;
        if (t[0] == "rangeHigh" || t[0] == "rangeLow") return setParam(t[0], std::stod(t[1]) / 100);  // centivolts
        return setParam(t[0], std::stod(t[1]));
    }

    float rangeSpan() const {
        float span = p.rangeHigh - p.rangeLow;
        if (std::fabs(span) < 0.01) return span >= 0 ? 0.01f : -0.01f;
        return span;
    }

    // transport start / Reset: also forget what was sounding
    void reset() { prevGate = false; prevCv = 0; retrig = false; gateOutput = false; resetState(); }
    // the module's own reset (Loop switched on, Reset Measure)
    void resetState() {
        rampPhase = 0; rampValue = DM_RAMP_MIN;
        prevInput = -5.001f; currentInput = DM_RAMP_MIN;
        resetTickOffset = 0;
        prevLoop = p.loop != 0;
        activeStage = -1;
        cvOutput = 0; targetCv = 0;
        gateTimer = 0;
        running = true;
        thresholdsDirty = true;
        lastScannerSegment = -1;
        prevRangeHigh = p.rangeHigh; prevRangeLow = p.rangeLow;
        prevThresholdMode = p.thresholdMode;
        sampledOctave = p.octave; sampledTranspose = p.transpose; sampledRoot = p.rootNote;
        extOnceArmed = false; extOnceDone = false;
        extMin = 0; extMax = 0;
    }
    void barBoundary() {}

    int scaledDivisorTicks() const {
        float cm = float(p.clockMult) * 0.01f;
        return std::max(1, lroundi(float(p.divisor * 4) / cm));
    }

    bool tick(uint32_t tick) {
        stepFired = false;
        if (!prevLoop && p.loop) { resetState(); resetTickOffset = tick; }
        if (prevLoop && !p.loop) { extOnceArmed = false; extOnceDone = false; extMin = 0; extMax = 0; }
        prevLoop = p.loop != 0;

        if (std::fabs(double(p.rangeHigh) - prevRangeHigh) > 1e-6 || std::fabs(double(p.rangeLow) - prevRangeLow) > 1e-6) {
            thresholdsDirty = true;
            prevRangeHigh = p.rangeHigh; prevRangeLow = p.rangeLow;
        }
        if (p.thresholdMode != prevThresholdMode) { thresholdsDirty = true; prevThresholdMode = p.thresholdMode; }

        uint32_t rel = tick - resetTickOffset;
        if (p.syncMode == 1) {
            uint32_t rd = p.resetMeasure * measureTicks;
            if (rd > 0 && rel % rd == 0) { resetState(); resetTickOffset = tick; rel = 0; }
        }

        // 1. the sweep (Aligned: locked to the bar, period = Divisor)
        if (p.clockSource != DM_SRC_EXTERNAL) {
            uint32_t period = scaledDivisorTicks();
            uint32_t rd2 = p.resetMeasure * measureTicks;
            uint32_t align = (p.playMode == 0 && rd2 > 0) ? rel % rd2 : rel;
            if (running || p.loop) {
                rampPhase = float(double(align % period) / period);
                if (p.clockSource == DM_SRC_TRIANGLE) {
                    float tri = rampPhase < 0.5f ? rampPhase * 2 : 1.f - (rampPhase - 0.5f) * 2;
                    rampValue = DM_RAMP_MIN + tri * 10;
                } else rampValue = DM_RAMP_MIN + rampPhase * 10;
            }
            if (!p.loop && running && (rel % period) + 1 >= period) {
                running = false; rampPhase = 1; rampValue = DM_RAMP_MAX;
            }
            currentInput = rampValue;
        } else {
            currentInput = p.input;
        }

        bool freeze = updateExternalOnce();

        // the Scanner: entering one of 32 zones flips that stage's direction
        int seg = (int)std::trunc(clamp(p.scanner, 0.f, 34.f));
        if (seg != lastScannerSegment) {
            if (seg >= 1 && seg <= 32) stages[seg - 1].cycleDirection();
            lastScannerSegment = seg;
        }

        if (thresholdsDirty) {
            if (p.thresholdMode == 1) recalcLength(); else recalcPosition();
            thresholdsDirty = false;
        }

        int newStage = freeze ? activeStage : findActiveStage(currentInput, prevInput);
        bool gateChanged = newStage != activeStage;
        if (gateTimer > 0) gateTimer--;
        activeStage = newStage;

        if (gateChanged && activeStage >= 0) {
            if (p.cvUpdate == 0) { sampledOctave = p.octave; sampledTranspose = p.transpose; sampledRoot = p.rootNote; }
            int stepTicks = scaledDivisorTicks();
            gateTimer = p.gateLength == 0 ? 3 : (stepTicks * p.gateLength) / 100;
            retrig = true;
            stepFired = true;
        }
        if (activeStage >= 0 || p.cvUpdate == 1) {
            targetCv = activeStage >= 0 ? noteToVolts(stages[activeStage].noteIndex) : 0.f;
            cvOutput = targetCv;
        }
        prevInput = currentInput;
        emit();
        return false;
    }

    bool updateExternalOnce() {
        if (!(p.clockSource == DM_SRC_EXTERNAL && !p.loop)) return false;
        double lo = std::min(p.rangeLow, p.rangeHigh), hi = std::max(p.rangeLow, p.rangeHigh);
        double span = std::max(0.01, (double)std::fabs(rangeSpan()));
        if (!extOnceDone) {
            if (extOnceArmed) {
                extMin = std::min(extMin, currentInput);
                extMax = std::max(extMax, currentInput);
                if (float((double(extMax) - extMin) / span) >= 0.9f) { extOnceArmed = false; extOnceDone = true; }
            } else {
                double tol = float(span * double(0.05f));
                if (currentInput >= lo - tol && currentInput <= hi + tol) {
                    extOnceArmed = true; extMin = extMax = currentInput;
                }
            }
        }
        return extOnceDone || !extOnceArmed;
    }

    void recalcPosition() {
        float minV = p.rangeLow, spanV = p.rangeHigh - minV;
        for (int i = 0; i < DM_STAGES; i++) {
            float norm = float(double(stages[i].threshold + 100) / 200.0);
            posTh[i] = minV + norm * spanV;
        }
    }
    void recalcLength() {
        int total = 0;
        for (int i = 0; i < DM_STAGES; i++) total += stages[i].threshold + 100;
        float span = p.rangeHigh - p.rangeLow;
        if (total == 0) {
            for (int i = 0; i < DM_STAGES; i++) lengthTh[i] = p.rangeLow + (float(i + 1) / float(DM_STAGES)) * span;
            return;
        }
        float v = p.rangeLow;
        for (int i = 0; i < DM_STAGES; i++) {
            float prop = float(double(stages[i].threshold + 100) / total);
            v = v + prop * span;
            lengthTh[i] = v;
        }
    }
    float threshold(int i) const { return p.thresholdMode == 1 ? lengthTh[i] : posTh[i]; }

    // first stage whose threshold the sweep crossed (in that stage's direction) wins
    int findActiveStage(float input, float prev) const {
        for (int i = 0; i < DM_STAGES; i++) {
            const DmStage &s = stages[i];
            float th = threshold(i);
            bool crossed = false;
            if (s.direction == DM_RISE) crossed = prev < th && input >= th;
            else if (s.direction == DM_FALL) crossed = prev > th && input <= th;
            else if (s.direction == DM_BOTH) crossed = (prev < th && input >= th) || (prev > th && input <= th);
            if (crossed) return i;
        }
        if (activeStage >= 0 && stages[activeStage].direction == DM_OFF) return -1;
        return activeStage;
    }

    float noteToVolts(int noteIndex) const {
        ScaleView s(p.scale, p.scaleRotate);
        bool sampled = p.cvUpdate == 0;
        int oct = sampled ? sampledOctave : p.octave, tr = sampled ? sampledTranspose : p.transpose;
        int root = sampled ? sampledRoot : p.rootNote;
        int shift = oct * s.notesPerOctave() + tr;
        return float(float(s.noteToVolts(noteIndex + shift)) + float(root) * (1.f / 12));
    }

    void update() {}

    // gate = timer running on an active stage; every new stage is a new note
    void emit() {
        bool g = gateTimer > 0 && activeStage >= 0;
        retrigger = g && retrig && prevGate;
        retrig = false;
        prevGate = g; prevCv = cvOutput;
        gateOutput = g;
    }

    // a brand-new module: 8 stages on rising edges spread across the sweep, a scale run
    void initFresh() {
        int notes[] = {0, 2, 4, 1, 5, 3, 7, 4};
        for (int i = 0; i < DM_STAGES; i++) {
            DmStage &s = stages[i];
            s.direction = DM_OFF; s.noteIndex = 0;
            if (i < 8) { s.threshold = -100 + i * 25; s.direction = DM_RISE; s.noteIndex = notes[i]; }
        }
        thresholdsDirty = true;
    }
};

} // namespace xf
