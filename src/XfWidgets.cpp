// The panel half of the shared frame: the menu display, the DATA encoder,
// the step grid and the CTRL knobs.
#include "Xf.hpp"
#include "Layout.hpp"

namespace xfm {

static const NVGcolor AMBER = nvgRGB(0xff, 0xb3, 0x47);
static const NVGcolor AMBER_DIM = nvgRGB(0x6a, 0x48, 0x1c);
static const NVGcolor CYAN = nvgRGB(0x5f, 0xd0, 0xe4);
static const NVGcolor CYAN_DIM = nvgRGB(0x1d, 0x4c, 0x55);
static const NVGcolor SCREEN = nvgRGB(0x0b, 0x0c, 0x0e);
static const NVGcolor SHADE = nvgRGB(0x16, 0x17, 0x1b);

static void text(NVGcontext* vg, float x, float y, const std::string& s, NVGcolor c, float size, int align = NVG_ALIGN_LEFT) {
	nvgFontSize(vg, size);
	nvgFillColor(vg, c);
	nvgTextAlign(vg, align | NVG_ALIGN_BASELINE);
	nvgText(vg, x, y, s.c_str(), NULL);
}

static std::string noteName(float volts) {
	int semi = (int)std::round(volts * 12.f);
	int oct = (int)std::floor(semi / 12.0) + 4;
	return string::f("%s%d", xf::ROOT_NAMES[((semi % 12) + 12) % 12], oct);
}

// ---------------------------------------------------------------- menus

static void appendParamItem(ui::Menu* menu, XfModule* m, int pid) {
	ParamQuantity* q = m->paramQuantities[pid];
	std::vector<std::string> choices = m->rowChoices(pid);
	if (choices.empty())
		if (SwitchQuantity* sq = dynamic_cast<SwitchQuantity*>(q))
			choices = sq->labels;
	if (!choices.empty()) {
		int mn = (int)q->getMinValue();
		menu->addChild(createIndexSubmenuItem(m->rowLabel(pid), choices,
			[=]() { return (size_t)((int)std::round(q->getValue()) - mn); },
			[=](size_t i) { q->setValue((float)((int)i + mn)); }));
		return;
	}
	auto* s = new ui::Slider;
	s->quantity = q;
	s->box.size.x = 220.f;
	menu->addChild(s);
}

static void appendPageMenu(ui::Menu* menu, XfModule* m, int page) {
	for (int pid : m->pageParams(page))
		appendParamItem(menu, m, pid);
}

static void appendCtrlMenu(ui::Menu* menu, XfModule* m, int k) {
	menu->addChild(createSubmenuItem(string::f("CTRL %d", k + 1), m->rowValue(XfModule::CTRL_DEST_PARAM + k, false), [=](ui::Menu* sub) {
		// destinations grouped by page, so the list stays short
		ParamQuantity* q = m->paramQuantities[XfModule::CTRL_DEST_PARAM + k];
		sub->addChild(createCheckMenuItem("None", "", [=]() { return m->ctrlDest(k) < 0; }, [=]() { q->setValue(0.f); }));
		for (int p = 0; p < (int)m->ad->pages.size(); p++) {
			std::vector<int> fs = m->ad->pages[p].fields;
			sub->addChild(createSubmenuItem(m->ad->pages[p].title, "", [=](ui::Menu* s2) {
				for (int f : fs)
					s2->addChild(createCheckMenuItem(m->ad->fields[f].label, "", [=]() { return m->ctrlDest(k) == f; },
						[=]() { q->setValue((float)(f + 1)); }));
			}));
		}
		sub->addChild(new MenuSeparator);
		appendParamItem(sub, m, XfModule::CTRL_MODE_PARAM + k);
	}));
}

// ---------------------------------------------------------------- display + encoder

// shared drag / scroll editing of the selected row
struct EditWidget : OpaqueWidget {
	XfModule* module = nullptr;
	float dragAccum = 0.f, dragMoved = 0.f;
	Vec pressPos;
	int editParam = -1;
	float oldValue = 0.f;

