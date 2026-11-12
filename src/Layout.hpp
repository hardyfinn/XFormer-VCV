// Panel positions in mm, shared by the widget and res/make_panels.py
// (keep the two in step).
#pragma once

namespace xfm {
namespace layout {

static const float PANEL_W = 132.08f;  // 26 HP
static const float DISP_X = 6.f, DISP_Y = 12.f, DISP_W = 71.7f, DISP_H = 46.f;
static const float ENC_X = 93.68f, ENC_Y = 26.f, ENC_SIZE = 16.f;
static const float PAGE_PREV_X = 86.18f, PAGE_NEXT_X = 101.18f, PAGE_Y = 45.f;
static const float SIDE_X = 120.18f, RUN_Y = 19.f, RESET_Y = 32.f, ACT_Y = 45.f;
static const float GRID_X = 6.f, GRID_Y = 62.5f, GRID_W = 120.08f, GRID_H = 25.5f;
static const float CTRL_X0 = 6.f, CTRL_DX = 30.02f;
static const float CTRL_KNOB_DX = 6.84f, CTRL_BTN_DX = 15.82f, CTRL_JACK_DX = 24.38f;
static const float CTRL_Y = 101.5f, CTRL_BTN_Y = 97.3f, CTRL_ATT_Y = 105.6f;
static const float JACK_Y = 120.f;
static const float IN_X[6] = {9.5f, 19.8f, 30.1f, 40.4f, 50.7f, 61.f};
static const float OUT_X[6] = {73.f, 83.f, 93.f, 103.f, 113.f, 123.f};

}  // namespace layout
}  // namespace xfm
