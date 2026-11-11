// ALGO: the firmware's Tuesday track, 16 generative algorithms.
#include "../Xf.hpp"
#include "../engine/TuesdayEngine.hpp"

namespace xfm {

static Info algoInfo() {
	Info i;
	i.slug = "Algo";
	i.title = "ALGO";
	i.blurb = "16 generative melody algorithms";
	i.auxIn[0] = "RESEED";
	i.auxInNote[0] = "trig";
	i.auxOut = "ACC";
	i.auxOutNote = "accent gate";
	i.action = "Reseed";
	i.actionLabel = "RESEED";
	return i;
}

struct AlgoAdapter : EngineAdapter<xf::TuesdayEngine> {
	// the grid shows the notes played lately
	static const int HISTORY = 32;
	int hist[HISTORY] = {};
	bool histGate[HISTORY] = {};
	int histPos = 0;
	bool lastAction = false, lastAux = false;
	int fVelSrc = -1;

	AlgoAdapter() {
		std::vector<std::string> algos(xf::ALGO_NAMES, xf::ALGO_NAMES + 16);
		std::vector<std::string> loops = {"Inf"};
		for (int i = 1; i < 30; i++)
			loops.push_back(std::to_string(xf::TU_LOOP_LENGTHS[i]));
		std::vector<std::string> masks = {"All"};
		for (int i = 0; i < 14; i++)
			masks.push_back(std::to_string(xf::TU_MASK_VALUES[i]));
		masks.push_back("None");

		addField(menu("algorithm", "Algorithm", algos, 1));
		addField(num("flow", "Flow", 0, 16, 4));
		addField(num("ornament", "Ornament", 0, 16, 4));
		addField(num("power", "Power", 0, 16, 12));
		addField(num("gateLength", "Gate length", 0, 100, 50, "%"));
		addField(num("glide", "Glide", 0, 100, 50, "%"));
		addField(num("trill", "Trill", 0, 100, 50, "%"));
		addField(menu("loopLength", "Loop", loops, 0));
		addField(menu("divisor", "Rate", divisorNames(), 3));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		addField(num("start", "Start", 0, 16, 0));
		addField(num("rotate", "Rotate", -64, 64, 0));
		addField(num("skew", "Skew", -8, 8, 0));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -11, 11, 0, " st"));
		addField(menu("maskParameter", "Mask", masks, 0));
		addField(menu("timeMode", "Mask time", {"Free", "Quarter", "1.5 Qrt", "3 Qrt"}, 0));
		addField(menu("maskProgression", "Mask step", {"None", "+1", "+5", "+7"}, 0));
		addField(num("gateOffset", "Gate offset", 0, 100, 0, "%"));
		addField(num("stepTrill", "Step trill", 0, 100, 0, "%"));
		fVelSrc = addField(menu("_velSrc", "Vel source", {"Algorithm", "Fixed"}, 0));
		addHostFields(true, false, true);

		addPage("PLAY", {"algorithm", "flow", "ornament", "power", "gateLength", "glide", "trill", "loopLength"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "_resetBars", "start", "rotate", "skew"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "octave", "transpose"});
		addPage("MASK & GATE", {"maskParameter", "timeMode", "maskProgression", "gateOffset", "stepTrill", "_velSrc", "_vel"});
		ctrlKeys[0] = "flow";
		ctrlKeys[1] = "ornament";
		ctrlKeys[2] = "power";
		ctrlKeys[3] = "gateLength";

		Lane l = lane("Notes played", -24, 24, " st");
		l.readOnly = true;
		lanes.push_back(l);
	}

	int velSource = 0;
	void setField(int f, int v) override {
		if (f == fVelSrc)
			velSource = v;
		Adapter::setField(f, v);
	}
	bool ownsResetBars() override { return false; }
	bool tick(uint32_t t) override {
		bool r = e.tick(t);
		if (e.stepFired) {
			histPos = (histPos + 1) % HISTORY;
			hist[histPos] = (int)std::lround(e.cvOutput * 12.0);
			histGate[histPos] = true;
		}
		return r;
	}
	void update() override {
		e.update();
	}
	float velocity() override { return velSource == 0 ? xf::clamp(e.velocity, 0, 127) / 127.f : -1.f; }
	int cyclePos() override {
		int len = e.actualLoopLength();
		int s = std::max(0, e.displayStep);
		return len > 0 ? s % len : s % 16;
	}
	float auxOut() override { return e.accent && e.gateOutput ? 10.f : 0.f; }
	void auxIn(int i, float v, bool connected) override {
		bool h = connected && v >= 1.f;
		if (h && !lastAux)
			e.reseed();
		lastAux = h;
	}
	void action(bool pressed) override {
		if (pressed && !lastAction)
			e.reseed();
		lastAction = pressed;
	}
	bool actionLit() override { return lastAction; }

	int count(int) override { return HISTORY; }
	int get(int, int i) override { return hist[(histPos + 1 + i) % HISTORY]; }
	int playPos(int) override { return HISTORY - 1; }
	bool stepOn(int i) override { return histGate[(histPos + 1 + i) % HISTORY]; }

	std::vector<std::string> actions() override { return {"Reseed"}; }
	void doAction(int) override { e.reseed(); }
	std::string status() override { return xf::ALGO_NAMES[xf::iclamp(e.p.algorithm, 0, 15)]; }
};

}  // namespace xfm

XF_MODEL(Algo, xfm::AlgoAdapter, xfm::algoInfo)
