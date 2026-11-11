// The module half of the shared frame: settings, CTRL knobs, the external
// clock follower and the outputs.
#include "Xf.hpp"

namespace xfm {

std::vector<std::string> scaleNames() {
	std::vector<std::string> v;
	for (const auto& s : xf::scales())
		v.push_back(s.name);
	return v;
}
std::vector<std::string> divisorNames() {
	std::vector<std::string> v;
	for (int i = 0; i < xf::DIVISOR_COUNT; i++)
		v.push_back(xf::DIVISORS[i].name);
	return v;
}
std::vector<std::string> rootNames() {
	return std::vector<std::string>(xf::ROOT_NAMES, xf::ROOT_NAMES + 12);
}
std::vector<std::string> runModeNames() {
	return std::vector<std::string>(xf::RUN_MODE_NAMES, xf::RUN_MODE_NAMES + 6);
}
int divisorIndexOf(int ticks) {
	for (int i = 0; i < xf::DIVISOR_COUNT; i++)
		if (xf::DIVISORS[i].v == ticks)
			return i;
	return 3;
}

// ---------------------------------------------------------------- setup

static XfModule* xfOf(Module* m) { return dynamic_cast<XfModule*>(m); }

// what a CTRL knob shows: its destination and the value it gives
struct CtrlQuantity : ParamQuantity {
	int k = 0;
	std::string getLabel() override {
		XfModule* m = xfOf(module);
		if (!m)
			return ParamQuantity::getLabel();
		const Field* fd = m->fieldAt(m->ctrlDest(k));
		return string::f("CTRL %d: %s", k + 1, fd ? fd->label.c_str() : "no destination");
	}
	std::string getDisplayValueString() override {
		XfModule* m = xfOf(module);
		if (!m)
			return ParamQuantity::getDisplayValueString();
		int d = m->ctrlDest(k);
		const Field* fd = m->fieldAt(d);
		if (!fd)
			return string::f("%d%%", (int)std::round(m->ctrlAmount(k) * 100.f));
		return fd->text(m->eff[d]);
	}
};

// A setting. Its range, name and value names come from the engine playing, so
// the Multi module can reuse the same params for every engine.
struct FieldQuantity : ParamQuantity {
	int f = 0;
	const Field* fd() {
		XfModule* m = xfOf(module);
		return m ? m->fieldAt(f) : nullptr;
	}
	float getMinValue() override { const Field* d = fd(); return d ? (float)d->min : 0.f; }
	float getMaxValue() override { const Field* d = fd(); return d ? (float)d->max : 0.f; }
	float getDefaultValue() override { const Field* d = fd(); return d ? (float)d->def : 0.f; }
	std::string getLabel() override { const Field* d = fd(); return d ? d->label : "(not used by this engine)"; }
	std::string getUnit() override { return ""; }
	std::string getDisplayValueString() override {
		const Field* d = fd();
		if (!d)
			return "";
		return d->text(clamp((int)std::round(getValue()), d->min, d->max));
	}
	void setDisplayValueString(std::string s) override {
		const Field* d = fd();
		if (!d)
			return;
		for (size_t i = 0; i < d->names.size(); i++)
			if (string::lowercase(d->names[i]) == string::lowercase(s)) {
				setValue((float)(d->min + (int)i));
				return;
			}
		if (d->names.empty())
			setValue((float)(std::atoi(s.c_str()) - d->shown));
	}
};

// a CTRL knob's destination: None or one of the engine's settings
struct CtrlDestQuantity : ParamQuantity {
	int k = 0;
	float getMaxValue() override { XfModule* m = xfOf(module); return m ? (float)m->ad->fields.size() : 0.f; }
	float getDefaultValue() override {
		XfModule* m = xfOf(module);
		if (!m || m->ad->ctrlKeys[k].empty())
			return 0.f;
		return (float)(m->ad->fieldIndex(m->ad->ctrlKeys[k]) + 1);
	}
	std::string getDisplayValueString() override {
		XfModule* m = xfOf(module);
		const Field* fd = m ? m->fieldAt(m->ctrlDest(k)) : nullptr;
		return fd ? fd->label : "None";
	}
};

struct ActionQuantity : ParamQuantity {
	std::string getLabel() override {
		XfModule* m = xfOf(module);
		if (!m)
			return "Action";
		return m->info->action.empty() ? "(not used by this engine)" : m->info->action;
	}
	std::string getDisplayValueString() override { return getValue() > 0.5f ? "Pressed" : "Released"; }
	std::string getUnit() override { return ""; }
};

struct AuxPortInfo : PortInfo {
	bool out = false;
	int j = 0;
	std::string getName() override {
		XfModule* m = xfOf(module);
		if (!m)
			return name;
		const Info& in = *m->info;
		std::string n = out ? in.auxOut : in.auxIn[j];
		std::string note = out ? in.auxOutNote : in.auxInNote[j];
		if (n.empty())
			return "(not used by this engine)";
		return n + (note.empty() ? "" : " (" + note + ")");
	}
};

XfModule::XfModule(const std::vector<Adapter*>& as, const std::vector<Info>& infs) {
	for (Adapter* a : as)
		adapters.emplace_back(a);
	infos = infs;
	slots.resize(adapters.size());
	ad = adapters[0].get();
	info = &infos[0];
	for (auto& a : adapters)
		maxFields = std::max(maxFields, (int)a->fields.size());
	numFields = (int)ad->fields.size();
	bool isMulti = adapters.size() > 1;
	config(FIELD_PARAM + maxFields + (isMulti ? 1 : 0), INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
	configButton(RESET_PARAM, "Reset (back to the start, on the next clock)");
	configParam<ActionQuantity>(ACTION_PARAM, 0.f, 1.f, 0.f, "Action");
	configButton(PAGE_PREV_PARAM, "Previous page");
	configButton(PAGE_NEXT_PARAM, "Next page");

	for (int k = 0; k < NUM_CTRLS; k++) {
		auto* q = configParam<CtrlQuantity>(CTRL_PARAM + k, 0.f, 1.f, 0.5f, string::f("CTRL %d", k + 1));
		q->k = k;
		configButton(CTRL_MENU_PARAM + k, string::f("CTRL %d menu (destination and mode)", k + 1));
		configParam(CTRL_ATT_PARAM + k, -1.f, 1.f, 1.f, string::f("CTRL %d CV amount (noon = off, left = inverted)", k + 1), "%", 0.f, 100.f);
		auto* dq = configParam<CtrlDestQuantity>(CTRL_DEST_PARAM + k, 0.f, (float)maxFields, 0.f, string::f("CTRL %d destination", k + 1));
		dq->k = k;
		dq->snapEnabled = true;
		dq->randomizeEnabled = false;
		params[CTRL_DEST_PARAM + k].setValue(dq->getDefaultValue());
		configSwitch(CTRL_MODE_PARAM + k, 0.f, 1.f, 0.f, string::f("CTRL %d mode", k + 1),
			{"Offset (noon = the menu value)", "Direct (the knob sets the value)"});
		configInput(CTRL_CV_INPUT + k, string::f("CTRL %d CV (+-5 V sweeps the whole knob, scaled by CV amount)", k + 1));
	}
	for (int f = 0; f < maxFields; f++) {
		auto* q = configParam<FieldQuantity>(FIELD_PARAM + f, 0.f, 1.f, 0.f, "");
		q->f = f;
		q->snapEnabled = true;
		params[FIELD_PARAM + f].setValue(q->getDefaultValue());
	}
	if (isMulti) {
		engineParam = FIELD_PARAM + maxFields;
		std::vector<std::string> names;
		for (const Info& in : infos)
			names.push_back(in.title);
		SwitchQuantity* q = configSwitch(engineParam, 0.f, (float)(names.size() - 1), 0.f, "Engine", names);
		q->resetEnabled = false;      // Initialize keeps the engine
		q->randomizeEnabled = false;
	}

	configInput(CLOCK_INPUT, "Clock (pulses per quarter note set on the Clock page)");
	configInput(RESET_INPUT, "Reset");
	configInput(RUN_INPUT, "Run toggle (trigger)");
	configInput(TRANSPOSE_INPUT, "Transpose (1 V/oct, added to the pitch output)");
	for (int i = 0; i < 2; i++)
		configInput<AuxPortInfo>(AUX_INPUT + i, "Aux")->j = i;
	configOutput(GATE_OUTPUT, "Gate");
	configOutput(PITCH_OUTPUT, "Pitch (1 V/oct)");
	configOutput(VEL_OUTPUT, "Velocity (0-10 V)");
	configOutput(TRIG_OUTPUT, "Trigger (each new note)");
	configOutput(EOC_OUTPUT, "End of cycle");
	configOutput<AuxPortInfo>(AUX_OUTPUT, "Aux")->out = true;

	eff.assign(maxFields, 0);
	owner.assign(maxFields, 0);
	applied.assign(maxFields, INT32_MIN);
	for (auto& up : adapters) {
		Adapter* a = up.get();
		a->notify = [this, a](const std::string& key, int v) {
			if (a != ad)
				return;
			int f = a->fieldIndex(key);
			if (f >= 0) {
				params[FIELD_PARAM + f].setValue((float)v);
				applied[f] = v;
			}
		};
		a->initFresh();
	}
	switchEngine(0, false);
}

// Multi: keep the settings of the engine being left, bring back the next one's
void XfModule::storeSlot(int n) {
	Slot& s = slots[n];
	int nf = (int)adapters[n]->fields.size();
	s.vals.resize(nf);
	for (int f = 0; f < nf; f++)
		s.vals[f] = params[FIELD_PARAM + f].getValue();
	for (int k = 0; k < NUM_CTRLS; k++) {
		s.dest[k] = params[CTRL_DEST_PARAM + k].getValue();
		s.mode[k] = params[CTRL_MODE_PARAM + k].getValue();
	}
	s.has = true;
}

void XfModule::loadSlot(int n) {
	Slot& s = slots[n];
	Adapter* a = adapters[n].get();
	int nf = (int)a->fields.size();
	for (int f = 0; f < maxFields; f++) {
		float v = 0.f;
		if (f < nf)
			v = (s.has && f < (int)s.vals.size()) ? s.vals[f] : (float)a->fields[f].def;
		params[FIELD_PARAM + f].setValue(v);
	}
	for (int k = 0; k < NUM_CTRLS; k++) {
		float def = a->ctrlKeys[k].empty() ? 0.f : (float)(a->fieldIndex(a->ctrlKeys[k]) + 1);
		params[CTRL_DEST_PARAM + k].setValue(s.has ? s.dest[k] : def);
		params[CTRL_MODE_PARAM + k].setValue(s.has ? s.mode[k] : 0.f);
	}
}

void XfModule::switchEngine(int n, bool save) {
	n = clamp(n, 0, (int)adapters.size() - 1);
	if (save && n != cur) {
		storeSlot(cur);
		ad->stop();
	}
	// the menus and grid first, so the panel never pairs a page with the wrong engine
	page = 0;
	selected = 0;
	scroll = 0;
	gridLane = 0;
	gridPage = 0;
	Adapter* a = adapters[n].get();
	ad = a;
	info = &infos[n];
	cur = n;
	numFields = (int)a->fields.size();
	if (save || multi())
		loadSlot(n);
	if (engineParam >= 0)
		params[engineParam].setValue((float)n);
	fPpqn = a->fieldIndex("_ppqn");
	fResetBars = a->fieldIndex("_resetBars");
	fSlide = a->fieldIndex("_slide");
	fVel = a->fieldIndex("_vel");
	std::fill(applied.begin(), applied.end(), INT32_MIN);
	lastCycle = -1;
	needApply = true;
}

float XfModule::ctrlAmount(int k) {
	float cv = inputs[CTRL_CV_INPUT + k].getVoltage() * params[CTRL_ATT_PARAM + k].getValue();
	return clamp(params[CTRL_PARAM + k].getValue() + cv / 10.f, 0.f, 1.f);
}

void XfModule::computeFields(bool force) {
	for (int f = 0; f < numFields; f++) {
		eff[f] = clamp(fieldValue(f), ad->fields[f].min, ad->fields[f].max);
		owner[f] = 0;
	}
	for (int k = 0; k < NUM_CTRLS; k++) {
		int d = ctrlDest(k);
		if (d < 0 || d >= numFields)
			continue;
		const Field& fd = ad->fields[d];
		float a = ctrlAmount(k);
		int span = fd.max - fd.min;
		if (ctrlDirect(k))
			eff[d] = fd.min + (int)std::round(a * span);
		else
			eff[d] = clamp(eff[d] + (int)std::round((a - 0.5f) * span), fd.min, fd.max);
		owner[d] = k + 1;
	}
	// send what changed; fields that limit each other go together, twice
	bool changed = false;
	for (int pass = 0; pass < (force ? 2 : 1); pass++) {
		for (int f = 0; f < numFields; f++) {
			if (!force && eff[f] == applied[f])
				continue;
			applied[f] = eff[f];
			ad->setField(f, eff[f]);
			changed = true;
			for (const auto& g : ad->linked) {
				if (std::find(g.begin(), g.end(), f) == g.end())
					continue;
				for (int r = 0; r < 2; r++)
					for (int o : g)
						if (o != f)
							ad->setField(o, eff[o]);
			}
		}
	}
	(void)changed;
}

std::vector<int> XfModule::pageParams(int p) {
	std::vector<int> rows;
	if (p < 0 || p >= pageCount())
		return rows;
	if (multi() && p == 0) {
		rows.push_back(engineParam);
		return rows;
	}
	if (isCtrlPage(p)) {
		int k = ctrlOfPage(p);
		rows.push_back(CTRL_DEST_PARAM + k);
		rows.push_back(CTRL_MODE_PARAM + k);
		return rows;
	}
	for (int f : ad->pages[p - pageBase()].fields)
		rows.push_back(FIELD_PARAM + f);
	return rows;
}

std::string XfModule::pageTitle(int p) {
	if (multi() && p == 0)
		return "ENGINE";
	if (isCtrlPage(p))
		return string::f("CTRL %d", ctrlOfPage(p) + 1);
	if (p < 0 || p >= pageCount())
		return "";
	return ad->pages[p - pageBase()].title;
}

std::string XfModule::rowLabel(int pid) {
	if (pid == engineParam)
		return "Engine";
	if (pid >= FIELD_PARAM) {
		const Field* fd = fieldAt(pid - FIELD_PARAM);
		return fd ? fd->label : "";
	}
	if (pid >= CTRL_MODE_PARAM)
		return "Mode";
	if (pid >= CTRL_DEST_PARAM)
		return "Destination";
	return "";
}

std::string XfModule::rowValue(int pid, bool effective) {
	if (pid == engineParam)
		return info->title;
	if (pid >= FIELD_PARAM) {
		int f = pid - FIELD_PARAM;
		const Field* fd = fieldAt(f);
		if (!fd)
			return "";
		return fd->text(effective ? eff[f] : clamp(fieldValue(f), fd->min, fd->max));
	}
	if (pid >= CTRL_MODE_PARAM)
		return ctrlDirect(pid - CTRL_MODE_PARAM) ? "Direct" : "Offset";
	if (pid >= CTRL_DEST_PARAM) {
		const Field* fd = fieldAt(ctrlDest(pid - CTRL_DEST_PARAM));
		return fd ? fd->label : "None";
	}
	return "";
}

std::vector<std::string> XfModule::rowChoices(int pid) {
	if (pid == engineParam) {
		std::vector<std::string> v;
		for (const Info& in : infos)
			v.push_back(in.title);
		return v;
	}
	if (pid >= FIELD_PARAM) {
		const Field* fd = fieldAt(pid - FIELD_PARAM);
		return fd ? fd->names : std::vector<std::string>();
	}
	if (pid >= CTRL_MODE_PARAM)
		return {"Offset", "Direct"};
	if (pid >= CTRL_DEST_PARAM) {
		std::vector<std::string> v = {"None"};
		for (const Field& f : ad->fields)
			v.push_back(f.label);
		return v;
	}
	return {};
}

int XfModule::rowOwner(int pid) {
	if (pid != engineParam && pid >= FIELD_PARAM && pid - FIELD_PARAM < (int)owner.size())
		return owner[pid - FIELD_PARAM];
	return 0;
}

// ---------------------------------------------------------------- running

void XfModule::doReset() {
	ad->reset();
	started = false;
	absTick = 0;
	origin = 0;
	pulseStart = 0;
	ticksThisPulse = 0;
	pulseSamples = 0;
	lastCycle = -1;
	gapLeft = 0.f;
	sliding = false;
	resetLight = 1.f;
}

void XfModule::runTick() {
	uint32_t rel = absTick - origin;
	int mt = std::max(1, ad->measureTicks);
	if (rel > 0 && rel % mt == 0)
		ad->bar();
	int rb = fResetBars >= 0 ? eff[fResetBars] : 0;
	if (!ad->ownsResetBars() && rb > 0 && rel > 0 && rel % (uint32_t)(rb * mt) == 0) {
		if (ad->barsReset()) {
			origin = absTick;
			rel = 0;
			lastCycle = -1;
		}
	}
	bool upd = ad->tick(rel);
	if (upd && sinceExtra >= 0.025f) {
		sinceExtra = 0.f;
		ad->update();
	}
	if (ad->fired()) {
		trigPulse.trigger(1e-3f);
		float v = ad->velocity();
		if (v >= 0.f)
			velHeld = v * 10.f;
	}
	if (ad->retrigger())
		gapLeft = 1e-3f;
	int cp = ad->cyclePos();
	if (lastCycle >= 0 && cp < lastCycle)
		eocPulse.trigger(1e-3f);
	lastCycle = cp;
	if (ad->auxIsTrigger() && ad->auxOut() > 0.f)
		auxPulse.trigger(1e-3f);
	absTick++;
}

void XfModule::process(const ProcessArgs& args) {
	const float dt = args.sampleTime;

	// Multi: the Engine setting changed
	if (engineParam >= 0) {
		int want = clamp((int)std::round(params[engineParam].getValue()), 0, (int)adapters.size() - 1);
		if (want != cur)
			switchEngine(want, true);
	}

	// settings: the menu values plus the CTRL knobs
	if (needApply) {
		computeFields(true);
		doReset();
		needApply = false;
	}
	else if (++fieldDivider >= 16) {
		fieldDivider = 0;
		computeFields(false);
	}
	for (int i = 0; i < 2; i++)
		if (!info->auxIn[i].empty())
			ad->auxIn(i, inputs[AUX_INPUT + i].getVoltage(), inputs[AUX_INPUT + i].isConnected());

	// edits from the panel
	while (editTail.load() != editHead.load()) {
		int t = editTail.load();
		Edit e = edits[t];
		if (e.lane >= 0)
			ad->set(e.lane, e.i, e.v);
		else if (e.lane == EDIT_ACTION)
			ad->doAction(e.i);
		else if (e.lane == EDIT_INIT)
			ad->initFresh();
		else if (e.lane == EDIT_CLEAR)
			ad->clearSteps();
		editTail.store((t + 1) % EDIT_QUEUE);
	}

	// page buttons
	if (pagePrev.process(params[PAGE_PREV_PARAM].getValue() > 0.f))
		goToPage(page - 1);
	if (pageNext.process(params[PAGE_NEXT_PARAM].getValue() > 0.f))
		goToPage(page + 1);
	for (int k = 0; k < NUM_CTRLS; k++)
		if (ctrlMenu[k].process(params[CTRL_MENU_PARAM + k].getValue() > 0.f)) {
			int p = ctrlPage(k);
			if (page == p)
				nextRow();
			else
				goToPage(p);
		}

	// run, reset, action
	if (runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
		params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
	bool running = params[RUN_PARAM].getValue() > 0.5f;
	if (running != wasRunning && !running)
		ad->stop();
	wasRunning = running;
	bool rst = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
	if (resetButton.process(params[RESET_PARAM].getValue() > 0.f))
		rst = true;
	if (rst)
		doReset();
	if (!info->action.empty())
		ad->action(params[ACTION_PARAM].getValue() > 0.5f);

	// external clock: each pulse is 192 / PPQN engine ticks, spread evenly over
	// the time between the last two pulses and re-synced on every pulse
	samplesSinceEdge += 1.0;
	int ppqn = PPQN_VALUES[clamp(fPpqn >= 0 ? eff[fPpqn] : 2, 0, PPQN_COUNT - 1)];
	int T = std::max(1, xf::CONFIG_PPQN / ppqn);
	if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
		if (haveEdge) {
			float period = (float)(samplesSinceEdge * dt);
			if (period < 8.f) {
				clockPeriod = period;
				samplesPerTick = samplesSinceEdge / T;
			}
		}
		else if (samplesPerTick <= 0.0)
			samplesPerTick = args.sampleRate * 0.5 / ppqn / T;   // until the clock is measured: 120 BPM
		haveEdge = true;
		samplesSinceEdge = 0.0;
		clockLight = 1.f;
		if (running) {
			if (!started) {
				started = true;
				absTick = origin;
			}
			else {
				// finish the last pulse's ticks if the clock sped up
				uint32_t end = pulseStart + pulseT;
				int guard = 0;
				while (absTick < end && guard++ < 1024)
					runTick();
			}
			pulseStart = absTick;
			pulseT = T;
			runTick();
			ticksThisPulse = 1;
			pulseSamples = 0.0;
		}
	}
	else if (running && started && ticksThisPulse < pulseT && samplesPerTick > 0.0) {
		pulseSamples += 1.0;
		while (ticksThisPulse < pulseT && pulseSamples >= ticksThisPulse * samplesPerTick) {
			runTick();
			ticksThisPulse++;
		}
	}
	if (clockPeriod > 0.f && samplesSinceEdge * dt > 8.f)
		clockPeriod = 0.f;  // the clock stopped

	// the engines' millisecond work (gate triggers, glides, ratchets)
	msAcc += dt;
	sinceExtra += dt;
	while (msAcc >= 0.001f) {
		msAcc -= 0.001f;
		ad->update();
	}

	// outputs
	bool g = running && started && ad->gate();
	if (gapLeft > 0.f) {
		gapLeft -= dt;
		g = false;
	}
	float target = ad->cv();
	if (target != lastTarget) {
		float slideMs = fSlide >= 0 ? (float)eff[fSlide] : 0.f;
		if (ad->slide() && slideMs > 0.f) {
			sliding = true;
			slideRate = std::fabs(target - pitchOut) / (slideMs * 0.001f);
		}
		else
			sliding = false;
		lastTarget = target;
	}
	if (sliding) {
		float step = slideRate * dt;
		if (std::fabs(target - pitchOut) <= step) {
			pitchOut = target;
			sliding = false;
		}
		else
			pitchOut += target > pitchOut ? step : -step;
	}
	else
		pitchOut = target;
	if (ad->velocity() < 0.f)
		velHeld = fVel >= 0 ? eff[fVel] * 0.1f : 8.f;

	outputs[GATE_OUTPUT].setVoltage(g ? 10.f : 0.f);
	outputs[PITCH_OUTPUT].setVoltage(pitchOut + inputs[TRANSPOSE_INPUT].getVoltage());
	outputs[VEL_OUTPUT].setVoltage(velHeld);
	outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(dt) ? 10.f : 0.f);
	outputs[EOC_OUTPUT].setVoltage(eocPulse.process(dt) ? 10.f : 0.f);
	if (ad->auxIsTrigger())
		outputs[AUX_OUTPUT].setVoltage(auxPulse.process(dt) ? 10.f : 0.f);
	else
		outputs[AUX_OUTPUT].setVoltage(ad->auxOut());

	// lights
	lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
	lights[RESET_LIGHT].setSmoothBrightness(resetLight, dt);
	resetLight = std::max(0.f, resetLight - dt * 8.f);
	clockLight = std::max(0.f, clockLight - dt * 10.f);
	lights[CLOCK_LIGHT].setBrightness(clockLight);
	lights[ACTION_LIGHT].setBrightness(ad->actionLit() ? 1.f : 0.f);
	for (int k = 0; k < NUM_CTRLS; k++)
		lights[CTRL_MENU_LIGHT + k].setBrightness(page == ctrlPage(k) ? 1.f : 0.f);
}

void XfModule::onReset(const ResetEvent& e) {
	Module::onReset(e);   // the field params go to the playing engine's defaults
	for (auto& a : adapters)
		a->initFresh();
	for (auto& sl : slots)
		sl.has = false;
	switchEngine(cur, false);
	loadSlot(cur);
}

json_t* XfModule::dataToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "page", json_integer(page));
	json_object_set_new(root, "gridLane", json_integer(gridLane));
	json_object_set_new(root, "gridPage", json_integer(gridPage));
	if (!multi()) {
		json_object_set_new(root, "engine", ad->toJson());
		return root;
	}
	// Multi: every engine's settings and pattern. The playing engine's settings
	// are saved here too, since Rack clamps params to the default engine's
	// ranges when it loads them.
	int c = cur;
	json_object_set_new(root, "current", json_integer(c));
	json_t* arr = json_array();
	for (size_t n = 0; n < adapters.size(); n++) {
		json_t* o = json_object();
		json_object_set_new(o, "slug", json_string(infos[n].slug.c_str()));
		// the playing engine's settings are in the params, the others' in their slots
		bool live = (int)n == c;
		if (live || slots[n].has) {
			json_t* v = json_array();
			int nf = (int)adapters[n]->fields.size();
			for (int f = 0; f < nf; f++)
				json_array_append_new(v, json_real(live ? params[FIELD_PARAM + f].getValue() : slots[n].vals[f]));
			json_object_set_new(o, "fields", v);
			json_t* d = json_array();
			json_t* md = json_array();
			for (int k = 0; k < NUM_CTRLS; k++) {
				json_array_append_new(d, json_real(live ? params[CTRL_DEST_PARAM + k].getValue() : slots[n].dest[k]));
				json_array_append_new(md, json_real(live ? params[CTRL_MODE_PARAM + k].getValue() : slots[n].mode[k]));
			}
			json_object_set_new(o, "ctrlDest", d);
			json_object_set_new(o, "ctrlMode", md);
		}
		json_object_set_new(o, "engine", adapters[n]->toJson());
		json_array_append_new(arr, o);
	}
	json_object_set_new(root, "engines", arr);
	return root;
}

