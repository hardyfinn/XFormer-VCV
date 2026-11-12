// INDEXED: the firmware's Indexed track. Up to 48 steps, each with its own
// duration, gate and note; two modulation inputs reshape chosen step groups.
#include "../Xf.hpp"
#include "../engine/IndexedEngine.hpp"

namespace xfm {

static Info indexedInfo() {
	Info i;
	i.slug = "Indexed";
	i.title = "INDEXED";
	i.blurb = "steps with their own durations";
	i.auxIn[0] = "MOD A";
	i.auxInNote[0] = "+-5 V";
	i.auxIn[1] = "MOD B";
	i.auxInNote[1] = "+-5 V";
	i.auxOut = "SLIDE";
	i.auxOutNote = "slide gate";
	return i;
}

std::vector<std::string> groupNames() {
	std::vector<std::string> n = {"All"};
	for (int i = 1; i < 16; i++) {
		std::string s;
		for (int b = 0; b < 4; b++)
			if (i & (1 << b))
				s += "ABCD"[b];
		n.push_back(s);
	}
	n.push_back("No group");
	return n;
}

struct IndexedAdapter : EngineAdapter<xf::IndexedEngine> {
	int fModA = -1, fModB = -1, fStretch = -1, fResetBars = -1;
	float modField[2] = {0.f, 0.f}, modCv[2] = {0.f, 0.f};

	IndexedAdapter() {
		std::vector<std::string> sources(xf::IDX_SOURCE_NAMES, xf::IDX_SOURCE_NAMES + 3);
		std::vector<std::string> targets(xf::IDX_TARGET_NAMES, xf::IDX_TARGET_NAMES + 3);
		std::vector<std::string> combine(xf::IDX_COMBINE_NAMES, xf::IDX_COMBINE_NAMES + 4);
		std::vector<std::string> resetBars = {"Off"};
		for (int i = 1; i <= 32; i++)
			resetBars.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));

		addField(num("activeLength", "Steps", 1, 48, 8));
		addField(num("firstStep", "First step", 0, 47, 0, "", 1));
		fModA = addField(num("modA", "Mod A", -100, 100, 0, "%"));
		fModB = addField(num("modB", "Mod B", -100, 100, 0, "%"));
		addField(menu("runMode", "Run mode", runModeNames(), 0));
		addField(menu("loop", "Play", {"Once", "Loop"}, 1));
		fStretch = addField(num("divisor", "Stretch", 1, 16, 1, "x"));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		fResetBars = addField(menu("resetBars", "Reset every", resetBars, 0));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addField(menu("routeASource", "A from", sources, 0));
		addField(menu("routeATarget", "A to", targets, 0));
		addField(menu("routeAGroups", "A steps", groupNames(), 0));
		addField(num("routeAAmount", "A amount", -200, 200, 100, "%"));
		addField(menu("routeBSource", "B from", sources, 0));
		addField(menu("routeBTarget", "B to", targets, 0));
		addField(menu("routeBGroups", "B steps", groupNames(), 0));
		addField(num("routeBAmount", "B amount", -200, 200, 100, "%"));
		addField(menu("routeCombine", "Combine", combine, 0));
		addHostFields(false, true, true);

		addPage("PLAY", {"activeLength", "firstStep", "modA", "modB", "runMode", "loop", "octave", "transpose"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "resetBars"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "_slide", "_vel"});
		addPage("ROUTE A", {"routeASource", "routeATarget", "routeAGroups", "routeAAmount", "routeCombine"});
		addPage("ROUTE B", {"routeBSource", "routeBTarget", "routeBGroups", "routeBAmount", "routeCombine"});
		linked.push_back({fieldIndex("activeLength"), fieldIndex("firstStep")});
		ctrlKeys[0] = "activeLength";
		ctrlKeys[1] = "modA";
		ctrlKeys[2] = "modB";
		ctrlKeys[3] = "transpose";

