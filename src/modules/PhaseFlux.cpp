// PHASEFLUX: the firmware's PhaseFlux track. 16 stages on a 4x4 grid, each a
// burst of pulses shaped by a timing curve and a pitch curve.
#include "../Xf.hpp"
#include "../engine/PhaseFluxEngine.hpp"

namespace xfm {

static Info phaseFluxInfo() {
	Info i;
	i.slug = "PhaseFlux";
	i.title = "PHASEFLUX";
	i.blurb = "16 stages of shaped pulse bursts";
	i.auxOut = "STAGE";
	i.auxOutNote = "trig";
	return i;
}

static std::vector<std::string> names(const char* const* a, int n) { return std::vector<std::string>(a, a + n); }

struct PhaseFluxAdapter : EngineAdapter<xf::PhaseFluxEngine> {
	PhaseFluxAdapter() {
		std::vector<std::string> rates;
		for (int i = 0; i < 17; i++)
			rates.push_back(std::to_string(xf::PF_RATES[i][0]) + ":" + std::to_string(xf::PF_RATES[i][1]));
		std::vector<std::string> resetBars = {"Off"};
		for (int i = 1; i <= 32; i++)
			resetBars.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));
		std::vector<std::string> orders = names(xf::PF_ACC_ORDER_NAMES, 4);

		addField(num("pulseNudge", "Pulses +-", -15, 15, 0));
		addField(num("lenNudge", "Length +-", -64, 64, 0));
		addField(num("warpNudge", "Warp", -64, 64, 0));
		addField(num("responseNudge", "Response", -64, 64, 0));
		addField(num("cyclePhaseWarp", "Cycle warp", -64, 64, 0));
		addField(num("globalPhase", "Phase", 0, 100, 0, "%"));
		addField(menu("traversal", "Path", names(xf::PF_TRAVERSAL_NAMES, 8), 0));
		addField(menu("pitchMode", "Pitch mode", {"Per stage", "Global"}, 0));
		addField(menu("divisor", "Rate", divisorNames(), 3));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		addField(menu("cycleLength", "Cycle", {"Adaptive", "Fixed"}, 0));
		addField(menu("pitchRate", "Pitch rate", rates, 9));
		addField(num("firstStage", "First stage", 0, 15, 0, "", 1));
		addField(num("lastStage", "Last stage", 0, 15, 15, "", 1));
		addField(menu("resetMeasure", "Reset every", resetBars, 0));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addField(menu("noteAccScope", "Note acc", {"Per stage", "Shared"}, 0));
		addField(menu("noteAccOrder", "N.acc order", orders, 0));
		addField(menu("noteAccPolarity", "N.acc range", {"Uni", "Bi"}, 0));
		addField(num("noteAccPos", "N.acc max", 0, 64, 28));
		addField(num("noteAccNeg", "N.acc min", 0, 64, 28));
		addField(num("noteAccReset", "N.acc reset", 0, 15, 0));
		addField(menu("pulseAccScope", "Pulse acc", {"Per stage", "Shared"}, 0));
		addField(menu("pulseAccOrder", "P.acc order", orders, 0));
		addField(menu("pulseAccPolarity", "P.acc range", {"Uni", "Bi"}, 0));
		addField(num("pulseAccPos", "P.acc max", 0, 16, 16));
		addField(num("pulseAccNeg", "P.acc min", 0, 16, 16));
		addField(num("pulseAccReset", "P.acc reset", 0, 15, 0));
		addHostFields(false, false, true);

		addPage("PLAY", {"pulseNudge", "lenNudge", "warpNudge", "responseNudge", "cyclePhaseWarp", "globalPhase", "traversal", "pitchMode"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "cycleLength", "pitchRate", "firstStage", "lastStage", "resetMeasure"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "octave", "transpose", "_vel"});
		addPage("NOTE ACC", {"noteAccScope", "noteAccOrder", "noteAccPolarity", "noteAccPos", "noteAccNeg", "noteAccReset"});
		addPage("PULSE ACC", {"pulseAccScope", "pulseAccOrder", "pulseAccPolarity", "pulseAccPos", "pulseAccNeg", "pulseAccReset"});
		linked.push_back({fieldIndex("firstStage"), fieldIndex("lastStage")});
		ctrlKeys[0] = "pulseNudge";
		ctrlKeys[1] = "warpNudge";
		ctrlKeys[2] = "responseNudge";
		ctrlKeys[3] = "globalPhase";