	void beginEdit() {
		editParam = module ? module->selectedParam() : -1;
		if (editParam >= 0)
			oldValue = module->params[editParam].getValue();
	}
	void endEdit() {
		if (editParam < 0 || !module)
			return;
		float nv = module->params[editParam].getValue();
		if (nv != oldValue) {
			auto* h = new history::ParamChange;
			h->name = "change " + module->rowLabel(editParam);
			h->moduleId = module->id;
			h->paramId = editParam;
			h->oldValue = oldValue;
			h->newValue = nv;
			APP->history->push(h);
		}
		editParam = -1;
	}
	void nudge(int delta) {
		if (!module)
			return;
		int pid = module->selectedParam();
		if (pid < 0)
			return;
		ParamQuantity* q = module->paramQuantities[pid];
		q->setValue(clamp(std::round(q->getValue()) + delta, q->getMinValue(), q->getMaxValue()));
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS)
			pressPos = e.pos;
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			openMenu();
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
			e.consume(this);
	}
	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragAccum = 0.f;
		dragMoved = 0.f;
		beginEdit();
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragMoved += std::fabs(e.mouseDelta.x) + std::fabs(e.mouseDelta.y);
		dragAccum += -e.mouseDelta.y;
		const float pixelsPerStep = 6.f;
		int mult = (APP->window->getMods() & GLFW_MOD_SHIFT) ? 10 : 1;
		while (dragAccum >= pixelsPerStep) {
			nudge(mult);
			dragAccum -= pixelsPerStep;
		}
		while (dragAccum <= -pixelsPerStep) {
			nudge(-mult);
			dragAccum += pixelsPerStep;
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		endEdit();
		if (dragMoved < 2.f)
			click();
	}
	virtual void click() {
		if (module)
			module->nextRow();
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!module)
			return;
		int d = e.scrollDelta.y > 0.f ? 1 : e.scrollDelta.y < 0.f ? -1 : 0;
		if (d != 0) {
			beginEdit();
			nudge(d * ((APP->window->getMods() & GLFW_MOD_SHIFT) ? 10 : 1));
			endEdit();
		}
		e.consume(this);
	}
	void openMenu() {
		if (!module)
			return;
		XfModule* m = module;
		ui::Menu* menu = rack::createMenu();
		menu->addChild(createMenuLabel(m->info->title + " settings"));
		std::vector<std::string> pages;
		for (int p = 0; p < m->pageCount(); p++)
			pages.push_back(m->pageTitle(p));
		menu->addChild(createIndexSubmenuItem("Page", pages, [=]() { return (size_t)m->page; }, [=](size_t p) { m->goToPage((int)p); }));
		menu->addChild(new MenuSeparator);
		appendPageMenu(menu, m, m->page);
	}
};

// every text a row's value can show, to size the menu to the longest
static std::vector<std::string> allValues(XfModule* m, int pid) {
	if (pid >= XfModule::FIELD_PARAM && pid != m->engineParam) {
		const Field* fd = m->fieldAt(pid - XfModule::FIELD_PARAM);
		if (!fd)
			return {};
		if (!fd->names.empty())
			return fd->names;
		return {fd->text(fd->min), fd->text(fd->max)};
	}
	return m->rowChoices(pid);
}

struct Display : EditWidget {
	std::string fontPath;
	static constexpr float HEAD = 15.f, ROW_H = 13.f, FOOT = 11.f, FONT = 11.5f;

