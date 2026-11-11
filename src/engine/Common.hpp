// Shared helpers for the X|FORMER engines, ported from the firmware
// (core/utils/Random.h, engine/KeyedRng.h, model/Scale.cpp, engine/SortedQueue.h,
// engine/SequenceState.cpp). The engines are plain C++ with no Rack dependency, so
// the test harness in test/ can run them against the firmware simulator's traces.
#pragma once
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace xf {

static const int CONFIG_PPQN = 192;           // engine ticks per quarter note
static const int CONFIG_SEQUENCE_PPQN = 48;   // divisor units

template <typename T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// C lround: halves away from zero
inline int lroundi(double x) { return x < 0 ? -(int)std::floor(-x + 0.5) : (int)std::floor(x + 0.5); }
// JS Math.round: halves up
inline int jsround(double x) { return (int)std::floor(x + 0.5); }
inline int floorDiv(int a, int b) { return (int)std::floor((double)a / b); }
inline int imod(int a, int n) { return ((a % n) + n) % n; }

// Linear congruential generator, identical to the firmware's Random
struct Random {
    uint32_t state = 0;
    Random(uint32_t seed = 0) : state(seed) {}
    uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
    bool nextBinary() { return next() < 0x80000000u; }
    uint32_t nextRange(uint32_t range) { uint32_t div = 0xffffffffu / range; return next() / div; }
    float nextFloat() { return float(next() >> 9) / 8388608.f; }
};

inline uint32_t mix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
inline uint32_t cellSeed(uint32_t base, uint32_t cell) { return mix32(base ^ (cell * 0x9e3779b9u)); }

// ---- scales: notes in 1/128 semitone, 1536 per octave ----
struct ScaleDef { const char *name; bool chromatic; std::vector<int> notes; };
inline const std::vector<ScaleDef> &scales() {
    static const std::vector<ScaleDef> s = {
        {"Semitones", true, {0, 128, 256, 384, 512, 640, 768, 896, 1024, 1152, 1280, 1408}},
        {"Major", true, {0, 256, 512, 640, 896, 1152, 1408}},
        {"H.Minor", true, {0, 256, 384, 640, 896, 1024, 1408}},
        {"Major Blues", true, {0, 384, 512, 896, 1152, 1280}},
        {"Minor Blues", true, {0, 384, 640, 768, 896, 1280}},
        {"Major Pent.", true, {0, 256, 512, 896, 1152}},
        {"Minor Pent.", true, {0, 384, 640, 896, 1280}},
        {"Folk", true, {0, 128, 384, 512, 640, 896, 1024, 1280}},
        {"Japanese", true, {0, 128, 640, 896, 1024}},
        {"Gamelan", true, {0, 128, 384, 896, 1024}},
        {"Gypsy", true, {0, 256, 384, 768, 896, 1024, 1408}},
        {"Arabian", true, {0, 128, 512, 640, 896, 1024, 1408}},
        {"Flamenco", true, {0, 128, 512, 640, 896, 1024, 1280}},
        {"Whole Tone", true, {0, 256, 512, 768, 1024, 1280}},
        // microtonal (the firmware's N-tet scales)
        {"5-tet", false, {0, 307, 614, 922, 1229}},
        {"7-tet", false, {0, 219, 439, 658, 878, 1097, 1317}},
        {"19-tet", false, {0, 81, 162, 243, 323, 404, 485, 566, 647, 728, 808, 889, 970, 1051, 1132, 1213, 1293, 1374, 1455}},
        {"22-tet", false, {0, 70, 140, 209, 279, 349, 419, 489, 559, 628, 698, 768, 838, 908, 977, 1047, 1117, 1187, 1257, 1327, 1396, 1466}},
        {"24-tet", false, {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960, 1024, 1088, 1152, 1216, 1280, 1344, 1408, 1472}},
    };
    return s;
}
inline int scaleCount() { return (int)scales().size(); }

// a scale with rotation (Mode): Major + 1 = Dorian ...
struct ScaleView {
    const std::vector<int> *base;
    int rotate;
    ScaleView(int index, int rot) {
        base = &scales()[iclamp(index, 0, scaleCount() - 1)].notes;
        int n = (int)base->size();
        rotate = imod(rot, n);
    }
    int notesPerOctave() const { return (int)base->size(); }
    double baseVolts(int note) const {
        int n = (int)base->size();
        int octave = floorDiv(note, n);
        int index = note - octave * n;
        return octave + (*base)[index] / 1536.0;
    }
    double noteToVolts(int note) const {
        if (rotate == 0) return baseVolts(note);
        return baseVolts(note + rotate) - baseVolts(rotate);
    }
};

inline int voltsToMidi(float cv) { return iclamp(60 + (int)std::floor(cv * 12 + 0.01), 0, 127); }

// Sorted event queue (engine/SortedQueue.h): holds capacity-1 events in tick order
template <typename E> struct SortedQueue {
    std::vector<E> items;
    size_t max;
    SortedQueue(size_t capacity = 16) : max(capacity - 1) {}
    void clear() { items.clear(); }
    bool empty() const { return items.empty(); }
    const E &front() const { return items.front(); }
    E pop() { E e = items.front(); items.erase(items.begin()); return e; }
    size_t size() const { return items.size(); }
    size_t insert(const E &ev) {
        size_t i = items.size();
        while (i > 0 && ev.tick < items[i - 1].tick) i--;
        items.insert(items.begin() + i, ev);
        return i;
    }
    void push(const E &ev) { if (items.size() >= max) items.erase(items.begin()); insert(ev); }
    // insert, then throw away every event scheduled after it
    void pushReplace(const E &ev) { size_t i = insert(ev); items.resize(i + 1); }
};

// Divisor menu (48 PPQN sequence ticks)
struct DivisorDef { const char *name; int v; };
static const DivisorDef DIVISORS[] = {
    {"1/64", 3}, {"1/32", 6}, {"1/16T", 8}, {"1/16", 12}, {"1/8T", 16}, {"1/16.", 18},
    {"1/8", 24}, {"1/4T", 32}, {"1/8.", 36}, {"1/4", 48}, {"1/2", 96}, {"1 bar", 192},
    {"2 bars", 384}, {"4 bars", 768}};
static const int DIVISOR_COUNT = 14;

// Sequence state: run modes Forward, Backward, Pendulum, PingPong, Random, Random Walk
inline int randomStep(int first, int last, Random &rng) { return (int)rng.nextRange(last - first + 1) + first; }
struct SequenceState {
    int step = -1, prevStep = -1, direction = 1, iteration = 0;
    void reset() { step = -1; prevStep = -1; direction = 1; iteration = 0; }
    void advanceFree(int runMode, int first, int last, Random &rng) {
        prevStep = step;
        if (step == -1) {
            if (runMode <= 3) { step = runMode == 1 ? last : first; direction = runMode == 1 ? -1 : 1; }
            else { step = randomStep(first, last, rng); direction = 1; }
            return;
        }
        step = iclamp(step, first, last);
        switch (runMode) {
        case 0: direction = 1; if (step >= last) { step = first; iteration++; } else step++; break;
        case 1: direction = -1; if (step <= first) { step = last; iteration++; } else step--; break;
        case 2: case 3:
            if (direction > 0 && step >= last) direction = -1;
            else if (direction < 0 && step <= first) { direction = 1; iteration++; }
            if (runMode == 2) { if (step + direction >= first && step + direction <= last) step += direction; }
            else step += direction;
            break;
        case 4: direction = 1; step = randomStep(first, last, rng); break;
        case 5: randomWalk(first, last, rng); break;
        }
    }
    void advanceAligned(int abs, int runMode, int first, int last, Random &rng) {
        prevStep = step;
        int n = last - first + 1;
        switch (runMode) {
        case 0: direction = 1; step = first + abs % n; iteration = abs / n; break;
        case 1: direction = -1; step = last - abs % n; iteration = abs / n; break;
        case 2:
            iteration = abs / (2 * n); abs %= 2 * n;
            direction = abs < n ? 1 : -1;
            step = abs < n ? first + abs : last - (abs - n);
            break;
        case 3: {
            int period = std::max(1, 2 * n - 2);
            iteration = abs / period; abs %= period;
            direction = abs < n ? 1 : -1;
            step = abs < n ? first + abs : last - (abs - n) - 1;
            break;
        }
        case 4: direction = 1; step = first + (int)rng.nextRange(n); break;
        case 5: randomWalk(first, last, rng); break;
        }
    }
    void randomWalk(int first, int last, Random &rng) {
        if (step == -1) { step = randomStep(first, last, rng); direction = 1; return; }
        if (rng.nextRange(2) == 0) { step = step == first ? last : step - 1; direction = -1; }
        else { step = step == last ? first : step + 1; direction = 1; }
    }
};

inline int rotateStep(int step, int first, int last, int rotate) {
    int n = last - first + 1;
    step = first + (step - first + rotate) % n;
    return step + (step < 0 ? n : 0);
}

static const char *const RUN_MODE_NAMES[] = {"Forward", "Backward", "Pendulum", "PingPong", "Random", "Rnd Walk"};
static const char *const ROOT_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace xf
