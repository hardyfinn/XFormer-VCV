// Runs the modules headless (no Rack window) through their clock follower, to
// check the outputs end to end. Built by hand against the Rack source tree:
// see test/README.md.
#include "../src/plugin.hpp"
#include "../src/Xf.hpp"
#include <cstdio>

Plugin* pluginInstance = nullptr;

static int run(Model* model, int ppqn, float bpm, float seconds, const char* name, int engine = -1) {
	Module* m = model->createModule();
	xfm::XfModule* x = dynamic_cast<xfm::XfModule*>(m);
	float sr = 48000.f;
	Module::ProcessArgs args;
	args.sampleRate = sr;
	args.sampleTime = 1.f / sr;
	args.frame = 0;
	// one sample with the clock low first, so the first pulse counts
	// (Multi: pick the engine through its Engine setting, as the display does)
	if (engine >= 0)
		x->params[x->engineParam].setValue((float)engine);
	x->process(args);
	x->inputs[xfm::XfModule::CLOCK_INPUT].setChannels(1);
	int idx = 0;
	while (xfm::PPQN_VALUES[idx] != ppqn) idx++;
	x->params[xfm::XfModule::FIELD_PARAM + x->fPpqn].setValue((float)idx);
	for (int o = 0; o < xfm::XfModule::OUTPUTS_LEN; o++)
		x->outputs[o].setChannels(1);
	double period = 60.0 / bpm / ppqn;
	int gates = 0, trigs = 0, eocs = 0;
	bool lastG = false, lastT = false, lastE = false;
	float minP = 1e9, maxP = -1e9;
	long n = (long)(seconds * sr);
	for (long i = 0; i < n; i++) {
		double t = i / (double)sr;
		double ph = std::fmod(t, period) / period;
		x->inputs[xfm::XfModule::CLOCK_INPUT].setVoltage(ph < 0.5 ? 10.f : 0.f);
		x->process(args);
		args.frame++;
		bool g = x->outputs[xfm::XfModule::GATE_OUTPUT].getVoltage() > 1.f;
		bool tr = x->outputs[xfm::XfModule::TRIG_OUTPUT].getVoltage() > 1.f;
		bool e = x->outputs[xfm::XfModule::EOC_OUTPUT].getVoltage() > 1.f;
		float p = x->outputs[xfm::XfModule::PITCH_OUTPUT].getVoltage();
		if (g && !lastG) gates++;
		if (tr && !lastT) trigs++;
		if (e && !lastE) eocs++;
		if (g) { minP = std::min(minP, p); maxP = std::max(maxP, p); }
		lastG = g; lastT = tr; lastE = e;
	}
	printf("%-10s ppqn %2d: ticks %u, gates %d, trigs %d, eoc %d, pitch %.3f..%.3f, bpm shown %.1f\n", name, ppqn,
		x->absTick, gates, trigs, eocs, minP, maxP, x->bpm());
	json_t* j = x->dataToJson();
	char* s = json_dumps(j, JSON_COMPACT);
	(void)s;
	free(s);
	json_decref(j);
	delete m;
	return gates;
}

// Multi keeps each engine's settings and pattern through a save and load
// while another engine plays
static int checkMulti() {
	int bad = 0;
	Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	xfm::XfModule* x = dynamic_cast<xfm::XfModule*>(modelMulti->createModule());
	// Note: change a setting and a CTRL destination, then switch to Fractal
	x->params[x->engineParam].setValue(2.f);
	x->process(args);
	int fNote = x->ad->fieldIndex("lastStep");
	x->params[xfm::XfModule::FIELD_PARAM + fNote].setValue(7.f);
	x->params[xfm::XfModule::CTRL_DEST_PARAM + 0].setValue(3.f);
	x->process(args);
	x->params[x->engineParam].setValue(6.f);
	x->process(args);
	int fFr = x->ad->fieldIndex("loopLast");
	x->params[xfm::XfModule::FIELD_PARAM + fFr].setValue(31.f);
	x->process(args);
	json_t* j = x->dataToJson();
	xfm::XfModule* y = dynamic_cast<xfm::XfModule*>(modelMulti->createModule());
	y->params[y->engineParam].setValue(6.f);   // as Rack loads the params first
	y->dataFromJson(j);
	y->process(args);
	bool ok = y->cur == 6 && y->params[xfm::XfModule::FIELD_PARAM + fFr].getValue() == 31.f;
	y->params[y->engineParam].setValue(2.f);
	y->process(args);
	ok = ok && y->cur == 2 && y->params[xfm::XfModule::FIELD_PARAM + fNote].getValue() == 7.f
		&& y->params[xfm::XfModule::CTRL_DEST_PARAM + 0].getValue() == 3.f && y->eff[fNote] == 7;
	// a fresh engine starts from its defaults
	y->params[y->engineParam].setValue(4.f);
	y->process(args);
	for (int f = 0; f < y->numFields; f++)
		ok = ok && (int)y->params[xfm::XfModule::FIELD_PARAM + f].getValue() == y->ad->fields[f].def;
	printf("Multi save/load and switching: %s\n", ok ? "ok" : "FAILED");
	json_decref(j);
	delete x;
	delete y;
	return bad + (ok ? 0 : 1);
}

int main() {
	const char* names[] = {"Algo", "Stochastic", "Note", "Indexed", "Discrete", "PhaseFlux", "Fractal"};
	for (int i = 0; i < 7; i++) {
		run(modelMulti, 1, 120.f, 8.f, names[i], i);
		run(modelMulti, 4, 120.f, 8.f, names[i], i);
		run(modelMulti, 24, 120.f, 8.f, names[i], i);
	}
	return checkMulti();
}