		lanes.push_back(lane("Duration", 0, xf::INDEXED_MAX_DURATION, " t"));
		lanes.push_back(lane("Gate", 0, xf::INDEXED_GATE_MAX, " t"));
		lanes.push_back(lane("Note", -63, 63));
		lanes.push_back(laneNamed("Slide", {"Off", "On"}));
		lanes.push_back(laneNamed("Groups", {"-", "A", "B", "AB", "C", "AC", "BC", "ABC", "D", "AD", "BD", "ABD", "CD", "ACD", "BCD", "ABCD"}));
	}

	void applyMod(int i) {
		float v = xf::clamp(modField[i] + modCv[i], -100.f, 100.f);
		if (i == 0)
			e.p.modA = v;
		else
			e.p.modB = v;
	}
	void setField(int f, int v) override {
		if (f == fModA || f == fModB) {
			int i = f == fModA ? 0 : 1;
			modField[i] = (float)v;
			applyMod(i);
			return;
		}
		if (f == fStretch) {
			e.setParam("divisor", v);
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
		modCv[i] = connected ? v * 20.f : 0.f;
		applyMod(i);
	}

	bool slide() override { return e.slideActive; }
	bool retrigger() override { return e.retrigger; }
	int cyclePos() override { return e.displayStep; }
	float auxOut() override { return e.slideActive && e.gateOutput ? 10.f : 0.f; }

	int count(int) override { return xf::INDEXED_MAX_STEPS; }
	int get(int l, int i) override {
		const xf::IndexedStep& s = e.steps[xf::iclamp(i, 0, xf::INDEXED_MAX_STEPS - 1)];
		switch (l) {
			case 0: return s.duration;
			case 1: return s.gateLength;
			case 2: return s.noteIndex;
			case 3: return s.slide;
			default: return s.groupMask;
		}
	}
	void set(int l, int i, int v) override { e.setStep(i, l, v); }
	void range(int, int& first, int& last) override {
		first = 0;
		last = e.p.activeLength - 1;
	}
	int playPos(int) override { return e.displayStep; }
	bool stepOn(int i) override { return e.steps[xf::iclamp(i, 0, xf::INDEXED_MAX_STEPS - 1)].gateLength > 0; }

	void initFresh() override { e.initFresh(); }
	void clearSteps() override {
		for (int i = 0; i < xf::INDEXED_MAX_STEPS; i++) {
			xf::IndexedStep& s = e.steps[i];
			s.noteIndex = 0;
			s.slide = 0;
			s.groupMask = 0;
			s.setDuration(48);
			s.setGateLength(0);
		}
	}

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* steps = json_array();
		for (int i = 0; i < xf::INDEXED_MAX_STEPS; i++) {
			const xf::IndexedStep& s = e.steps[i];
			json_t* a = json_array();
			int v[5] = {s.duration, s.gateLength, s.noteIndex, s.slide, s.groupMask};
			for (int k = 0; k < 5; k++)
				json_array_append_new(a, json_integer(v[k]));
			json_array_append_new(steps, a);
		}
		json_object_set_new(j, "steps", steps);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* steps = json_object_get(j, "steps");
		for (int i = 0; steps && i < xf::INDEXED_MAX_STEPS && i < (int)json_array_size(steps); i++) {
			json_t* a = json_array_get(steps, i);
			xf::IndexedStep& s = e.steps[i];
			s.duration = xf::iclamp((int)json_integer_value(json_array_get(a, 0)), 0, xf::INDEXED_MAX_DURATION);
			s.gateLength = 0;
			s.setGateLength((int)json_integer_value(json_array_get(a, 1)));
			s.noteIndex = xf::iclamp((int)json_integer_value(json_array_get(a, 2)), -63, 63);
			s.slide = json_integer_value(json_array_get(a, 3)) ? 1 : 0;
			s.groupMask = (int)json_integer_value(json_array_get(a, 4)) & 15;
		}
	}
};

}  // namespace xfm

XF_ENGINE(Indexed, xfm::IndexedAdapter, xfm::indexedInfo)