	Display() { fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf"); }

	// the menu's width, worked out once per engine
	Adapter* sizedFor = nullptr;
	std::vector<float> pageW;
	float left = 5.f;
	static float menuWidth(NVGcontext* vg, XfModule* m, const std::vector<int>& rows) {
		float labW = 0.f, valW = 0.f;
		for (int pid : rows) {
			labW = std::max(labW, nvgTextBounds(vg, 0, 0, m->rowLabel(pid).c_str(), NULL, NULL));
			for (const std::string& v : allValues(m, pid))
				valW = std::max(valW, nvgTextBounds(vg, 0, 0, v.c_str(), NULL, NULL));
		}
		return labW + nvgTextBounds(vg, 0, 0, " ", NULL, NULL) + valW;
	}

	int visibleRows() { return std::max(1, (int)((box.size.y - HEAD - FOOT - 1.f) / ROW_H)); }

	void click() override {
		if (!module)
			return;
		if (pressPos.y < HEAD) {
			// the header: left half = previous page, right half = next page
			module->goToPage(module->page + (pressPos.x < box.size.x / 2 ? -1 : 1));
			return;
		}
		int r = (int)std::floor((pressPos.y - HEAD) / ROW_H) + module->scroll;
		int n = (int)module->pageParams(module->page).size();
		if (r >= 0 && r < n)
			module->selected = r;
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, SCREEN);
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawScreen(args);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawScreen(const DrawArgs& args) {
		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		float W = box.size.x, H = box.size.y, pad = 5.f;
		nvgScissor(vg, 0, 0, W, H);
		if (!module) {
			text(vg, W / 2, H / 2 + 4, "X|FORMER", AMBER, 14.f, NVG_ALIGN_CENTER);
			nvgResetScissor(vg);
			return;
		}
		XfModule* m = module;

		// header: < page n/N >   clock
		std::string title = string::f("< %s >", m->pageTitle(m->page).c_str());
		text(vg, pad, 11.f, title, AMBER, 10.f);
		bool running = m->params[XfModule::RUN_PARAM].getValue() > 0.5f;
		std::string clk;
		if (!running)
			clk = "STOPPED";
		else if (!m->inputs[XfModule::CLOCK_INPUT].isConnected())
			clk = "NO CLOCK";
		else if (m->bpm() > 0.f)
			clk = string::f("%.0f BPM", m->bpm());
		else
			clk = "WAITING";
		text(vg, W - pad - 7.f, 11.f, clk, running ? CYAN : AMBER_DIM, 9.f, NVG_ALIGN_RIGHT);
		nvgBeginPath(vg);
		nvgCircle(vg, W - pad - 2.f, 8.f, 2.f);
		nvgFillColor(vg, m->clockLight > 0.05f ? CYAN : CYAN_DIM);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, pad, 13.5f);
		nvgLineTo(vg, W - pad, 13.5f);
		nvgStrokeColor(vg, AMBER_DIM);
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// rows, scrolled so the selected one shows
		std::vector<int> rows = m->pageParams(m->page);
		int n = (int)rows.size(), vis = visibleRows();
		m->selected = clamp(m->selected, 0, std::max(0, n - 1));
		if (m->selected < m->scroll)
			m->scroll = m->selected;
		if (m->selected >= m->scroll + vis)
			m->scroll = m->selected - vis + 1;
		m->scroll = clamp(m->scroll, 0, std::max(0, n - vis));
		// The menu is only as wide as the page needs: the longest label, one
		// character, then room for the longest value any row can show, so the
		// eye doesn't travel across empty screen. It sits in the middle.
		// The labels start at the same place on every page (the widest page of
		// this engine sits in the middle), so they don't jump when paging.
		float fs = FONT;
		nvgFontSize(vg, fs);
		nvgTextLetterSpacing(vg, 0.f);
		float badge = 16.f;   // room for a CTRL badge to the right of the values
		float maxW = W - 2 * pad - badge - 4.f;
		if (sizedFor != m->ad) {
			sizedFor = m->ad;
			pageW.clear();
			float widest = 0.f;
			for (int p = 0; p < m->pageCount(); p++) {
				pageW.push_back(menuWidth(vg, m, m->pageParams(p)));
				widest = std::max(widest, pageW.back());
			}
			left = std::max(pad, (W - std::min(widest, maxW) - badge) / 2.f);
		}
		float blockW = std::min(m->page < (int)pageW.size() ? pageW[m->page] : maxW, maxW);
		float x0 = left, x1 = x0 + blockW;
		for (int r = m->scroll; r < n && r < m->scroll + vis; r++) {
			int pid = rows[r];
			float y = HEAD + (r - m->scroll) * ROW_H;
			bool sel = r == m->selected;
			int own = m->rowOwner(pid);
			if (sel) {
				nvgBeginPath(vg);
				nvgRect(vg, x0 - 3.f, y, x1 - x0 + 6.f, ROW_H);
				nvgFillColor(vg, AMBER);
				nvgFill(vg);
			}
			text(vg, x0, y + ROW_H * 0.78f, m->rowLabel(pid), sel ? SCREEN : AMBER, fs);
			text(vg, x1, y + ROW_H * 0.78f, m->rowValue(pid, true), sel ? SCREEN : (own ? CYAN : AMBER), fs, NVG_ALIGN_RIGHT);
			if (own) {
				// "C1".."C4": a CTRL knob drives this row
				float bx = x1 + 5.f;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, bx, y + 1.5f, 13.f, ROW_H - 3.f, 2.f);
				nvgFillColor(vg, CYAN);
				nvgFill(vg);
				text(vg, bx + 6.5f, y + ROW_H * 0.74f, string::f("C%d", own), SCREEN, 9.f, NVG_ALIGN_CENTER);
			}
		}
		if (n > vis) {
			// scroll bar
			float top = HEAD, h = vis * ROW_H;
			nvgBeginPath(vg);
			nvgRect(vg, W - 2.f, top + h * m->scroll / n, 1.5f, h * vis / n);
			nvgFillColor(vg, AMBER_DIM);
			nvgFill(vg);
		}
		if (m->isCtrlPage(m->page)) {
			int k = m->ctrlOfPage(m->page);
			int d = m->ctrlDest(k);
			const Field* fd = m->fieldAt(d);
			std::string help = !fd ? "Pick a setting for this knob with DATA." :
				(m->ctrlDirect(k) ? "Direct: the knob sweeps the whole range." : "Offset: noon leaves the menu value as set.");
			text(vg, pad, HEAD + 3 * ROW_H, help, AMBER_DIM, 8.f);
			if (fd)
				text(vg, pad, HEAD + 4 * ROW_H, "Now: " + fd->text(m->eff[d]), CYAN, 9.f);
		}

