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

// what a CTRL knob shows: its destination and the value it gives
struct CtrlQuantity : ParamQuantity {
	int k = 0;
	std::string getLabel() override {
		XfModule* m = dynamic_cast<XfModule*>(module);
		if (!m)
			return ParamQuantity::getLabel();
		int d = m->ctrlDest(k);
		return string::f("CTRL %d: %s", k + 1, d < 0 ? "no destination" : m->ad->fields[d].label.c_str());
	}
	std::string getDisplayValueString() override {
		XfModule* m = dynamic_cast<XfModule*>(module);
		if (!m)
			return ParamQuantity::getDisplayValueString();
		int d = m->ctrlDest(k);
		if (d < 0)
			return string::f("%d%%", (int)std::round(m->ctrlAmount(k) * 100.f));
		return m->ad->fields[d].text(m->eff[d]);
	}
};

XfModule::XfModule(Adapter* a, const Info& inf) : ad(a), info(inf) {
	numFields = (int)ad->fields.size();
	config(FIELD_PARAM + numFields, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
	configButton(RESET_PARAM, "Reset (back to the start, on the next clock)");
	if (info.action.empty())
		configButton(ACTION_PARAM, "(unused)");
	else if (info.actionLatch)
		configSwitch(ACTION_PARAM, 0.f, 1.f, 0.f, info.action, {"Off", "On"});
	else
		configButton(ACTION_PARAM, info.action);
	configButton(PAGE_PREV_PARAM, "Previous page");
	configButton(PAGE_NEXT_PARAM, "Next page");

	std::vector<std::string> destNames = {"None"};
	for (const Field& f : ad->fields)
		destNames.push_back(f.label);
	for (int k = 0; k < NUM_CTRLS; k++) {
		auto* q = configParam<CtrlQuantity>(CTRL_PARAM + k, 0.f, 1.f, 0.5f, string::f("CTRL %d", k + 1));
		q->k = k;
		configButton(CTRL_MENU_PARAM + k, string::f("CTRL %d menu (destination and mode)", k + 1));
		configParam(CTRL_ATT_PARAM + k, -1.f, 1.f, 1.f, string::f("CTRL %d CV amount (noon = off, left = inverted)", k + 1), "%", 0.f, 100.f);
		int def = ad->ctrlKeys[k].empty() ? -1 : ad->fieldIndex(ad->ctrlKeys[k]);
		configSwitch(CTRL_DEST_PARAM + k, 0.f, (float)numFields, (float)(def + 1), string::f("CTRL %d destination", k + 1), destNames);
		configSwitch(CTRL_MODE_PARAM + k, 0.f, 1.f, 0.f, string::f("CTRL %d mode", k + 1),
			{"Offset (noon = the menu value)", "Direct (the knob sets the value)"});
		configInput(CTRL_CV_INPUT + k, string::f("CTRL %d CV (+-5 V sweeps the whole knob, scaled by CV amount)", k + 1));
	}
	for (int f = 0; f < numFields; f++) {
		const Field& fd = ad->fields[f];
		if (!fd.names.empty())
			configSwitch(FIELD_PARAM + f, fd.min, fd.max, fd.def, fd.label, fd.names);
		else
			configParam(FIELD_PARAM + f, fd.min, fd.max, fd.def, fd.label, fd.unit, 0.f, 1.f, fd.shown)->snapEnabled = true;
	}

	configInput(CLOCK_INPUT, "Clock (pulses per quarter note set on the Clock page)");
	configInput(RESET_INPUT, "Reset");
	configInput(RUN_INPUT, "Run toggle (trigger)");
	configInput(TRANSPOSE_INPUT, "Transpose (1 V/oct, added to the pitch output)");
	for (int i = 0; i < 2; i++)
		configInput(AUX_INPUT + i, info.auxIn[i].empty() ? "(unused)" : info.auxIn[i] + (info.auxInNote[i].empty() ? "" : " (" + info.auxInNote[i] + ")"));
	configOutput(GATE_OUTPUT, "Gate");
	configOutput(PITCH_OUTPUT, "Pitch (1 V/oct)");
	configOutput(VEL_OUTPUT, "Velocity (0-10 V)");
	configOutput(TRIG_OUTPUT, "Trigger (each new note)");
	configOutput(EOC_OUTPUT, "End of cycle");
	configOutput(AUX_OUTPUT, info.auxOut.empty() ? "(unused)" : info.auxOut + (info.auxOutNote.empty() ? "" : " (" + info.auxOutNote + ")"));

	eff.assign(numFields, 0);
	owner.assign(numFields, 0);
	applied.assign(numFields, INT32_MIN);
	fPpqn = ad->fieldIndex("_ppqn");
	fResetBars = ad->fieldIndex("_resetBars");
	fSlide = ad->fieldIndex("_slide");
	fVel = ad->fieldIndex("_vel");
	ad->notify = [this](const std::string& key, int v) {
		int f = ad->fieldIndex(key);
		if (f >= 0) {
			params[FIELD_PARAM + f].setValue((float)v);
			applied[f] = v;
		}
	};
	ad->initFresh();
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
	if (isCtrlPage(p)) {
		int k = p - (int)ad->pages.size();
		rows.push_back(CTRL_DEST_PARAM + k);
		rows.push_back(CTRL_MODE_PARAM + k);
		return rows;
	}
	for (int f : ad->pages[p].fields)
		rows.push_back(FIELD_PARAM + f);
	return rows;
}

std::string XfModule::pageTitle(int p) {
	if (isCtrlPage(p))
		return string::f("CTRL %d", p - (int)ad->pages.size() + 1);
	return ad->pages[p].title;
}

std::string XfModule::rowLabel(int pid) {
	if (pid >= FIELD_PARAM)
		return ad->fields[pid - FIELD_PARAM].label;
	if (pid >= CTRL_MODE_PARAM)
		return "Mode";
	if (pid >= CTRL_DEST_PARAM)
		return "Destination";
	return "";
}

std::string XfModule::rowValue(int pid, bool effective) {
	if (pid >= FIELD_PARAM) {
		int f = pid - FIELD_PARAM;
		return ad->fields[f].text(effective ? eff[f] : clamp(fieldValue(f), ad->fields[f].min, ad->fields[f].max));
	}
	if (pid >= CTRL_MODE_PARAM)
		return ctrlDirect(pid - CTRL_MODE_PARAM) ? "Direct" : "Offset";
	if (pid >= CTRL_DEST_PARAM) {
		int d = ctrlDest(pid - CTRL_DEST_PARAM);
		return d < 0 ? "None" : ad->fields[d].label;
	}
	return "";
}

int XfModule::rowOwner(int pid) {
	if (pid >= FIELD_PARAM)
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
		if (!info.auxIn[i].empty())
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
			int p = (int)ad->pages.size() + k;
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
	if (!info.action.empty())
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
		lights[CTRL_MENU_LIGHT + k].setBrightness(page == (int)ad->pages.size() + k ? 1.f : 0.f);
}

void XfModule::onReset(const ResetEvent& e) {
	Module::onReset(e);
	ad->initFresh();
	page = 0;
	selected = 0;
	scroll = 0;
	gridLane = 0;
	gridPage = 0;
	needApply = true;
}

json_t* XfModule::dataToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "page", json_integer(page));
	json_object_set_new(root, "gridLane", json_integer(gridLane));
	json_object_set_new(root, "gridPage", json_integer(gridPage));
	json_object_set_new(root, "engine", ad->toJson());
	return root;
}

void XfModule::dataFromJson(json_t* root) {
	json_t* j;
	if ((j = json_object_get(root, "page")))
		page = clamp((int)json_integer_value(j), 0, pageCount() - 1);
	if ((j = json_object_get(root, "gridLane")))
		gridLane = clamp((int)json_integer_value(j), 0, std::max(0, (int)ad->lanes.size() - 1));
	if ((j = json_object_get(root, "gridPage")))
		gridPage = std::max(0, (int)json_integer_value(j));
	selected = 0;
	scroll = 0;
	if ((j = json_object_get(root, "engine")))
		ad->fromJson(j);
	needApply = true;
}

}  // namespace xfm