void XfModule::dataFromJson(json_t* root) {
	json_t* j;
	if (multi() && (j = json_object_get(root, "engines"))) {
		for (size_t i = 0; i < json_array_size(j); i++) {
			json_t* o = json_array_get(j, i);
			json_t* sl = json_object_get(o, "slug");
			int n = -1;
			for (size_t q = 0; q < infos.size(); q++)
				if (sl && infos[q].slug == json_string_value(sl))
					n = (int)q;
			if (n < 0)
				continue;
			Slot& s = slots[n];
			json_t* v = json_object_get(o, "fields");
			s.has = v != nullptr;
			if (v) {
				const Adapter* a = adapters[n].get();
				s.vals.resize(a->fields.size());
				for (size_t f = 0; f < a->fields.size(); f++)
					s.vals[f] = f < json_array_size(v) ? (float)json_number_value(json_array_get(v, f)) : (float)a->fields[f].def;
				json_t* d = json_object_get(o, "ctrlDest");
				json_t* md = json_object_get(o, "ctrlMode");
				for (int k = 0; k < NUM_CTRLS; k++) {
					s.dest[k] = d ? (float)json_number_value(json_array_get(d, k)) : 0.f;
					s.mode[k] = md ? (float)json_number_value(json_array_get(md, k)) : 0.f;
				}
			}
			if (json_t* e = json_object_get(o, "engine"))
				adapters[n]->fromJson(e);
		}
		int c = (j = json_object_get(root, "current")) ? (int)json_integer_value(j) : 0;
		switchEngine(c, false);   // also loads its saved settings into the params
	}
	if ((j = json_object_get(root, "page")))
		page = clamp((int)json_integer_value(j), 0, pageCount() - 1);
	if ((j = json_object_get(root, "gridLane")))
		gridLane = clamp((int)json_integer_value(j), 0, std::max(0, (int)ad->lanes.size() - 1));
	if ((j = json_object_get(root, "gridPage")))
		gridPage = std::max(0, (int)json_integer_value(j));
	selected = 0;
	scroll = 0;
	if (!multi() && (j = json_object_get(root, "engine")))
		ad->fromJson(j);
	needApply = true;
}

}  // namespace xfm