		// footer: engine status, note playing
		float fy = H - 3.5f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, pad, H - FOOT);
		nvgLineTo(vg, W - pad, H - FOOT);
		nvgStrokeColor(vg, AMBER_DIM);
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);
		text(vg, pad, fy, m->ad->status(), AMBER, 8.5f);
		std::string note = m->outputs[XfModule::GATE_OUTPUT].getVoltage() > 1.f ? noteName(m->pitchOut) : "";
		text(vg, W - pad, fy, note, CYAN, 8.5f, NVG_ALIGN_RIGHT);
		nvgResetScissor(vg);
	}
};

// The DATA encoder: drag up/down to change the selected row, click for the next row.
struct Encoder : EditWidget {
	float angle = 0.f;
	void onDragMove(const DragMoveEvent& e) override {
		angle += -e.mouseDelta.y * 0.04f;
		EditWidget::onDragMove(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		angle += e.scrollDelta.y > 0.f ? 0.3f : -0.3f;
		EditWidget::onHoverScroll(e);
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float r = box.size.x / 2;
		Vec c(r, r);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillColor(vg, nvgRGB(0x14, 0x15, 0x18));
		nvgFill(vg);
		for (int i = 0; i < 24; i++) {
			float t = angle + i * (2 * M_PI / 24);
			nvgBeginPath(vg);
			nvgMoveTo(vg, c.x + std::cos(t) * r * 0.84f, c.y + std::sin(t) * r * 0.84f);
			nvgLineTo(vg, c.x + std::cos(t) * r * 0.98f, c.y + std::sin(t) * r * 0.98f);
			nvgStrokeColor(vg, nvgRGB(0x3a, 0x3d, 0x44));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r * 0.72f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, 2 * r, nvgRGB(0x4a, 0x4e, 0x56), nvgRGB(0x22, 0x24, 0x28)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, c.x + std::cos(angle - M_PI / 2) * r * 0.48f, c.y + std::sin(angle - M_PI / 2) * r * 0.48f, r * 0.08f);
		nvgFillColor(vg, AMBER);
		nvgFill(vg);
		OpaqueWidget::draw(args);
	}
};

// ---------------------------------------------------------------- the step grid

// undo for a grid edit
struct GridAction : history::ModuleAction {
	int lane = 0;
	std::vector<int> idx, before, after;
	void apply(const std::vector<int>& v) {
		XfModule* m = dynamic_cast<XfModule*>(APP->engine->getModule(moduleId));
		if (!m)
			return;
		for (size_t i = 0; i < idx.size(); i++)
			m->queueEdit(lane, idx[i], v[i]);
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};

struct Grid : OpaqueWidget {
	XfModule* module = nullptr;
	std::string fontPath;
	static constexpr float HEAD = 12.f;
	static const int COLS = 32;
	// a drag in progress
	bool painting = false;
	int paintValue = 0;
	std::map<int, int> before, after;
	int hoverStep = -1;
	Vec pressPos;

