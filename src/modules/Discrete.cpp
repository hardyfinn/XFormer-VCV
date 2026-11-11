// DISCRETE: the firmware's Discrete Map track. A sweep (internal saw or
// triangle, or the INPUT jack) crosses 32 stage thresholds; each crossing plays
// that stage's note.
#include "../Xf.hpp"
#include "../engine/DiscreteEngine.hpp"

namespace xfm {

static Info discreteInfo() {
	Info i;
	i.slug = "Discrete";
	i.title = "DISCRETE";
	i.blurb = "a sweep crossing 32 note thresholds";
	i.auxIn[0] = "INPUT";
	i.auxInNote[0] = "+-5 V";
	i.auxIn[1] = "SCAN";
	i.auxInNote[1] = "0-10 V";
	i.auxOut = "SWEEP";
	i.auxOutNote = "+-5 V";
	return i;
}

struct DiscreteAdapter : EngineAdapter<xf::DiscreteEngine> {
	int fInput = -1, fScanner = -1, fHigh = -1, fLow = -1, fResetBars = -1;
	float inField = 0.f, inCv = 0.f, scanField = 0.f, scanCv = 0.f;

	DiscreteAdapter() {
		std::vector<std::string> resetBars = {"Off"};
		for (int i = 1; i <= 32; i++)
			resetBars.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));
		addField(menu("clockSource", "Source", {"Saw", "Triangle", "Input"}, 0));
		addField(menu("divisor", "Sweep time", divisorNames(), 11));
		addField(num("gateLength", "Gate length", 0, 100, 0, "%"));
		fInput = addField(num("input", "Input", -100, 100, 0, "%"));
		fScanner = addField(num("scanner", "Scanner", 0, 34, 0));
		fHigh = addField(num("rangeHigh", "Range top", -100, 100, 100, "%"));
		fLow = addField(num("rangeLow", "Range bottom", -100, 100, -100, "%"));
		addField(menu("thresholdMode", "Spacing", {"Position", "Length"}, 0));
		addField(menu("loop", "Play", {"Once", "Loop"}, 1));
		addField(menu("playMode", "Sweep timing", {"Bar-aligned", "Free"}, 1));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		fResetBars = addField(menu("resetBars", "Reset every", resetBars, 0));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addHostFields(false, false, true);

		addPage("PLAY", {"clockSource", "divisor", "gateLength", "input", "scanner", "rangeHigh", "rangeLow", "thresholdMode"});
		addPage("CLOCK", {"_ppqn", "loop", "playMode", "clockMult", "resetBars"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "octave", "transpose", "_vel"});
		ctrlKeys[0] = "rangeHigh";
		ctrlKeys[1] = "rangeLow";
		ctrlKeys[2] = "gateLength";
		ctrlKeys[3] = "transpose";

		lanes.push_back(lane("Threshold", -100, 100));
		lanes.push_back(laneNamed("Direction", {"Rise", "Fall", "Off", "Both"}));
		lanes.push_back(lane("Note", -63, 63));
	}

	void applyInput() { e.p.input = xf::clamp(inField * 0.05f + inCv, -5.f, 5.f); }
	void applyScanner() { e.p.scanner = xf::clamp(scanField + scanCv, 0.f, 34.f); }
	void setField(int f, int v) override {
		if (f == fInput) {
			inField = (float)v;
			applyInput();
			return;
		}
		if (f == fScanner) {
			scanField = (float)v;
			applyScanner();
			return;
		}
		if (f == fHigh || f == fLow) {
			e.setParam(f == fHigh ? "rangeHigh" : "rangeLow", v * 0.05);
			return;
		}
		if (f == fResetBars) {
			e.p.resetMeasure = xf::iclamp(v, 0, 128);
			e.p.syncMode = v > 0 ? 1 : 0;
			return;
		}
		Adapter::setField(f, v);
	}
	void auxIn(int i, float v, bool connected) override {
		if (i == 0) {
			inCv = connected ? v : 0.f;
			applyInput();
		}
		else {
			scanCv = connected ? v * 3.4f : 0.f;
			applyScanner();
		}
	}

	bool retrigger() override { return e.retrigger; }
	int cyclePos() override { return (int)(e.rampPhase * 1000.f); }
	float auxOut() override { return e.currentInput; }

	int count(int) override { return xf::DM_STAGES; }
	int get(int l, int i) override {
		const xf::DmStage& s = e.stages[xf::iclamp(i, 0, xf::DM_STAGES - 1)];
		return l == 0 ? s.threshold : l == 1 ? s.direction : s.noteIndex;
	}
	void set(int l, int i, int v) override { e.setStage(i, l, v); }
	int playPos(int) override { return e.activeStage; }
	bool stepOn(int i) override { return e.stages[xf::iclamp(i, 0, xf::DM_STAGES - 1)].direction != xf::DM_OFF; }

	void initFresh() override { e.initFresh(); }
	void clearSteps() override {
		for (int i = 0; i < xf::DM_STAGES; i++) {
			e.stages[i].direction = xf::DM_OFF;
			e.stages[i].noteIndex = 0;
		}
		e.clearThresholds();
	}
	std::string status() override {
		return e.activeStage >= 0 ? string::f("Stage %d", e.activeStage + 1) : "";
	}

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* st = json_array();
		for (int i = 0; i < xf::DM_STAGES; i++) {
			json_t* a = json_array();
			json_array_append_new(a, json_integer(e.stages[i].threshold));
			json_array_append_new(a, json_integer(e.stages[i].direction));
			json_array_append_new(a, json_integer(e.stages[i].noteIndex));
			json_array_append_new(st, a);
		}
		json_object_set_new(j, "stages", st);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* st = json_object_get(j, "stages");
		for (int i = 0; st && i < xf::DM_STAGES && i < (int)json_array_size(st); i++) {
			json_t* a = json_array_get(st, i);
			for (int f = 0; f < 3; f++)
				e.setStage(i, f, (int)json_integer_value(json_array_get(a, f)));
		}
	}
};

}  // namespace xfm

XF_MODEL(Discrete, xfm::DiscreteAdapter, xfm::discreteInfo)
