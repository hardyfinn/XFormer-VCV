// MULTI: all seven X|FORMER engines behind one panel. The ENGINE page (the
// first page of the display) picks which one plays; each engine keeps its own
// settings, CTRL assignments and pattern while another one is playing.
#include "../Xf.hpp"

#define XF_ENGINE(NAME)              \
	xfm::Adapter* xfmMake##NAME();   \
	xfm::Info xfmInfo##NAME();
XF_ENGINE(Algo)
XF_ENGINE(Stochastic)
XF_ENGINE(Note)
XF_ENGINE(Indexed)
XF_ENGINE(Discrete)
XF_ENGINE(PhaseFlux)
XF_ENGINE(Fractal)

namespace xfm {

static Info multiInfo() {
	Info i;
	i.slug = "Multi";
	i.multi = true;
	// what the browser shows before a module exists: the first engine's labels
	Info a = xfmInfoAlgo();
	i.blurb = a.blurb;
	for (int j = 0; j < 2; j++) {
		i.auxIn[j] = a.auxIn[j];
		i.auxInNote[j] = a.auxInNote[j];
	}
	i.auxOut = a.auxOut;
	i.auxOutNote = a.auxOutNote;
	i.actionLabel = a.actionLabel;
	i.title = a.title;
	return i;
}

struct MultiModule : XfModule {
	MultiModule()
		: XfModule({xfmMakeAlgo(), xfmMakeStochastic(), xfmMakeNote(), xfmMakeIndexed(), xfmMakeDiscrete(),
		             xfmMakePhaseFlux(), xfmMakeFractal()},
		           {xfmInfoAlgo(), xfmInfoStochastic(), xfmInfoNote(), xfmInfoIndexed(), xfmInfoDiscrete(),
		             xfmInfoPhaseFlux(), xfmInfoFractal()}) {}
};

struct MultiWidget : XfWidget {
	MultiWidget(MultiModule* m) : XfWidget(m, multiInfo()) {}
};

}  // namespace xfm

Model* modelMulti = createModel<xfm::MultiModule, xfm::MultiWidget>("Multi");