	Grid() { fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf"); }

	Adapter* ad() { return module ? module->ad : nullptr; }
	int laneIdx() { return module ? clamp(module->gridLane, 0, std::max(0, (int)ad()->lanes.size() - 1)) : 0; }
	int count() { return ad() ? std::max(1, ad()->count(laneIdx())) : 32; }
	int cols() { return std::min(COLS, count()); }
	int pages() { return (count() + COLS - 1) / COLS; }
	int pageIdx() { return module ? clamp(module->gridPage, 0, pages() - 1) : 0; }
	float colW() { return (box.size.x - 4.f) / cols(); }
	int stepAt(Vec p) {
		int c = (int)std::floor((p.x - 2.f) / colW());
		if (c < 0 || c >= cols())
			return -1;
		int i = pageIdx() * COLS + c;
		return i < count() ? i : -1;
	}
	int valueAt(Vec p) {
		const Lane& l = ad()->lanes[laneIdx()];
		float top = HEAD + 2.f, h = box.size.y - top - 2.f;
		float t = clamp(1.f - (p.y - top) / h, 0.f, 1.f);
		return l.min + (int)std::round(t * (l.max - l.min));
	}
	Vec mouseLocal() {
		return APP->scene->getMousePos().minus(getAbsoluteOffset(Vec())).div(getAbsoluteZoom());
	}

	void paintAt(Vec p) {
		int i = stepAt(p);
		if (i < 0)
			return;
		const Lane& l = ad()->lanes[laneIdx()];
		int v = (l.max - l.min == 1) ? paintValue : valueAt(p);
		if (!before.count(i))
			before[i] = ad()->get(laneIdx(), i);
		after[i] = v;
		module->queueEdit(laneIdx(), i, v);
	}
	void finishPaint() {
		if (!painting)
			return;
		painting = false;
		if (after.empty())
			return;
		auto* h = new GridAction;
		h->name = "edit " + ad()->lanes[laneIdx()].label;
		h->moduleId = module->id;
		h->lane = laneIdx();
		for (auto& kv : after) {
			h->idx.push_back(kv.first);
			h->before.push_back(before[kv.first]);
			h->after.push_back(kv.second);
		}
		APP->history->push(h);
		before.clear();
		after.clear();
	}

	void changeLane(int d) {
		int n = (int)ad()->lanes.size();
		module->gridLane = ((laneIdx() + d) % n + n) % n;
		module->gridPage = clamp(module->gridPage, 0, pages() - 1);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module) {
			OpaqueWidget::onButton(e);
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			openMenu();
			e.consume(this);
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			pressPos = e.pos;
			e.consume(this);
			if (e.pos.y < HEAD) {
				float W = box.size.x;
				if (e.pos.x < W * 0.55f)
					changeLane(e.pos.x < W * 0.275f ? -1 : 1);
				else if (pages() > 1)
					module->gridPage = (pageIdx() + (e.pos.x < W * 0.8f ? pages() - 1 : 1)) % pages();
				return;
			}
			const Lane& l = ad()->lanes[laneIdx()];
			if (l.readOnly)
				return;
			int i = stepAt(e.pos);
			if (i < 0)
				return;
			painting = true;
			before.clear();
			after.clear();
			if (l.max - l.min == 1)
				paintValue = ad()->get(laneIdx(), i) == l.min ? l.max : l.min;
			paintAt(e.pos);
		}
		if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT)
			finishPaint();
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (painting && e.button == GLFW_MOUSE_BUTTON_LEFT)
			paintAt(mouseLocal());
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT)
			finishPaint();
	}
	void onHover(const HoverEvent& e) override {
		hoverStep = e.pos.y >= HEAD ? stepAt(e.pos) : -1;
		OpaqueWidget::onHover(e);
	}
	void onLeave(const LeaveEvent& e) override {
		hoverStep = -1;
		OpaqueWidget::onLeave(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!module)
			return;
		int d = e.scrollDelta.y > 0.f ? 1 : e.scrollDelta.y < 0.f ? -1 : 0;
		if (d == 0)
			return;
		e.consume(this);
		if (e.pos.y < HEAD) {
			changeLane(-d);
			return;
		}
		const Lane& l = ad()->lanes[laneIdx()];
		int i = stepAt(e.pos);
		if (l.readOnly || i < 0)
			return;
		int mult = (APP->window->getMods() & GLFW_MOD_SHIFT) ? 10 : 1;
		int old = ad()->get(laneIdx(), i);
		int v = clamp(old + d * mult, l.min, l.max);
		module->queueEdit(laneIdx(), i, v);
		auto* h = new GridAction;
		h->name = "edit " + l.label;
		h->moduleId = module->id;
		h->lane = laneIdx();
		h->idx = {i};
		h->before = {old};
		h->after = {v};
		APP->history->push(h);
	}

