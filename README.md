# X|FORMER for VCV Rack

**Multi**, one sequencer module for VCV Rack 2 that holds the seven track
engines of the X|FORMER Eurorack firmware (itself based on PER|FORMER by
Simon Kallweit). Each engine is a line-for-line C++ port, checked against
traces from the firmware simulator, so it plays exactly what the hardware
track would.

The first display page, ENGINE, picks the engine (also in the right-click
menu). The names by the two aux inputs, the aux output and the action button,
and the engine name in the title strip, change with the engine. Each engine
keeps its own settings, CTRL assignments and pattern while another one plays,
and all seven are saved with the patch, so switching back picks up where you
left it. Switching restarts the sequence on the next clock pulse.

| Engine | What it does | Aux in 1 / 2 | Aux out | Button |
|---|---|---|---|---|
| **Algo** | 16 generative melody algorithms (the Tuesday track) | RESEED trig | ACC (accent gate) | Reseed |
| **Stochastic** | Rhythm and melody rolled from weighted dice | NEW R / NEW M trig | SLIDE gate | New rhythm + melody |
| **Note** | 64-step sequencer with probabilities, conditions, ratchets, accumulator | FILL gate | SLIDE gate | Fill (hold) |
| **Indexed** | Steps with their own durations, reshaped by two mod inputs | MOD A / MOD B (±5 V) | SLIDE gate | none |
| **Discrete** | A sweep crossing 32 note thresholds | INPUT (±5 V) / SCAN (0-10 V) | SWEEP (±5 V) | none |
| **PhaseFlux** | 16 stages on a 4×4 grid, each a burst of shaped pulses | none | STAGE trig | none |
| **Fractal** | Records a phrase from gate + CV, loops it and grows branches | REC G gate / REC CV 1 V/oct | ORN trig | Record on/off |

## Installing

Download `XFormer-2.x.x-mac-arm64.vcvplugin` from the
[latest release](https://github.com/hardyfinn/xformer-vcv/releases/tag/latest)
and drop it into your Rack 2 plugins folder
(`~/Library/Application Support/Rack2/plugins-mac-arm64/`), then restart Rack.

## The panel

* **Display + DATA encoder.** The display shows one settings page at a time.
  Turn (drag) the encoder to change the selected row, click it to move to the
  next row. Click the left or right half of the display header, or the
  `< PAGE >` buttons, to change page. You can also scroll the mouse wheel over
  the display (shift for ×10).
* **Grid.** Shows the pattern; click or drag to paint values, scroll to nudge
  one step. Click the grid header to switch lane (Note, Gate, Probability...)
  or 32-step page. Right-click for lane choice, randomise and fill.
* **CTRL 1-4.** Four assignable knobs, as on Metroplex. The small lit button
  next to each knob opens that CTRL's page in the display, where the DATA
  encoder picks the destination (any setting of the module) and the mode:
  Offset (noon leaves the setting as set, the knob moves it either way) or
  Direct (the knob is the setting). The same choices are in the knob's
  right-click menu. The trimpot is the CV amount (noon off, left inverted);
  ±5 V on the CV jack sweeps the full range. A CTRL'd row shows a `C1`-`C4`
  badge in the display.
* **RUN / RESET / action button.** RUN toggles playback, RESET returns to the
  start, the amber button does the engine's action (see table).

### Clock

There is no internal clock: patch a clock into **CLOCK**. Set the incoming
resolution on the CLOCK page (`PPQN`: 1, 2, 4 = 16ths (default), 16, 24 or 48).
The module measures the time between pulses and divides it into the
firmware's 192 ticks per quarter, so swing, ratchets and odd divisions land
where the hardware puts them. Higher PPQN mostly helps when the clock tempo
changes, since the module adjusts on every pulse; for a steady clock 4 PPQN is
enough. **RESET** and **RUN** (a trigger toggles run, like the button) inputs are next to it. **V/OCT** transposes the pitch output.

### Outputs

GATE, PITCH (1 V/oct, with slide where the engine slides), VEL (0-10 V),
TRIG (1 ms per note), EOC (end of cycle/pattern) and the engine's aux output.

## Building

```
make RACK_DIR=<Rack-SDK>        # or: make dist
test/golden.sh                  # checks every engine against firmware traces
python3 res/make_panels.py      # redraws the panel (needs fonttools + Inter)
```

GitHub Actions builds the mac-arm64 plugin on every push to `main` and puts
it on the `latest` release.

## License

GPL-3.0-or-later. The engines are derived from PER|FORMER (MIT, © 2017-2018
Simon Kallweit); see LICENSE.txt.
