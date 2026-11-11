// The shared frame of the seven X|FORMER modules: an engine adapter (one per
// module), the module itself (external clock follower, menus, CTRL knobs,
// outputs) and the panel widget (display, DATA encoder, step grid).
#pragma once
#include "plugin.hpp"
#include "engine/Common.hpp"
#include <atomic>
#include <functional>

namespace xfm {

// ---------------------------------------------------------------- specs

// A setting shown in the display. The value stored is what the engine wants;
// `shown` is added for display only (one-based step numbers).
struct Field {
	std::string key, label;
	int min = 0, max = 1, def = 0;
	std::vector<std::string> names;  // when set: names[v - min]
	std::string unit;
	int shown = 0;
	std::string text(int v) const {
		if (!names.empty()) {
			int i = v - min;
			return (i >= 0 && i < (int)names.size()) ? names[i] : std::to_string(v);
		}
		return std::to_string(v + shown) + unit;
	}
};
inline Field num(std::string key, std::string label, int mn, int mx, int def, std::string unit = "", int shown = 0) {
	Field f;
	f.key = key; f.label = label; f.min = mn; f.max = mx; f.def = def; f.unit = unit; f.shown = shown;
	return f;
}
inline Field menu(std::string key, std::string label, std::vector<std::string> names, int def, int mn = 0) {
	Field f;
	f.key = key; f.label = label; f.min = mn; f.max = mn + (int)names.size() - 1; f.def = def; f.names = names;
	return f;
}

// A row of per-step values in the grid.
struct Lane {
	std::string label;
	int min = 0, max = 1;
	std::vector<std::string> names;
	bool readOnly = false;
	int shown = 0;
	std::string unit;
	std::string text(int v) const {
		if (!names.empty()) {
			int i = v - min;
			return (i >= 0 && i < (int)names.size()) ? names[i] : std::to_string(v);
		}
		return std::to_string(v + shown) + unit;
	}
};
inline Lane lane(std::string label, int mn, int mx, std::string unit = "", int shown = 0) {
	Lane l;
	l.label = label; l.min = mn; l.max = mx; l.unit = unit; l.shown = shown;
	return l;
}
inline Lane laneNamed(std::string label, std::vector<std::string> names, int mn = 0) {
	Lane l;
	l.label = label; l.min = mn; l.max = mn + (int)names.size() - 1; l.names = names;
	return l;
}

struct Page {
	std::string title;
	std::vector<int> fields;
};

// What the panel shows, known before a module exists (for the browser).
struct Info {
	std::string slug, title, blurb;
	std::string auxIn[2], auxInNote[2];   // empty = no jack
	std::string auxOut, auxOutNote;
	std::string action;                   // empty = no button
	std::string actionLabel;              // the short name over the button
	bool multi = false;                   // the Multi module: every jack, labels drawn live
};

// common menus
std::vector<std::string> scaleNames();
std::vector<std::string> divisorNames();
std::vector<std::string> rootNames();
std::vector<std::string> runModeNames();
int divisorIndexOf(int ticks);

// ---------------------------------------------------------------- engine adapter

struct Adapter {
	std::vector<Field> fields;
	std::vector<Page> pages;
	std::vector<Lane> lanes;
	std::vector<std::vector<int>> linked;   // fields that limit each other: re-sent together
	std::string ctrlKeys[4];                // default CTRL destinations
	int measureTicks = 768;
	// the engine changed one of its own settings (New Rhythm switches Live -> Loop)
	std::function<void(const std::string&, int)> notify;