	void openMenu() {
		XfModule* m = module;
		ui::Menu* menu = rack::createMenu();
		menu->addChild(createMenuLabel("Step grid"));
		std::vector<std::string> names;
		for (const Lane& l : m->ad->lanes)
			names.push_back(l.label);
		menu->addChild(createIndexSubmenuItem("Show", names, [=]() { return (size_t)m->gridLane; },
			[=](size_t i) { m->gridLane = (int)i; m->gridPage = 0; }));
		int li = laneIdx();
		const Lane& l = m->ad->lanes[li];
		if (!l.readOnly) {
			int n = count();
			int first = 0, last = n - 1;
			m->ad->range(li, first, last);
			menu->addChild(createMenuItem("Randomise " + l.label + " (playing steps)", "", [=]() {
				auto* h = new GridAction;
				h->name = "randomise " + l.label;
				h->moduleId = m->id;
				h->lane = li;
				for (int i = std::max(0, first); i <= std::min(n - 1, last); i++) {
					int v = l.min + (int)std::floor(random::uniform() * (l.max - l.min + 1));
					h->idx.push_back(i);
					h->before.push_back(m->ad->get(li, i));
					h->after.push_back(std::min(v, l.max));
				}
				h->redo();
				APP->history->push(h);
			}));
			menu->addChild(createMenuItem("Set all " + l.label + " to the first step's value", "", [=]() {
				auto* h = new GridAction;
				h->name = "fill " + l.label;
				h->moduleId = m->id;
				h->lane = li;
				int v = m->ad->get(li, std::max(0, first));
				for (int i = 0; i < n; i++) {
					h->idx.push_back(i);
					h->before.push_back(m->ad->get(li, i));
					h->after.push_back(v);
				}
				h->redo();
				APP->history->push(h);
			}));
		}
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, SCREEN);
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawGrid(args);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawGrid(const DrawArgs& args) {
		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		float W = box.size.x, H = box.size.y;
		nvgScissor(vg, 0, 0, W, H);
		if (!module) {
			// browser preview: a few bars
			for (int c = 0; c < 16; c++) {
				float x = 2.f + c * (W - 4.f) / 16, h = (H - HEAD - 6.f) * (0.2f + 0.6f * std::fabs(std::sin(c * 1.7f)));
				nvgBeginPath(vg);
				nvgRect(vg, x + 1.f, H - 3.f - h, (W - 4.f) / 16 - 2.f, h);
				nvgFillColor(vg, AMBER);
				nvgFill(vg);
			}
			nvgResetScissor(vg);
			return;
		}
		Adapter* a = ad();
		int li = laneIdx();
		const Lane& l = a->lanes[li];
		int n = count(), nc = cols(), pg = pageIdx();
		float cw = colW();
		int first = 0, last = n - 1;
		a->range(li, first, last);
		int play = a->playPos(li);

		// header: < lane >   hover info   < steps >
		text(vg, 4.f, 9.f, "< " + l.label + " >", AMBER, 9.f);
		if (hoverStep >= 0 && hoverStep < n)
			text(vg, W * 0.62f, 9.f, string::f("%d: %s", hoverStep + 1, l.text(a->get(li, hoverStep)).c_str()), CYAN, 9.f, NVG_ALIGN_CENTER);
		if (pages() > 1)
			text(vg, W - 4.f, 9.f, string::f("< %d-%d of %d >", pg * COLS + 1, std::min(n, (pg + 1) * COLS), n), AMBER, 9.f, NVG_ALIGN_RIGHT);
		else if (l.readOnly)
			text(vg, W - 4.f, 9.f, "view only", AMBER_DIM, 8.f, NVG_ALIGN_RIGHT);

		float top = HEAD + 2.f, h = H - top - 2.f;
		float span = (float)std::max(1, l.max - l.min);
		bool bip = l.min < 0 && l.max > 0;
		float zeroY = bip ? top + h * (l.max / span) : top + h;
		bool toggle = l.max - l.min == 1;
		for (int c = 0; c < nc; c++) {
			int i = pg * COLS + c;
			if (i >= n)
				break;
			float x = 2.f + c * cw;
			bool inside = i >= first && i <= last;
			if (!inside || c % 4 == 0) {
				nvgBeginPath(vg);
				nvgRect(vg, x, top, cw, h);
				nvgFillColor(vg, inside ? nvgRGB(0x12, 0x13, 0x16) : SHADE);
				nvgFill(vg);
			}
			if (i == play) {
				nvgBeginPath(vg);
				nvgRect(vg, x, top, cw, h);
				nvgFillColor(vg, nvgRGBA(0x5f, 0xd0, 0xe4, 0x30));
				nvgFill(vg);
			}
			int v = a->get(li, i);
			bool on = a->stepOn(i);
			NVGcolor col = i == play ? CYAN : (inside ? AMBER : AMBER_DIM);
			if (!on && !toggle)
				col = inside ? nvgRGB(0x8a, 0x62, 0x2a) : nvgRGB(0x3a, 0x2a, 0x14);
			float bx = x + std::max(0.5f, cw * 0.12f), bw = std::max(1.f, cw - 2 * std::max(0.5f, cw * 0.12f));
			if (toggle) {
				float s = std::min(bw, h * 0.5f);
				float by = top + h / 2 - s / 2;
				nvgBeginPath(vg);
				nvgRect(vg, bx + (bw - s) / 2, by, s, s);
				if (v == l.max) {
					nvgFillColor(vg, col);
					nvgFill(vg);
				}
				else {
					nvgStrokeColor(vg, AMBER_DIM);
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
				}
				continue;
			}
			float vy = top + h * (1.f - (v - l.min) / span);
			float y0 = std::min(vy, zeroY), y1 = std::max(vy, zeroY);
			if (y1 - y0 < 1.5f) {
				y0 = vy - 0.75f;
				y1 = vy + 0.75f;
			}
			nvgBeginPath(vg);
			nvgRect(vg, bx, y0, bw, y1 - y0);
			nvgFillColor(vg, col);
			nvgFill(vg);
			// named values: the name under the bar top when there is room
			if (!l.names.empty() && cw >= 20.f)
				text(vg, x + cw / 2, std::min(top + h - 2.f, vy + 9.f), l.text(v), i == play ? SCREEN : SCREEN, 7.f, NVG_ALIGN_CENTER);
		}
		if (bip) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, 2.f, zeroY);
			nvgLineTo(vg, W - 2.f, zeroY);
			nvgStrokeColor(vg, AMBER_DIM);
			nvgStrokeWidth(vg, 0.5f);
			nvgStroke(vg);
		}
		nvgResetScissor(vg);
	}
};

