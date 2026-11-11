// STOCHASTIC: the firmware's Stochastic track. Rhythm and melody are rolled from
// weighted dice, live or captured into a loop.
#include "../Xf.hpp"
#include "../engine/StochasticEngine.hpp"

namespace xfm {

static Info stochasticInfo() {
	Info i;
	i.slug = "Stochastic";
	i.title = "STOCHASTIC";
	i.blurb = "weighted-dice rhythm and melody";
	i.auxIn[0] = "NEW R";
	i.auxInNote[0] = "trig";
	i.auxIn[1] = "NEW M";
	i.auxInNote[1] = "trig";
	i.auxOut = "SLIDE";
	i.auxOutNote = "slide gate";
	i.action = "New rhythm + melody";
	i.actionLabel = "NEW";
	return i;
}

static const char* const DUR_NAMES[8] = {"8x", "4x", "3x", "2x", "4/3x", "1x", "2/3x", "1/2x"};

struct StochasticAdapter : EngineAdapter<xf::StochasticEngine> {
	bool lastAction = false, lastAux[2] = {false, false};

	StochasticAdapter() {
		std::vector<std::string> durs(DUR_NAMES, DUR_NAMES + 8);
		addField(num("complexity", "Complexity", 0, 100, 50, "%"));
		addField(num("contour", "Contour", -100, 100, 0));
		addField(num("range", "Range", 0, 100, 50, "%"));
		addField(num("variation", "Variation", 0, 100, 16, "%"));
		addField(num("rest", "Rest", 0, 100, 0, "%"));
		addField(num("gateLength", "Gate length", 0, 100, 0, "%"));
		addField(num("burst", "Burst", 0, 100, 0, "%"));
		addField(num("slide", "Slide", 0, 100, 0, "%"));
		addField(menu("rhythmMode", "Rhythm", {"Loop", "Live"}, 1));
		addField(menu("melodyMode", "Melody", {"Loop", "Live"}, 1));
		addField(menu("noteDuration", "Note length", durs, 5));
		addField(menu("evolve", "Evolve", {"Off", "On"}, 0));
		addField(num("patienceRhythm", "Patience R", 0, 100, 100, "%"));
		addField(num("patienceMelody", "Patience M", 0, 100, 100, "%"));
		addField(num("mutate", "Mutate", 0, 100, 0, "%"));
		addField(num("jump", "Jump", 0, 100, 0, "%"));
		addField(menu("divisor", "Rate", divisorNames(), 3));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		addField(num("size", "Steps", 2, 64, 32));
		addField(num("first", "First step", 0, 63, 0, "", 1));
		addField(num("rotate", "Rotate", -64, 64, 0));
		addField(num("feel", "Feel", 0, 100, 50, "%"));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addField(num("degreeRotation", "Degree rot", -32, 32, 0));
		addField(num("maskMelody", "Mask M", 0, 100, 100, "%"));
		addField(num("tiltMelody", "Tilt M", 0, 100, 0, "%"));
		addField(num("repeatProb", "Repeat", 0, 100, 0, "%"));
		addField(num("legatoProb", "Legato", 0, 100, 0, "%"));
		addField(num("sleep", "Sleep", 0, 100, 0, "%"));
		addField(num("maskRhythm", "Mask R", 0, 100, 100, "%"));
		addField(num("tiltRhythm", "Tilt R", 0, 100, 50, "%"));
		addField(num("maskRotation", "Mask rot", -32, 32, 0));
		addField(num("marblesSpread", "Spread", 0, 100, 50, "%"));
		addField(num("marblesBias", "Bias", 0, 100, 50, "%"));
		addField(num("burstRate", "Burst rate", 0, 100, 50, "%"));
		addField(num("burstCount", "Burst count", 0, 100, 0, "%"));
		addField(menu("burstHold", "Burst mode", {"Hold fit", "Hold over", "Roll fit", "Roll over"}, 1));
		addHostFields(true, true, true);

		addPage("PLAY", {"complexity", "contour", "range", "variation", "rest", "gateLength", "burst", "slide"});
		addPage("LOOP", {"rhythmMode", "melodyMode", "noteDuration", "evolve", "patienceRhythm", "patienceMelody", "mutate", "jump"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "_resetBars", "size", "first", "rotate", "feel"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "octave", "transpose", "degreeRotation", "maskMelody", "tiltMelody"});
		addPage("RHYTHM", {"repeatProb", "legatoProb", "sleep", "maskRhythm", "tiltRhythm", "maskRotation", "marblesSpread", "marblesBias"});
		addPage("BURST & OUT", {"burstRate", "burstCount", "burstHold", "_slide", "_vel"});
		linked.push_back({fieldIndex("size"), fieldIndex("first")});
		ctrlKeys[0] = "complexity";
		ctrlKeys[1] = "range";
		ctrlKeys[2] = "variation";
		ctrlKeys[3] = "rest";

		Lane pat = lane("Pattern (degrees)", -14, 28);
		pat.readOnly = true;
		lanes.push_back(pat);
		lanes.push_back(lane("Degree tickets", -1, 100));
		lanes.push_back(lane("Length tickets", -1, 100));
	}

	bool ownsResetBars() override { return false; }
	bool barsReset() override {
		e.measureReset();
		return false;
	}
	bool slide() override { return e.slideActive; }
	int cyclePos() override { return e.currentStep; }
	float auxOut() override { return e.slideActive && e.gateOutput ? 10.f : 0.f; }