	virtual ~Adapter() {}
	// module settings every engine has: clock PPQN, and optionally Reset Bars,
	// slide time and a fixed velocity level
	void addHostFields(bool resetBars, bool slide, bool vel) {
		addField(menu("_ppqn", "Clock in", {"1 PPQN", "2 PPQN", "4 PPQN (16ths)", "16 PPQN", "24 PPQN", "48 PPQN"}, 2));
		if (resetBars) {
			std::vector<std::string> n = {"Off"};
			for (int i = 1; i <= 32; i++)
				n.push_back(std::to_string(i) + (i == 1 ? " bar" : " bars"));
			addField(menu("_resetBars", "Reset every", n, 0));
		}
		if (slide)
			addField(num("_slide", "Slide time", 0, 2000, 60, " ms"));
		if (vel)
			addField(num("_vel", "Velocity", 0, 100, 80, "%"));
	}
	int fieldIndex(const std::string& key) const {
		for (int i = 0; i < (int)fields.size(); i++)
			if (fields[i].key == key)
				return i;
		return -1;
	}
	int addField(const Field& f) {
		fields.push_back(f);
		return (int)fields.size() - 1;
	}
	void addPage(const std::string& title, const std::vector<std::string>& keys) {
		Page p;
		p.title = title;
		for (const auto& k : keys) {
			int i = fieldIndex(k);
			if (i >= 0)
				p.fields.push_back(i);
		}
		pages.push_back(p);
	}

	// settings: keys that start with "_" belong to the module, the rest go to the engine
	virtual void setParam(const std::string& key, double v) = 0;
	virtual void setField(int f, int v) {
		const Field& fd = fields[f];
		if (fd.key[0] == '_')
			return;
		if (fd.key == "divisor") {
			setParam("divisor", xf::DIVISORS[xf::iclamp(v, 0, xf::DIVISOR_COUNT - 1)].v);
			return;
		}
		setParam(fd.key, v);
	}

	// running
	virtual void reset() = 0;
	virtual bool tick(uint32_t t) = 0;   // true when the CV changed
	virtual void update() = 0;           // once a millisecond
	virtual void bar() {}
	virtual bool ownsResetBars() { return true; }   // the engine has its own Reset Measure
	// Reset Bars for engines without their own: true = a hard restart (tick count restarts)
	virtual bool barsReset() { reset(); return true; }
	virtual void stop() {}

	// outputs
	virtual bool gate() = 0;
	virtual float cv() = 0;
	virtual bool slide() { return false; }      // the module glides to this note
	virtual bool retrigger() { return false; }  // a new note straight after the last: drop the gate briefly
	virtual bool fired() = 0;                   // a note started this tick
	virtual float velocity() { return -1.f; }   // 0..1, or < 0 to use the fixed level
	virtual int cyclePos() = 0;                 // falls back when a cycle ends (EOC)
	virtual bool auxIsTrigger() { return false; }
	virtual float auxOut() { return 0.f; }      // volts, or > 0 = fire a trigger
	virtual void auxIn(int i, float volts, bool connected) {}
	virtual void action(bool pressed) {}        // the ACT button / gate
	virtual bool actionLit() { return false; }

	// the grid
	virtual int count(int lane) = 0;
	virtual int get(int lane, int i) = 0;
	virtual void set(int lane, int i, int v) {}
	virtual void range(int lane, int& first, int& last) { first = 0; last = count(lane) - 1; }
	virtual int playPos(int lane) { return -1; }
	virtual bool stepOn(int i) { return true; }
	virtual int barHeightLane() { return -1; }   // a read-only lane drawn from its own scale