// ---------------------------------------------------------------- knobs and panel

struct CtrlKnob : RoundBlackKnob {
	int k = 0;
	void appendContextMenu(ui::Menu* menu) override {
		XfModule* m = dynamic_cast<XfModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		appendCtrlMenu(menu, m, k);
	}
};

// Multi: the names of the aux jacks, the action button and the engine, drawn
// from the engine playing (the panel leaves these spaces blank)
struct LiveLabels : TransparentWidget {
	XfModule* module = nullptr;
	Info fallback;
	std::string boldPath, plainPath;
	LiveLabels() {
		boldPath = asset::system("res/fonts/Nunito-Bold.ttf");
		plainPath = asset::system("res/fonts/DejaVuSans.ttf");
	}
	// size in mm, (x, y) the baseline point in mm; shrinks to fit maxW when given
	void label(NVGcontext* vg, int face, const std::string& s, float x, float y, float size, NVGcolor c,
		int align = NVG_ALIGN_CENTER, float maxW = 0.f) {
		if (s.empty())
			return;
		nvgFontFaceId(vg, face);
		nvgFontSize(vg, mm2px(size));
		nvgTextLetterSpacing(vg, 0.f);
		if (maxW > 0.f) {
			float w = nvgTextBounds(vg, 0, 0, s.c_str(), NULL, NULL);
			if (w > mm2px(maxW)) {
				nvgFontSize(vg, mm2px(size) * mm2px(maxW) / w);
			}
		}
		nvgFillColor(vg, c);
		nvgTextAlign(vg, align | NVG_ALIGN_BASELINE);
		nvgText(vg, mm2px(x), mm2px(y), s.c_str(), NULL);
	}
	void draw(const DrawArgs& args) override {
		using namespace layout;
		std::shared_ptr<window::Font> bold = APP->window->loadFont(boldPath);
		std::shared_ptr<window::Font> plain = APP->window->loadFont(plainPath);
		if (!bold || bold->handle < 0 || !plain || plain->handle < 0)
			return;
		const Info& in = module ? *module->info : fallback;
		NVGcontext* vg = args.vg;
		const NVGcolor amber = nvgRGB(0xC2, 0x73, 0x0F), ink = nvgRGB(0x1E, 0x21, 0x26), soft = nvgRGB(0x6B, 0x73, 0x7C);
		const NVGcolor outText = nvgRGB(0xF2, 0xA9, 0x3B), outSoft = nvgRGB(0x9A, 0xA3, 0xAD);
		// title strip: the engine and what it does
		label(vg, bold->handle, in.title, 46.f, 9.f, 3.4f, ink, NVG_ALIGN_LEFT);
		nvgFontFaceId(vg, bold->handle);
		nvgFontSize(vg, mm2px(3.4f));
		nvgTextLetterSpacing(vg, 0.f);
		float tw = nvgTextBounds(vg, 0, 0, in.title.c_str(), NULL, NULL) / mm2px(1.f);
		float bx = 46.f + tw + 2.5f;
		label(vg, plain->handle, in.blurb, bx, 8.8f, 2.3f, soft, NVG_ALIGN_LEFT, std::max(10.f, PANEL_W - 17.f - bx));
		// aux inputs, aux output, action button
		for (int j = 0; j < 2; j++) {
			if (in.auxIn[j].empty()) {
				label(vg, bold->handle, "-", IN_X[4 + j], 114.9f, 2.9f, soft);
				continue;
			}
			label(vg, bold->handle, in.auxIn[j], IN_X[4 + j], 114.9f, 2.9f, amber, NVG_ALIGN_CENTER, 9.6f);
			label(vg, plain->handle, in.auxInNote[j], IN_X[4 + j], 126.6f, 2.1f, soft, NVG_ALIGN_CENTER, 9.6f);
		}
		label(vg, bold->handle, in.auxOut.empty() ? "-" : in.auxOut, OUT_X[5], 114.9f, 2.9f, outText, NVG_ALIGN_CENTER, 9.6f);
		label(vg, plain->handle, in.auxOutNote, OUT_X[5], 126.6f, 2.1f, outSoft, NVG_ALIGN_CENTER, 9.6f);
		label(vg, bold->handle, in.actionLabel, SIDE_X, ACT_Y - 4.6f, 3.2f, amber, NVG_ALIGN_CENTER, 11.f);
	}
};

