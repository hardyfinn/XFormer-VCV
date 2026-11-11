// FRACTAL: the firmware's Fractal track. Records a phrase from the REC GATE and
// REC CV inputs, loops it and grows branches and ornaments from it.
#include "../Xf.hpp"
#include "../engine/FractalEngine.hpp"

namespace xfm {

static Info fractalInfo() {
	Info i;
	i.slug = "Fractal";
	i.title = "FRACTAL";
	i.blurb = "a phrase looper that grows branches";
	i.auxIn[0] = "REC G";
	i.auxInNote[0] = "gate";
	i.auxIn[1] = "REC CV";
	i.auxInNote[1] = "1 V/oct";
	i.auxOut = "ORN";
	i.auxOutNote = "trig";
	i.action = "Record on/off";
	i.actionLabel = "REC";
	return i;
}

struct FractalAdapter : EngineAdapter<xf::FractalEngine> {
	bool lastAction = false;
	int lastOrn = 0;
	bool ornFired = false;

	FractalAdapter() {
		e.inlineSource = true;
		std::vector<std::string> orders(xf::FR_ORDER_NAMES, xf::FR_ORDER_NAMES + 7);
		std::vector<std::string> resetBars = {"Off"};
		for (int i = 1; i <= 32; i++)
			resetBars.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));

		addField(menu("record", "Record", {"Off", "On"}, 0));
		addField(menu("lock", "Lock", {"Off", "On"}, 0));
		addField(num("ornRate", "Ornaments", 0, 100, 0, "%"));
		addField(num("ornIntensity", "Orn depth", 0, 100, 0, "%"));
		addField(num("branchCount", "Branches", 0, 7, 0));
		addField(num("path", "Path", 0, 255, 0));
		addField(num("branchSeed", "Seed", 0, 255, 0));
		addField(menu("orderMode", "Order", orders, 0));
		addField(menu("cadence", "Capture", {"Section", "Event"}, 1));
		addField(menu("fidelity", "Timing", {"Quantized", "Feel"}, 1));
		addField(menu("recordMode", "Rec mode", {"Replace", "Latch"}, 0));
		addField(num("recordSkip", "Rec skip", 0, 15, 0));
		addField(num("recordFirst", "Rec first", 0, 127, 0, "", 1));
		addField(num("recordLast", "Rec last", 0, 127, 15, "", 1));
		addField(menu("divisor", "Rate", divisorNames(), 3));
		addField(num("clockMult", "Clock mult", 50, 150, 100, "%"));
		addField(menu("resetMeasure", "Reset every", resetBars, 0));
		addField(num("loopFirst", "Loop first", 0, 127, 0, "", 1));
		addField(num("loopLast", "Loop last", 0, 127, 15, "", 1));
		addField(num("rotate", "Rotate", -64, 64, 0));
		addField(num("trackDelay", "Delay", 0, 16, 0));
		addField(num("branchPool", "Pool", 0, 255, 255));
		addField(num("ornFirst", "Orn first", 0, 127, 0, "", 1));
		addField(num("ornLast", "Orn last", 0, 127, 15, "", 1));
		addField(menu("scale", "Scale", scaleNames(), 1));
		addField(menu("rootNote", "Root", rootNames(), 0));
		addField(num("scaleRotate", "Mode", 0, 11, 0));
		addField(menu("quantize", "Main note", {"As played", "To scale"}, 0));
		addField(num("octave", "Octave", -10, 10, 0));
		addField(num("transpose", "Transpose", -48, 48, 0, " st"));
		addHostFields(false, false, true);

		addPage("PLAY", {"record", "lock", "ornRate", "ornIntensity", "branchCount", "path", "branchSeed", "orderMode"});
		addPage("RECORD", {"cadence", "fidelity", "recordMode", "recordSkip", "recordFirst", "recordLast"});
		addPage("CLOCK", {"_ppqn", "divisor", "clockMult", "resetMeasure"});
		addPage("LOOP", {"loopFirst", "loopLast", "rotate", "trackDelay", "branchPool", "ornFirst", "ornLast"});
		addPage("SCALE", {"scale", "rootNote", "scaleRotate", "quantize", "octave", "transpose", "_vel"});
		ctrlKeys[0] = "ornRate";
		ctrlKeys[1] = "branchCount";
		ctrlKeys[2] = "path";
		ctrlKeys[3] = "loopLast";

