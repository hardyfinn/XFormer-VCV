// The few Rack library functions module_sim needs, without the rest of Rack.
#include <rack.hpp>
#include <cstdarg>

namespace rack {
namespace engine {
struct Module::Internal {
	bool bypassed = false;
};
Module::Module() { internal = new Internal; }
Module::~Module() {
	for (ParamQuantity* q : paramQuantities) delete q;
	for (PortInfo* p : inputInfos) delete p;
	for (PortInfo* p : outputInfos) delete p;
	for (LightInfo* l : lightInfos) delete l;
	delete internal;
}
void Module::config(int numParams, int numInputs, int numOutputs, int numLights) {
	params.resize(numParams);
	inputs.resize(numInputs);
	outputs.resize(numOutputs);
	lights.resize(numLights);
	paramQuantities.resize(numParams);
	for (int i = 0; i < numParams; i++) configParam(i, 0.f, 1.f, 0.f);
	inputInfos.resize(numInputs);
	for (int i = 0; i < numInputs; i++) configInput(i);
	outputInfos.resize(numOutputs);
	for (int i = 0; i < numOutputs; i++) configOutput(i);
	lightInfos.resize(numLights);
}
void Module::onReset(const ResetEvent& e) {}
}  // namespace engine
namespace string {
std::string f(const char* format, ...) {
	va_list args;
	va_start(args, format);
	char buf[1024];
	vsnprintf(buf, sizeof buf, format, args);
	va_end(args);
	return buf;
}
}  // namespace string
}  // namespace rack