		for (int f = 0; f < xf::PF_FIELD_COUNT; f++) {
			const xf::PfFieldDef& d = xf::PF_FIELDS[f];
			switch (f) {
				case xf::PF_SKIP:
				case xf::PF_PITCH_FLIPV:
				case xf::PF_PITCH_FLIPH:
				case xf::PF_T_FLIPV:
				case xf::PF_T_FLIPH: lanes.push_back(laneNamed(d.label, {"Off", "On"})); break;
				case xf::PF_PITCH_RANGE: lanes.push_back(laneNamed(d.label, names(xf::PF_RANGE_NAMES, 4))); break;
				case xf::PF_PITCH_DIR: lanes.push_back(laneNamed(d.label, names(xf::PF_DIRECTION_NAMES, 3))); break;
				case xf::PF_PITCH_CURVE: lanes.push_back(laneNamed(d.label, names(xf::PF_PITCH_CURVE_NAMES, 4))); break;
				case xf::PF_T_CURVE: lanes.push_back(laneNamed(d.label, names(xf::PF_TEMPORAL_CURVE_NAMES, 3))); break;
				case xf::PF_MASK: lanes.push_back(laneNamed(d.label, names(xf::PF_MASK_NAMES, 8))); break;
				case xf::PF_ACC_TRIG:
				case xf::PF_PULSE_ACC_TRIG: lanes.push_back(laneNamed(d.label, {"Stage", "Pulse"})); break;
				case xf::PF_T_REPEAT:
				case xf::PF_P_REPEAT: lanes.push_back(laneNamed(d.label, names(xf::PF_REPEAT_NAMES, 4))); break;
				case xf::PF_T_WINDOW:
				case xf::PF_P_WINDOW: lanes.push_back(laneNamed(d.label, names(xf::PF_WINDOW_NAMES, 5))); break;
				case xf::PF_GATE:
				case xf::PF_MASK_MELODY:
				case xf::PF_TILT_MELODY: lanes.push_back(lane(d.label, d.min, d.max, "%")); break;
				default: lanes.push_back(lane(d.label, d.min, d.max));
			}
		}
	}

	bool retrigger() override { return e.retrigger; }
	int cyclePos() override { return e.slotIdx; }
	bool auxIsTrigger() override { return true; }
	float auxOut() override { return e.stageTrigger ? 1.f : 0.f; }

	int count(int) override { return xf::PF_STAGES; }
	int get(int l, int i) override { return e.stages[xf::iclamp(i, 0, xf::PF_STAGES - 1)][xf::iclamp(l, 0, xf::PF_FIELD_COUNT - 1)]; }
	void set(int l, int i, int v) override { e.setStage(i, l, v); }
	void range(int, int& first, int& last) override {
		first = e.p.firstStage;
		last = e.p.lastStage;
	}
	int playPos(int) override { return e.activeCell; }
	bool stepOn(int i) override {
		const xf::PfStage& s = e.stages[xf::iclamp(i, 0, xf::PF_STAGES - 1)];
		return !s[xf::PF_SKIP] && s[xf::PF_PULSES] > 0;
	}

	void initFresh() override { e.initFresh(); }
	void clearSteps() override {
		for (int i = 0; i < xf::PF_STAGES; i++)
			e.stages[i].clear();
		e.layoutDirty = true;
	}
	std::string status() override {
		return string::f("Stage %d  acc %+d", e.activeCell + 1, e.noteAccFor(e.activeCell));
	}

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* st = json_array();
		for (int i = 0; i < xf::PF_STAGES; i++) {
			json_t* o = json_object();
			for (int f = 0; f < xf::PF_FIELD_COUNT; f++)
				json_object_set_new(o, xf::PF_FIELDS[f].key, json_integer(e.stages[i][f]));
			json_array_append_new(st, o);
		}
		json_object_set_new(j, "stages", st);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* st = json_object_get(j, "stages");
		for (int i = 0; st && i < xf::PF_STAGES && i < (int)json_array_size(st); i++) {
			json_t* o = json_array_get(st, i);
			for (int f = 0; f < xf::PF_FIELD_COUNT; f++) {
				json_t* v = json_object_get(o, xf::PF_FIELDS[f].key);
				if (v)
					e.setStage(i, f, (int)json_integer_value(v));
			}
		}
		e.layoutDirty = true;
	}
};

}  // namespace xfm

XF_MODEL(PhaseFlux, xfm::PhaseFluxAdapter, xfm::phaseFluxInfo)