		std::vector<std::string> gates = {"Rest", "Trig"};
		for (int g = 2; g <= 14; g++)
			gates.push_back(std::to_string(g) + "/15");
		gates.push_back("Tie");
		lanes.push_back(lane("Pitch", -60, 60, " st"));
		lanes.push_back(laneNamed("Gate", gates));
		lanes.push_back(lane("Onset", 0, 15, "/16"));
	}

	void auxIn(int i, float v, bool connected) override {
		if (i == 0)
			e.srcGate = connected && v >= 1.f;
		else
			e.srcCv = connected ? v : 0.f;
	}
	void action(bool pressed) override {
		if (pressed && !lastAction && notify) {
			e.p.record = e.p.record ? 0 : 1;
			notify("record", e.p.record);
		}
		lastAction = pressed;
	}
	bool actionLit() override { return e.capturing(); }

	bool tick(uint32_t t) override {
		bool r = e.tick(t);
		ornFired = e.ornamentCount != lastOrn;
		lastOrn = e.ornamentCount;
		return r;
	}
	int cyclePos() override { return e.globalPos; }
	bool auxIsTrigger() override { return true; }
	float auxOut() override { return ornFired ? 1.f : 0.f; }

	int count(int) override { return xf::FRACTAL_CELLS; }
	int get(int l, int i) override {
		i = xf::iclamp(i, 0, xf::FRACTAL_CELLS - 1);
		if (l == 2)
			return e.onset[i];
		xf::FrCell c = xf::frDecode(e.trunk[i]);
		if (!c.valid)
			return 0;
		return l == 0 ? (int)std::lround(c.semi) : c.gateLen;
	}
	void set(int l, int i, int v) override {
		i = xf::iclamp(i, 0, xf::FRACTAL_CELLS - 1);
		if (l == 2) {
			e.onset[i] = xf::iclamp(v, 0, 15);
			return;
		}
		xf::FrCell c = xf::frDecode(e.trunk[i]);
		if (!c.valid) {
			c.semi = 0;
			c.gateLen = 8;
		}
		if (l == 0)
			c.semi = (float)xf::iclamp(v, -60, 60);
		else
			c.gateLen = xf::iclamp(v, 0, 15);
		e.trunk[i] = xf::frEncode(c.semi, c.gateLen, true);
	}
	void range(int, int& first, int& last) override {
		first = e.p.loopFirst;
		last = std::max(e.p.loopFirst, e.p.loopLast);
	}
	int playPos(int) override { return e.readPos; }
	bool stepOn(int i) override {
		xf::FrCell c = xf::frDecode(e.trunk[xf::iclamp(i, 0, xf::FRACTAL_CELLS - 1)]);
		return c.valid && c.gateLen > 0;
	}

	void initFresh() override { e.initFresh(); }
	void clearSteps() override { e.clearTrunk(); }
	std::string status() override {
		std::string s = e.capturing() ? string::f("REC %d", e.recordPos + 1) : (e.p.lock ? "LOCKED" : "");
		if (e.segment > 0)
			s += string::f("  branch %d", e.segment);
		return s;
	}

	json_t* toJson() override {
		json_t* j = json_object();
		json_t* t = json_array();
		json_t* o = json_array();
		for (int i = 0; i < xf::FRACTAL_CELLS; i++) {
			json_array_append_new(t, json_integer(e.trunk[i]));
			json_array_append_new(o, json_integer(e.onset[i]));
		}
		json_object_set_new(j, "trunk", t);
		json_object_set_new(j, "onset", o);
		return j;
	}
	void fromJson(json_t* j) override {
		json_t* t = json_object_get(j, "trunk");
		json_t* o = json_object_get(j, "onset");
		for (int i = 0; i < xf::FRACTAL_CELLS; i++) {
			if (t && i < (int)json_array_size(t))
				e.trunk[i] = (int)json_integer_value(json_array_get(t, i)) & 0xffff;
			if (o && i < (int)json_array_size(o))
				e.onset[i] = (int)json_integer_value(json_array_get(o, i)) & 0xf;
		}
	}
};

}  // namespace xfm

XF_MODEL(Fractal, xfm::FractalAdapter, xfm::fractalInfo)