XfWidget::XfWidget(XfModule* module, const Info& info) {
	using namespace layout;
	setModule(module);
	setPanel(createPanel(asset::plugin(pluginInstance, "res/" + info.slug + ".svg")));
	if (info.multi) {
		LiveLabels* ll = createWidget<LiveLabels>(Vec(0, 0));
		ll->box.size = box.size;
		ll->module = module;
		ll->fallback = info;
		addChild(ll);
	}
	addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	Display* disp = createWidget<Display>(mm2px(Vec(DISP_X, DISP_Y)));
	disp->box.size = mm2px(Vec(DISP_W, DISP_H));
	disp->module = module;
	addChild(disp);

	Encoder* enc = createWidget<Encoder>(mm2px(Vec(ENC_X - ENC_SIZE / 2, ENC_Y - ENC_SIZE / 2)));
	enc->box.size = mm2px(Vec(ENC_SIZE, ENC_SIZE));
	enc->module = module;
	addChild(enc);

	addParam(createParamCentered<VCVButton>(mm2px(Vec(PAGE_PREV_X, PAGE_Y)), module, XfModule::PAGE_PREV_PARAM));
	addParam(createParamCentered<VCVButton>(mm2px(Vec(PAGE_NEXT_X, PAGE_Y)), module, XfModule::PAGE_NEXT_PARAM));
	addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(mm2px(Vec(SIDE_X, RUN_Y)), module,
		XfModule::RUN_PARAM, XfModule::RUN_LIGHT));
	addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(SIDE_X, RESET_Y)), module,
		XfModule::RESET_PARAM, XfModule::RESET_LIGHT));
	if (!info.action.empty() || info.multi)
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<YellowLight>>>(mm2px(Vec(SIDE_X, ACT_Y)), module,
			XfModule::ACTION_PARAM, XfModule::ACTION_LIGHT));

	Grid* grid = createWidget<Grid>(mm2px(Vec(GRID_X, GRID_Y)));
	grid->box.size = mm2px(Vec(GRID_W, GRID_H));
	grid->module = module;
	addChild(grid);

	for (int k = 0; k < NUM_CTRLS; k++) {
		float bx = CTRL_X0 + CTRL_DX * k;
		CtrlKnob* knob = createParamCentered<CtrlKnob>(mm2px(Vec(bx + CTRL_KNOB_DX, CTRL_Y)), module, XfModule::CTRL_PARAM + k);
		knob->k = k;
		addParam(knob);
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(bx + CTRL_BTN_DX, CTRL_BTN_Y)), module,
			XfModule::CTRL_MENU_PARAM + k, XfModule::CTRL_MENU_LIGHT + k));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(bx + CTRL_BTN_DX, CTRL_ATT_Y)), module, XfModule::CTRL_ATT_PARAM + k));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(bx + CTRL_JACK_DX, CTRL_Y)), module, XfModule::CTRL_CV_INPUT + k));
	}

	int ins[4] = {XfModule::CLOCK_INPUT, XfModule::RESET_INPUT, XfModule::RUN_INPUT, XfModule::TRANSPOSE_INPUT};
	for (int j = 0; j < 4; j++)
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X[j], JACK_Y)), module, ins[j]));
	for (int j = 0; j < 2; j++)
		if (!info.auxIn[j].empty() || info.multi)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X[4 + j], JACK_Y)), module, XfModule::AUX_INPUT + j));
	for (int j = 0; j < 6; j++) {
		if (j == XfModule::AUX_OUTPUT && info.auxOut.empty() && !info.multi)
			continue;
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(OUT_X[j], JACK_Y)), module, j));
	}
}

void XfWidget::appendContextMenu(Menu* menu) {
	XfModule* m = dynamic_cast<XfModule*>(module);
	if (!m)
		return;
	menu->addChild(new MenuSeparator);
	if (m->multi())
		appendParamItem(menu, m, m->engineParam);
	menu->addChild(createMenuLabel(m->info->title + " settings"));
	for (int p = 0; p < (int)m->ad->pages.size(); p++) {
		int page = m->pageBase() + p;
		menu->addChild(createSubmenuItem(m->ad->pages[p].title, "", [=](Menu* sub) { appendPageMenu(sub, m, page); }));
	}
	for (int k = 0; k < NUM_CTRLS; k++)
		appendCtrlMenu(menu, m, k);
	menu->addChild(new MenuSeparator);
	menu->addChild(createMenuLabel("Pattern"));
	std::vector<std::string> acts = m->ad->actions();
	for (int i = 0; i < (int)acts.size(); i++)
		menu->addChild(createMenuItem(acts[i], "", [=]() { m->queueEdit(XfModule::EDIT_ACTION, i, 0); }));
	menu->addChild(createMenuItem("Load the starter pattern", "", [=]() { m->queueEdit(XfModule::EDIT_INIT, 0, 0); }));
	menu->addChild(createMenuItem("Clear the pattern", "", [=]() { m->queueEdit(XfModule::EDIT_CLEAR, 0, 0); }));
}

}  // namespace xfm
