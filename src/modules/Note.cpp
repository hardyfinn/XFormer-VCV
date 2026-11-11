// NOTE: the firmware's Note track, a 64-step sequencer with probabilities,
// conditions, ratchets and an accumulator.
#include "../Xf.hpp"
#include "../engine/NoteEngine.hpp"

namespace xfm {

static Info noteInfo() {
	Info i;
	i.slug = "Note";
	i.title = "NOTE";
	i.blurb = "64-step sequencer with probabilities and conditions";
	i.auxIn[0] = "FILL";
	i.auxInNote[0] = "gate";
	i.auxOut = "SLIDE";
	i.auxOutNote = "slide gate";
	i.action = "Fill (hold)";
	return i;
}

static std::vector<std::string> resetBarNames() {
	std::vector<std::string> n = {"Off"};
	for (int i = 1; i <= 32; i++)
		n.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));
	return n;
}

struct NoteAdapter : EngineAdapter<xf::NoteEngine> {
	bool fillButton = false, fillGate = false;

	NoteAdapter() {
		addField(num("firstStep", "First step", 0, 63, 0, "", 1));
		addField(num("lastStep", "Last step", 0, 63, 15, "", 1));
		addField(menu("runMode", "Run mode", runModeNames(), 0));
		addField(num("rotate", "Rotate", -64, 64, 0));
		addField(num("gateProbabilityBias", "Gate bias", -8, 8, 0));
		addField(num("lengthBias", "Length bias", -8, 8, 0));
		addField(num("noteProbabilityBias", "Note bias", -8, 8, 0));
		addField(num("retriggerProbabilityBias", "Ratchet bias", -8, 8, 0));
		addField(menu("divisor", "Rate", divisorNames(), 3));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		addField(menu("resetMeasure", "Reset every", resetBarNames(), 0));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addField(menu("accEnabled", "Accumulator", {"Off", "On"}, 1));
		addField(menu("accMode", "Acc mode", {"Stage", "Track"}, 1));
		addField(menu("accDirection", "Acc direction", {"Up", "Down", "Freeze"}, 0));
		addField(menu("accOrder", "Acc order", {"Wrap", "Pendulum", "Random", "Hold"}, 0));
		addField(menu("accTrigger", "Acc trigger", {"Step", "Gate", "Ratchet"}, 0));
		addField(num("accMin", "Acc min", -100, 100, 0));
		addField(num("accMax", "Acc max", -100, 100, 7));
		addField(num("accStep", "Acc step", 0, 100, 1));
		addHostFields(false, true, true);

		addPage("PLAY", {"firstStep", "lastStep", "runMode", "rotate", "gateProbabilityBias", "lengthBias", "noteProbabilityBias", "retriggerProbabilityBias"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "resetMeasure"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "octave", "transpose", "_slide", "_vel"});
		addPage("ACCUMULATOR", {"accEnabled", "accMode", "accDirection", "accOrder", "accTrigger", "accMin", "accMax", "accStep"});
		linked.push_back({fieldIndex("firstStep"), fieldIndex("lastStep")});
		ctrlKeys[0] = "lastStep";
		ctrlKeys[1] = "gateProbabilityBias";
		ctrlKeys[2] = "noteProbabilityBias";
		ctrlKeys[3] = "transpose";

		std::vector<std::string> conds;
		for (int c = 0; c < xf::CONDITION_LAST; c++)
			conds.push_back(xf::conditionName(c));
		for (int l = 0; l < xf::NF_COUNT; l++) {
			int f = laneField(l);
			const xf::FieldDef& d = xf::NOTE_FIELDS[f];
			if (f == xf::NF_GATE || f == xf::NF_SLIDE)
				lanes.push_back(laneNamed(d.label, {"Off", "On"}));
			else if (f == xf::NF_CONDITION)
				lanes.push_back(laneNamed(d.label, conds));
			else if (f == xf::NF_GATE_PROB || f == xf::NF_RETRIG_PROB || f == xf::NF_LEN_VAR_PROB || f == xf::NF_NOTE_VAR_PROB)
				lanes.push_back(lane(d.label, d.min, d.max, "/8", 1));
			else if (f == xf::NF_RETRIG || f == xf::NF_PULSES || f == xf::NF_LENGTH)
				lanes.push_back(lane(d.label, d.min, d.max, "", 1));
			else
				lanes.push_back(lane(d.label, d.min, d.max));
		}
	}
	// lane order on screen: Note, Gate, then the rest (Note is what most edits are)
	int laneField(int l) {
		static int map[xf::NF_COUNT];
		static bool init = false;
		if (!init) {
			std::vector<int> order = {xf::NF_NOTE, xf::NF_GATE};
			for (int f = 0; f < xf::NF_COUNT; f++)
				if (f != xf::NF_NOTE && f != xf::NF_GATE)
					order.push_back(f);
			for (int i = 0; i < xf::NF_COUNT; i++)
				map[i] = order[i];
			init = true;
		}
		return map[xf::iclamp(l, 0, xf::NF_COUNT - 1)];
	}

	bool slide() override { return e.slideActive; }
	int cyclePos() override { return e.currentStep; }
	float auxOut() override { return e.slideActive && e.gateOutput ? 10.f : 0.f; }
	void auxIn(int i, float v, bool connected) override {
		fillGate = connected && v >= 1.f;
		e.fill = fillGate || fillButton;
	}
	void action(bool pressed) override {
		fillButton = pressed;
		e.fill = fillGate || fillButton;
	}
	bool actionLit() override { return e.fill; }

	int count(int) override { return xf::NOTE_STEP_COUNT; }
	int get(int l, int i) override { return e.steps[xf::iclamp(i, 0, 63)].v[laneField(l)]; }
	void set(int l, int i, int v) override { e.setStep(i, laneField(l), v); }
	void range(int, int& first, int& last) override {
		first = e.p.firstStep;
		last = e.p.lastStep;
	}
	int playPos(int) override { return e.currentStep; }
	bool stepOn(int i) override { return e.steps[xf::iclamp(i, 0, 63)].v[xf::NF_GATE] != 0; }

	void initFresh() override {
		e.clearSteps();
		e.initFresh();
	}
	void clearSteps() override { e.clearSteps(); }

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* steps = json_array();
		for (int i = 0; i < xf::NOTE_STEP_COUNT; i++) {
			json_t* a = json_array();
			for (int f = 0; f < xf::NF_COUNT; f++)
				json_array_append_new(a, json_integer(e.steps[i].v[f]));
			json_array_append_new(steps, a);
		}
		json_object_set_new(j, "steps", steps);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* steps = json_object_get(j, "steps");
		for (int i = 0; steps && i < xf::NOTE_STEP_COUNT && i < (int)json_array_size(steps); i++) {
			json_t* a = json_array_get(steps, i);
			for (int f = 0; f < xf::NF_COUNT && f < (int)json_array_size(a); f++)
				e.setStep(i, f, (int)json_integer_value(json_array_get(a, f)));
		}
	}
};

}  // namespace xfm

XF_MODEL(Note, xfm::NoteAdapter, xfm::noteInfo)
