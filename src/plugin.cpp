#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelAlgo);
	p->addModel(modelStochastic);
	p->addModel(modelNote);
	p->addModel(modelIndexed);
	p->addModel(modelDiscrete);
	p->addModel(modelPhaseFlux);
	p->addModel(modelFractal);
	p->addModel(modelMulti);
}