	// pattern
	virtual void initFresh() {}
	virtual void clearSteps() {}
	virtual std::vector<std::string> actions() { return {}; }
	virtual void doAction(int i) {}
	virtual json_t* toJson() { return json_object(); }
	virtual void fromJson(json_t* j) {}
	virtual std::string status() { return ""; }   // a short line for the display footer
};

// a common base for engines that look alike (gateOutput, cvOutput, stepFired ...)
template <typename E>
struct EngineAdapter : Adapter {
	E e;
	void setParam(const std::string& key, double v) override { e.setParam(key, v); }
	void reset() override { e.reset(); }
	bool tick(uint32_t t) override { return e.tick(t); }
	void update() override { e.update(); }
	void bar() override { e.barBoundary(); }
	bool gate() override { return e.gateOutput; }
	float cv() override { return (float)e.cvOutput; }
	bool fired() override { return e.stepFired; }
};

// ---------------------------------------------------------------- module

static const int NUM_CTRLS = 4;
static const int PPQN_VALUES[] = {1, 2, 4, 16, 24, 48};
static const int PPQN_COUNT = 6;

struct XfModule : Module {
	enum ParamId {
		RUN_PARAM,
		RESET_PARAM,
		ACTION_PARAM,
		PAGE_PREV_PARAM,
		PAGE_NEXT_PARAM,
		ENUMS(CTRL_PARAM, NUM_CTRLS),
		ENUMS(CTRL_MENU_PARAM, NUM_CTRLS),
		ENUMS(CTRL_ATT_PARAM, NUM_CTRLS),
		ENUMS(CTRL_DEST_PARAM, NUM_CTRLS),
		ENUMS(CTRL_MODE_PARAM, NUM_CTRLS),
		FIELD_PARAM  // then one per field
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		TRANSPOSE_INPUT,
		ENUMS(AUX_INPUT, 2),
		ENUMS(CTRL_CV_INPUT, NUM_CTRLS),
		INPUTS_LEN
	};
	enum OutputId {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		VEL_OUTPUT,
		TRIG_OUTPUT,
		EOC_OUTPUT,
		AUX_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		CLOCK_LIGHT,
		ACTION_LIGHT,
		RESET_LIGHT,
		ENUMS(CTRL_MENU_LIGHT, NUM_CTRLS),
		LIGHTS_LEN
	};

	// One engine per module, or all seven in Multi. Every adapter lives as long
	// as the module, so the panel can still read one that was just swapped out.
	std::vector<std::unique_ptr<Adapter>> adapters;
	std::vector<Info> infos;
	Adapter* ad = nullptr;          // the engine playing
	const Info* info = nullptr;     // its labels
	int cur = 0;
	int engineParam = -1;           // Multi: the Engine setting's param id
	int maxFields = 0;              // field params configured (the largest engine)
	int numFields = 0;              // fields of the engine playing
	// Multi: the settings of the engines not playing
	struct Slot {
		bool has = false;
		std::vector<float> vals;
		float dest[NUM_CTRLS], mode[NUM_CTRLS];
	};
	std::vector<Slot> slots;
	std::vector<int> eff;       // values after the CTRL knobs, what the engine has
	std::vector<int> owner;     // which CTRL (1-4) drives a field, 0 = none
	std::vector<int> applied;   // last values sent to the engine
	int fPpqn = -1, fResetBars = -1, fSlide = -1, fVel = -1;
	bool needApply = true;

	// menus (UI state, saved with the patch)
	int page = 0, selected = 0, scroll = 0;
	int gridLane = 0, gridPage = 0;

	// edits from the UI thread, applied on the audio thread
	struct Edit { int lane, i, v; };
	static const int EDIT_QUEUE = 1024;
	Edit edits[EDIT_QUEUE];
	std::atomic<int> editHead{0}, editTail{0};
	void queueEdit(int lane, int i, int v) {
		int h = editHead.load();
		int n = (h + 1) % EDIT_QUEUE;
		if (n == editTail.load())
			return;  // full: drop
		edits[h] = {lane, i, v};
		editHead.store(n);
	}
	enum { EDIT_ACTION = -1, EDIT_INIT = -2, EDIT_CLEAR = -3 };

	// clock follower
	dsp::SchmittTrigger clockTrigger, resetTrigger, runTrigger;
	dsp::BooleanTrigger resetButton, pagePrev, pageNext, ctrlMenu[NUM_CTRLS];
	bool started = false, haveEdge = false;
	uint32_t absTick = 0, origin = 0, pulseStart = 0;
	int pulseT = 48;
	int ticksThisPulse = 0;
	double pulseSamples = 0, samplesSinceEdge = 0, samplesPerTick = 0;
	float clockPeriod = 0.f;    // seconds between clock pulses
	float msAcc = 0.f, sinceExtra = 1.f;
	int lastCycle = -1;