	void newRhythm() {
		if (e.newRhythm() && notify)
			notify("rhythmMode", 0);
	}
	void newMelody() {
		if (e.newMelody() && notify)
			notify("melodyMode", 0);
	}
	void auxIn(int i, float v, bool connected) override {
		bool h = connected && v >= 1.f;
		if (h && !lastAux[i])
			i == 0 ? newRhythm() : newMelody();
		lastAux[i] = h;
	}
	void action(bool pressed) override {
		if (pressed && !lastAction) {
			newRhythm();
			newMelody();
		}
		lastAction = pressed;
	}
	bool actionLit() override { return lastAction; }

	int count(int l) override {
		if (l == 0)
			return std::max(1, e.cache.count);
		if (l == 1)
			return e.N();
		return 8;
	}
	int get(int l, int i) override {
		if (l == 0) {
			if (i >= e.cache.count)
				return 0;
			const xf::st::Cell& c = e.cache.cells[i];
			return c.degree + c.octave * e.N();
		}
		if (l == 1)
			return e.p.degreeTickets[xf::iclamp(i, 0, 31)];
		return e.p.durationTickets[xf::iclamp(i, 0, 7)];
	}
	void set(int l, int i, int v) override {
		if (l == 1)
			e.setParam("degreeTicket" + std::to_string(i), v);
		else if (l == 2)
			e.setParam("durationTicket" + std::to_string(i), v);
	}
	int playPos(int l) override {
		if (l != 0)
			return -1;
		return e.displayStep - e.p.first;
	}
	bool stepOn(int i) override { return i < e.cache.count ? e.cache.cells[i].audible : false; }

	void initFresh() override { e.initFresh(); }
	void clearSteps() override {
		for (int i = 0; i < 32; i++)
			e.p.degreeTickets[i] = 0;
		for (int i = 0; i < 8; i++)
			e.p.durationTickets[i] = 0;
		e.refreshLoopSources();
	}
	std::vector<std::string> actions() override { return {"New rhythm", "New melody", "Undo rhythm", "Undo melody"}; }
	void doAction(int i) override {
		switch (i) {
			case 0: newRhythm(); break;
			case 1: newMelody(); break;
			case 2: e.undoRhythm(); break;
			case 3: e.undoMelody(); break;
		}
	}
	std::string status() override {
		return std::string(e.p.rhythmMode ? "R live" : "R loop") + "  " + (e.p.melodyMode ? "M live" : "M loop");
	}

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* steps = json_array();
		for (int i = 0; i < xf::st::STEP_COUNT; i++) {
			const xf::st::StepContent& s = e.p.steps[i];
			json_t* a = json_array();
			int v[11] = {s.durationIndex, s.densityRank, s.burstTails, s.burstRate, s.rest, s.legato, s.slide,
				s.rhythmValid, s.degree, s.octave, s.melodyValid};
			for (int k = 0; k < 11; k++)
				json_array_append_new(a, json_integer(v[k]));
			json_array_append_new(steps, a);
		}
		json_object_set_new(j, "steps", steps);
		json_object_set_new(j, "rhythmValid", json_boolean(e.p.rhythmValid));
		json_object_set_new(j, "melodyValid", json_boolean(e.p.melodyValid));
		json_object_set_new(j, "rhythmSeed", json_integer(e.p.rhythmSeed));
		json_object_set_new(j, "melodySeed", json_integer(e.p.melodySeed));
		json_object_set_new(j, "capturedR", json_boolean(e.capturedR));
		json_object_set_new(j, "capturedM", json_boolean(e.capturedM));
		json_t* dt = json_array();
		for (int i = 0; i < 32; i++)
			json_array_append_new(dt, json_integer(e.p.degreeTickets[i]));
		json_object_set_new(j, "degreeTickets", dt);
		json_t* ut = json_array();
		for (int i = 0; i < 8; i++)
			json_array_append_new(ut, json_integer(e.p.durationTickets[i]));
		json_object_set_new(j, "durationTickets", ut);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* steps = json_object_get(j, "steps");
		for (int i = 0; steps && i < xf::st::STEP_COUNT && i < (int)json_array_size(steps); i++) {
			json_t* a = json_array_get(steps, i);
			int v[11] = {};
			for (int k = 0; k < 11 && k < (int)json_array_size(a); k++)
				v[k] = (int)json_integer_value(json_array_get(a, k));
			xf::st::StepContent& s = e.p.steps[i];
			s.durationIndex = v[0]; s.densityRank = v[1]; s.burstTails = v[2]; s.burstRate = v[3];
			s.rest = v[4]; s.legato = v[5]; s.slide = v[6]; s.rhythmValid = v[7];
			s.degree = v[8]; s.octave = v[9]; s.melodyValid = v[10];
		}
		e.p.rhythmValid = json_is_true(json_object_get(j, "rhythmValid"));
		e.p.melodyValid = json_is_true(json_object_get(j, "melodyValid"));
		e.p.rhythmSeed = (uint32_t)json_integer_value(json_object_get(j, "rhythmSeed"));
		e.p.melodySeed = (uint32_t)json_integer_value(json_object_get(j, "melodySeed"));
		e.capturedR = json_is_true(json_object_get(j, "capturedR"));
		e.capturedM = json_is_true(json_object_get(j, "capturedM"));
		json_t* dt = json_object_get(j, "degreeTickets");
		for (int i = 0; dt && i < 32 && i < (int)json_array_size(dt); i++)
			e.p.degreeTickets[i] = xf::iclamp((int)json_integer_value(json_array_get(dt, i)), -1, 100);
		json_t* ut = json_object_get(j, "durationTickets");
		for (int i = 0; ut && i < 8 && i < (int)json_array_size(ut); i++)
			e.p.durationTickets[i] = xf::iclamp((int)json_integer_value(json_array_get(ut, i)), -1, 100);
		e.save();
		e.refreshStepCache();
	}
};

}  // namespace xfm

XF_MODEL(Stochastic, xfm::StochasticAdapter, xfm::stochasticInfo)
