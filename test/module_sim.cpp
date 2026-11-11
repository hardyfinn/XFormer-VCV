// Runs the modules headless (no Rack window) through their clock follower, to
// check the outputs end to end. Built by hand against the Rack source tree:
// see test/README.md.
#include "../src/plugin.hpp"
#include "../src/Xf.hpp"
#include <cstdio>

Plugin* pluginInstance = nullptr;

static void run(Model* model, int ppqn, float bpm, float seconds, const char* name) {
	Module* m = model->createModule();
	xfm::XfModule* x = dynamic_cast<xfm::XfModule*>(m);
	float sr = 48000.f;
	Module::ProcessArgs args;
	args.sampleRate = sr;
	args.sampleTime = 1.f / sr;
	args.frame = 0;
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
}

int main() {
	Model* models[] = {modelAlgo, modelStochastic, modelNote, modelIndexed, modelDiscrete, modelPhaseFlux, modelFractal};
	const char* names[] = {"Algo", "Stochastic", "Note", "Indexed", "Discrete", "PhaseFlux", "Fractal"};
	for (int i = 0; i < 7; i++) {
		run(models[i], 1, 120.f, 8.f, names[i]);
		run(models[i], 4, 120.f, 8.f, names[i]);
		run(models[i], 24, 120.f, 8.f, names[i]);
	}
	return 0;
}