	// outputs
	float pitchOut = 0.f, slideRate = 0.f, lastTarget = 0.f;
	bool sliding = false;
	float gapLeft = 0.f;
	dsp::PulseGenerator trigPulse, eocPulse, auxPulse;
	float velHeld = 0.f;
	float clockLight = 0.f, resetLight = 0.f;
	int fieldDivider = 0;
	bool wasRunning = true;

	XfModule(Adapter* a, const Info& inf) : XfModule(std::vector<Adapter*>{a}, std::vector<Info>{inf}) {}
	XfModule(const std::vector<Adapter*>& as, const std::vector<Info>& infs);
	bool multi() const { return engineParam >= 0; }
	const Field* fieldAt(int f) {
		Adapter* a = ad;
		return f >= 0 && f < (int)a->fields.size() ? &a->fields[f] : nullptr;
	}
	void storeSlot(int n);
	void loadSlot(int n);
	void switchEngine(int n, bool save);

	int paramOf(int f) const { return FIELD_PARAM + f; }
	int fieldValue(int f) { return (int)std::round(params[FIELD_PARAM + f].getValue()); }
	int ctrlDest(int k) { return (int)std::round(params[CTRL_DEST_PARAM + k].getValue()) - 1; }   // -1 = none
	bool ctrlDirect(int k) { return params[CTRL_MODE_PARAM + k].getValue() > 0.5f; }
	float ctrlAmount(int k);
	void computeFields(bool force);
	int effOf(int f) { return f >= 0 && f < numFields ? eff[f] : 0; }

	// menus: [ENGINE (Multi)] the engine's pages, then one page per CTRL knob
	int pageBase() { return multi() ? 1 : 0; }
	int ctrlPage(int k) { return pageBase() + (int)ad->pages.size() + k; }
	int pageCount() { return ctrlPage(NUM_CTRLS); }
	bool isCtrlPage(int p) { return p >= ctrlPage(0); }
	int ctrlOfPage(int p) { return p - ctrlPage(0); }
	std::vector<std::string> rowChoices(int pid);   // names for a menu, empty = a number
	std::vector<int> pageParams(int p);
	std::string pageTitle(int p);
	int selectedParam() {
		std::vector<int> rows = pageParams(page);
		if (rows.empty())
			return -1;
		return rows[clamp(selected, 0, (int)rows.size() - 1)];
	}
	void goToPage(int p) {
		page = (p % pageCount() + pageCount()) % pageCount();
		selected = 0;
		scroll = 0;
	}
	void nextRow() {
		int n = (int)pageParams(page).size();
		if (n > 0)
			selected = (selected + 1) % n;
	}
	std::string rowLabel(int pid);
	std::string rowValue(int pid, bool effective);
	int rowOwner(int pid);

	// running
	void doReset();
	void runTick();
	void process(const ProcessArgs& args) override;
	void onReset(const ResetEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
	float bpm() { return clockPeriod > 0.f ? 60.f / (clockPeriod * PPQN_VALUES[clamp(effOf(fPpqn), 0, PPQN_COUNT - 1)]) : 0.f; }
};

// ---------------------------------------------------------------- widget

struct XfWidget : ModuleWidget {
	XfWidget(XfModule* module, const Info& info);
	void appendContextMenu(Menu* menu) override;
};

}  // namespace xfm

// one model per module, at file scope: XF_MODEL(Algo, xfm::AlgoAdapter, xfm::algoInfo)
#define XF_MODEL(NAME, ADAPTER, INFO)                                      \
	struct NAME##Module : xfm::XfModule {                                  \
		NAME##Module() : xfm::XfModule(new ADAPTER, INFO()) {}             \
	};                                                                      \
	xfm::Adapter* xfmMake##NAME() { return new ADAPTER; }                  \
	xfm::Info xfmInfo##NAME() { return INFO(); }                           \
	struct NAME##Widget : xfm::XfWidget {                                  \
		NAME##Widget(NAME##Module* m) : xfm::XfWidget(m, INFO()) {}        \
	};                                                                      \
	Model* model##NAME = createModel<NAME##Module, NAME##Widget>(#NAME);
