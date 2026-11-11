// Panel positions in mm, shared by the widget and res/make_panels.py
// (keep the two in step).
#pragma once

namespace xfm {
namespace layout {

static const float PANEL_W = 152.4f;  // 30 HP
static const float DISP_X = 6.f, DISP_Y = 12.f, DISP_W = 92.f, DISP_H = 40.f;
static const float ENC_X = 114.f, ENC_Y = 26.f, ENC_SIZE = 16.f;
static const float PAGE_PREV_X = 106.5f, PAGE_NEXT_X = 121.5f, PAGE_Y = 45.f;
static const float SIDE_X = 140.5f, RUN_Y = 19.f, RESET_Y = 32.f, ACT_Y = 45.f;
static const float GRID_X = 6.f, GRID_Y = 55.5f, GRID_W = 140.4f, GRID_H = 33.5f;
static const float CTRL_X0 = 6.f, CTRL_DX = 35.1f;
static const float CTRL_KNOB_DX = 8.f, CTRL_BTN_DX = 18.5f, CTRL_JACK_DX = 28.5f;
static const float CTRL_Y = 101.5f, CTRL_BTN_Y = 97.3f, CTRL_ATT_Y = 105.6f;
static const float JACK_Y = 120.f;
static const float IN_X[6] = {10.f, 22.f, 34.f, 46.f, 58.f, 70.f};
static const float OUT_X[6] = {84.f, 95.f, 106.f, 117.f, 128.f, 139.f};

}  // namespace layout
}  // namespace xfm
