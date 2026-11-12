// Help for every setting and grid lane: shown on the display (one line in the
// footer, the full text while DATA is held), in the tooltips and in the
// right-click menu. Keyed by the field key; an engine's own entry wins over the
// shared one.
#include "Xf.hpp"
#include <map>

namespace xfm {

typedef std::map<std::string, std::string> HelpMap;

static const HelpMap& commonHelp() {
	static const HelpMap m = {
		{"_ppqn", "How many pulses the CLOCK input gets per quarter note. 4 PPQN = 16th notes (the usual sequencer clock). 24 and 48 suit MIDI-style clocks and follow tempo changes more closely."},
		{"divisor", "The length of one step. Faster rates play the same pattern in less time."},
		{"clockMult", "Speeds the engine's clock up or slows it down against the CLOCK input (100% = in time). Small offsets make patterns drift against other sequencers."},
		{"_resetBars", "Restarts the pattern every N bars of the clock (Off = never)."},
		{"resetMeasure", "Sends the pattern back to its first step every N bars (Off = never). Dice and accumulators keep their state; only the position jumps back."},
		{"resetBars", "Sends the pattern back to its start every N bars (Off = never)."},
		{"scale", "The scale the notes are picked from. Every pitch the engine makes is a scale step, so changing the scale re-colours the same pattern. Includes microtonal scales (up to 24-tet)."},
		{"rootNote", "The key: moves the whole scale, and so every note, up to this root."},
		{"scaleRotate", "Plays the scale from another degree, which gives its modes (Major with Mode 1 = Dorian, 2 = Phrygian and so on)."},
		{"octave", "Shifts every note by whole octaves."},
		{"transpose", "Shifts every note by scale steps (in Semitones, by semitones). The notes stay in the scale."},
		{"_vel", "The level of the VEL output for every note (100% = 10 V)."},
		{"_slide", "How long PITCH takes to glide to a sliding note, in milliseconds. 0 jumps straight there."},
		{"runMode", "The order the steps play in. Forward and Backward loop; Pendulum goes back and forth playing the end steps twice, PingPong without repeating them; Random jumps anywhere; Rnd Walk moves one step left or right."},
		{"rotate", "Shifts where playback starts, without changing the steps."},
		{"gateLength", "Note length. Low values make short, staccato notes."},
	};
	return m;
}

static const HelpMap& engineHelp(const std::string& slug) {
	static const std::map<std::string, HelpMap> all = {
		{"Algo", {
			{"algorithm", "Which of the 16 generators makes the notes. Each has its own style: Stomper for acid bass, Markov for wandering melody, Aphex for broken lines, Turing for a Turing Machine..."},
			{"flow", "The first seed: picks the structure of the pattern (which notes, which rhythm). Each value is a different, repeatable pattern."},
			{"ornament", "The second seed: picks the detail (accents, trills, octave jumps) on top of the Flow pattern."},
			{"power", "Density. 0 is silent; higher values let more steps through. Accented steps always play."},
			{"gateLength", "Note length. 50% plays the algorithm's own lengths, 100% doubles them, low values make short staccato notes."},
			{"glide", "The chance a note slides from the previous one (PITCH glides at Slide time). Some algorithms only slide above 50%."},
			{"trill", "The chance a step the algorithm marks gets a ratchet: the note fires two extra times inside the step, a step higher."},
			{"loopLength", "Inf keeps generating new material. A number restarts the seeds every N steps, so the pattern repeats as an N-step loop."},
			{"_resetBars", "Restarts the pattern from scratch every N bars, seeds included (Off = never)."},
			{"start", "Waits this many steps after a reset before the first note."},
			{"rotate", "Shifts where in the loop the pattern starts, without changing it. Needs a finite Loop."},
			{"skew", "Bends Power across the loop. Positive values start sparse and end dense; negative values the other way round. Needs a finite Loop."},
			{"transpose", "Shifts every note by scale steps (in Semitones, by semitones). The notes stay in the scale."},
			{"maskParameter", "Hides the clock from the algorithm in on/off windows a prime number of ticks long, which carves rests and polyrhythms. All = no mask, None = silence."},
			{"timeMode", "Free: the on and off windows are both the prime length. Qrt / 1.5 Qrt / 3 Qrt: the cycle repeats every 1, 1.5 or 3 beats, and its first prime-length part is silent."},
			{"maskProgression", "After each mask window, moves to another prime in the list (+1, +5 or +7 places), so the gaps keep changing. None keeps one prime."},
			{"gateOffset", "Timing feel. 0% plays straight on the grid, 50% uses the algorithm's own pushes and drags, 100% doubles them."},
			{"stepTrill", "The chance a step the algorithm wants to trill plays as a fast group of notes inside the step."},
			{"_velSrc", "Algorithm: VEL follows the algorithm (accents are louder). Fixed: every note uses the Velocity setting."},
			{"_vel", "The VEL level for every note when Vel source = Fixed (100% = 10 V)."},
		}},
		{"Stochastic", {
			{"complexity", "How often a repeated pitch is rolled again. High values avoid playing the same note twice in a row; low values allow repeats."},
			{"contour", "Pushes the melody's direction. Positive values make it more likely to rise, negative to fall, 0 has no preference."},
			{"range", "50 keeps notes in one octave. Above 50 notes spread over up to 4 octaves. Below 50 the line can jump an octave from the last note, more often towards 0."},
			{"variation", "How much note lengths vary around Note length. 0 always uses it, around 50 mixes the favourite lengths, 100 explores far-away lengths."},
			{"rest", "The chance a step is silent."},
			{"gateLength", "Spread of note lengths inside each step. 0 makes every note half its step; higher values randomise between short and long."},
			{"burst", "The chance a step turns into a burst: a quick group of notes inside the step. Above 50% the Burst mode is also chosen at random per step."},
			{"slide", "The chance a note slides into the next one (PITCH glides at Slide time; the SLIDE output goes high)."},
			{"rhythmMode", "Live rolls a new rhythm for every note. Loop repeats a stored rhythm of Steps length. NEW R (or the button) captures the last bars into the loop."},
			{"melodyMode", "Live rolls a new pitch for every note. Loop repeats a stored melody. NEW M (or the button) captures it."},
			{"noteDuration", "The home note length, as a multiple of Rate (8x to 1/2x). Variation spreads lengths around it."},
			{"evolve", "Off: every reset starts from the stored loop, so arrangements repeat. On: Mutate, Patience and Jump carry on from where they were."},
			{"patienceRhythm", "How long a looped rhythm survives before it is replaced. Low values replace it after a pass or two; 99 lasts about a hundred passes; 100 never."},
			{"patienceMelody", "Like Patience R, for the looped melody."},
			{"mutate", "The chance, at the end of each pass, that one step of the loop gets a new rhythm or pitch. The loop slowly drifts."},
			{"jump", "The chance, at the end of each pass, that the whole line moves an octave up or down (up to 2 octaves either way)."},
			{"_resetBars", "Sends the loop back to its first step every N bars (Off = never). Mutations and dice keep going."},
			{"size", "How many steps the loop has (Loop modes)."},
			{"first", "The first step of the loop."},
			{"rotate", "Starts the loop from another step, without changing it."},
			{"feel", "45 to 55 leaves the timing alone. Outside that, the loop is stretched or squeezed to last 3-4 beats (low) or 4-5 beats (high), so it runs against the bar."},
			{"degreeRotation", "Rotates which scale degrees are favoured, so the melody centres on another note of the scale."},
			{"maskMelody", "Loop melody only: 100% plays every pitch; lower values mute the less tonal ones, keeping root and fifth longest."},
			{"tiltMelody", "Lets the least tonal pitches through Mask M too, for tense, outside notes."},
			{"repeatProb", "The chance a step replays the previous step instead of its own."},
			{"legatoProb", "The chance a note is tied to the next one (no gap between them)."},
			{"sleep", "After each pass of the loop, pauses for a number of steps (100% = 40 steps)."},
			{"maskRhythm", "Loop rhythm only: thins out the loop. 100% plays every step; lower values mute more steps."},
			{"tiltRhythm", "Which steps Mask R mutes. 50 picks at random; towards 0 it keeps long notes and mutes short ones; towards 100 the opposite."},
			{"maskRotation", "Shuffles the favouring among the allowed degrees, without changing which degrees are allowed."},
			{"marblesSpread", "When Range offers several octaves, how freely the octave is picked: low values stay near Bias, high values use the whole spread."},
			{"marblesBias", "When Range offers several octaves, leans towards low octaves (0) or high octaves (100)."},
			{"burstRate", "How the notes inside a burst are spaced: lower values give wider spacing, higher values tighter, faster bursts."},
			{"burstCount", "How many notes a burst has (2 to 8): higher values favour more notes."},
			{"burstHold", "Hold: a burst repeats the same pitch. Roll: each burst note gets a new pitch. Fit: the burst fits inside the step. Over: it may run past it."},
		}},
		{"Note", {
			{"firstStep", "The first step that plays."},
			{"lastStep", "The last step that plays. First and Last together set the loop length."},
			{"rotate", "Shifts the playback start inside First to Last, without changing the steps."},
			{"gateProbabilityBias", "Adds to every step's Gate % at once. Negative values thin the pattern, positive values fill it in."},
			{"lengthBias", "Adds to every step's Length at once: longer or shorter notes."},
			{"noteProbabilityBias", "Adds to every step's Note var % at once: more or fewer random pitch changes."},
			{"retriggerProbabilityBias", "Adds to every step's Ratch % at once."},
			{"accEnabled", "Turns the accumulator on or off. It adds a moving offset to the pitch of steps with an Accum value."},
			{"accMode", "Stage: the accumulator only changes the pitch of steps with an Accum value. Track: it shifts every step."},
			{"accDirection", "Which way the accumulator counts. Freeze holds its value."},
			{"accOrder", "What happens at the limits. Wrap jumps to the other end, Pendulum turns around, Random picks any value in range, Hold stops at the limit."},
			{"accTrigger", "When the accumulator moves: once per step with an Accum value (Step), on every gate (Gate), or on every ratchet hit (Ratchet)."},
			{"accMin", "The lowest accumulator value, in scale steps."},
			{"accMax", "The highest accumulator value, in scale steps."},
			{"accStep", "How many scale steps the accumulator moves each time it is triggered."},
		}},
		{"Indexed", {
			{"activeLength", "How many steps play (1 to 48)."},
			{"firstStep", "The step the pattern starts on. The order stays the same, only rotated."},
			{"modA", "A modulation source for the routes: this setting plus the MOD A input (+-5 V = +-100%). On its own it does nothing; ROUTE A/B decide what it changes."},
			{"modB", "Like Mod A, for the MOD B input."},
			{"loop", "Loop repeats the steps. Once plays them one time after each reset and then stays silent."},
			{"divisor", "Multiplies every step's duration (1 = as drawn, 2 = half speed)."},
			{"resetBars", "Back to the first step every N bars (Off = never)."},
			{"routeASource", "Which modulation drives route A: Mod A, Mod B or nothing."},
			{"routeATarget", "What route A changes on each step: its Duration (timing), its Gate (note length) or its Note (pitch)."},
			{"routeAGroups", "Which steps route A affects: all of them, the steps tagged with these groups (Groups lane), or steps with no group."},
			{"routeAAmount", "How strongly the modulation changes the target. At 100% with the mod at 100%: Duration doubles, Note moves an octave, Gate grows by 100 ticks. Negative values invert it."},
			{"routeBSource", "Which modulation drives route B: Mod A, Mod B or nothing."},
			{"routeBTarget", "What route B changes on each step: Duration, Gate or Note."},
			{"routeBGroups", "Which steps route B affects."},
			{"routeBAmount", "Like A amount, for route B."},
			{"routeCombine", "When both routes hit the same target on a step: A to B adds them, Mux averages them, Min or Max keeps the smaller or larger change."},
		}},
		{"Discrete", {
			{"clockSource", "What crosses the thresholds: a rising Saw, a Triangle (up then down, so Rise and Fall stages both play), or the Input value (with the INPUT jack)."},
			{"divisor", "How long one full sweep takes. Longer sweeps spread the same notes over more time."},
			{"gateLength", "Note length as a share of the Sweep time. 0% plays a short trigger."},
			{"input", "The level the stages listen to when Source = Input, from -5 V to +5 V. The INPUT jack adds to it (+-5 V = +-100%): patch an LFO or envelope and the melody follows it."},
			{"scanner", "Sweeps across the 32 stages. Each stage it enters flips its trigger to the next type (Rise, Fall, Off, Both), so moving it rewrites the pattern. The SCAN jack adds to it (0-10 V)."},
			{"rangeHigh", "The top of the window the thresholds sit in. Narrowing the window bunches the notes together in time."},
			{"rangeLow", "The bottom of the window the thresholds sit in."},
			{"thresholdMode", "Position: each stage plays where its threshold sits. Length: the active stages are spread evenly over the sweep, whatever their thresholds."},
			{"loop", "Loop: the sweep repeats. Once: one sweep after each reset, then silence."},
			{"playMode", "Free: the sweep counts from the last reset. Bar-aligned: it also restarts every Reset every bars."},
			{"resetBars", "The sweep restarts every N bars (Off = never)."},
		}},
		{"PhaseFlux", {
			{"pulseNudge", "Adds or removes pulses on every stage at once."},
			{"lenNudge", "Makes every stage longer or shorter at once, in Rate steps."},
			{"warpNudge", "Adds to every stage's time and pitch warp: pushes the pulses and the pitch curve towards the start or end."},
			{"responseNudge", "Adds to every stage's time and pitch response: makes the curves steeper or softer."},
			{"cyclePhaseWarp", "Bends time across the whole cycle, so some stages rush and others drag while the cycle length stays the same."},
			{"globalPhase", "Shifts where in the cycle playback starts (0 to 100% of the cycle)."},
			{"traversal", "The walk across the 4x4 grid of stages: Snake, Rows, SnakeUp, Cols, ColSnake, Spiral, Inward or Diag. Same stages, different order."},
			{"pitchMode", "Per stage: every stage runs its own pitch curve. Global: stage 1's pitch curve runs across all stages at Pitch rate, so the melody drifts against the rhythm."},
			{"divisor", "The time unit: a stage lasts Length x Rate (Length 4 at 1/16 = one beat)."},
			{"cycleLength", "Adaptive: skipped stages take no time, so the cycle gets shorter. Fixed: skipped stages keep their time as silence."},
			{"pitchRate", "Global pitch mode only: how much of the pitch curve each stage uses (1:7 = a seventh of the curve per stage, 1:1 = one whole curve per stage)."},
			{"firstStage", "The first stage of the grid that plays."},
			{"lastStage", "The last stage that plays."},
			{"resetMeasure", "Back to the first stage every N bars (Off = never). Accumulators keep their values."},
			{"noteAccScope", "Per stage: every stage has its own note accumulator. Shared: one accumulator moves all stages."},
			{"noteAccOrder", "At the limits: Wrap jumps to the other end, Pendulum turns around, Hold stops, Return goes back to 0."},
			{"noteAccPolarity", "Uni: rises stay between 0 and Max, falls between 0 and Min. Bi: the value can swing between Min and Max."},
			{"noteAccPos", "How far the note accumulator can climb, in scale steps."},
			{"noteAccNeg", "How far the note accumulator can fall, in scale steps."},
			{"noteAccReset", "Clears the note accumulator every N cycles (0 = never)."},
			{"pulseAccScope", "Per stage or Shared, for the pulse accumulator."},
			{"pulseAccOrder", "At the limits of the pulse accumulator: Wrap, Pendulum, Hold or Return."},
			{"pulseAccPolarity", "Uni or Bi range, for the pulse accumulator."},
			{"pulseAccPos", "How many pulses the pulse accumulator can add."},
			{"pulseAccNeg", "How many pulses it can take away."},
			{"pulseAccReset", "Clears the pulse accumulator every N cycles (0 = never)."},
		}},
		{"Fractal", {
			{"record", "Records REC G (gate) and REC CV (1 V/oct) into the loop while on. The REC button toggles it."},
			{"lock", "Freezes the loop (recording is ignored) and repeats the last ornament instead of rolling new ones."},
			{"ornRate", "The chance a note gets an ornament: a short flourish around it. The ORN output fires on each one."},
			{"ornIntensity", "Which ornaments are allowed. Below 40%: anticipations, suspensions, octave and fifth jumps, half turns. From 40%: also runs, turns, arpeggios, mordents. From 75%: also the trill."},
			{"branchCount", "How many transformed copies play after the recorded loop (0 to 7). Each copy transforms the one before it, so the line keeps developing."},
			{"path", "Which branches play on the way out and which on the way back (in reverse order). Each value is a different route through the branches."},
			{"branchSeed", "Picks which transforms the branches use. The same seed always gives the same chain."},
			{"orderMode", "How the loop is read: Forward, Reverse, Pendulum, Random, Converge (both ends to the middle), Diverge (middle out) or Page by page."},
			{"cadence", "Section: one cell is written every Rate step. Event: a cell is written each time the pitch at REC CV changes."},
			{"fidelity", "Quantized: every recorded note starts on the step. Feel: it keeps where in the step you played it."},
			{"recordMode", "Replace: steps where nothing was played are recorded as rests. Latch: only steps with a gate are written (overdub)."},
			{"recordSkip", "After writing a cell, skips this many steps before writing the next. Records a sparser version of what you play."},
			{"recordFirst", "The first cell recording writes to."},
			{"recordLast", "The last cell recording writes to; after it, recording wraps back to Rec first."},
			{"divisor", "One cell = one Rate step, both when recording and when playing back."},
			{"resetMeasure", "Back to Loop first every N bars (Off = never). The recording is kept."},
			{"loopFirst", "The first cell of the loop that plays."},
			{"loopLast", "The last cell of the loop that plays."},
			{"rotate", "Starts reading the loop from another cell."},
			{"trackDelay", "Plays everything this many steps late. Two copies with different delays play a canon."},
			{"branchPool", "Which kinds of transform the branches may use (transpose, reverse, inverse, retrograde inverse, rotate, compress, expand, gate thin). Each value is a different set."},
			{"ornFirst", "Ornaments only happen on cells from Orn first..."},
			{"ornLast", "...to Orn last."},
			{"quantize", "As played: recorded pitches stay as played. To scale: they snap to the scale."},
		}},
	};
	static const HelpMap none;
	auto it = all.find(slug);
	return it == all.end() ? none : it->second;
}

static const std::map<std::string, std::string>& laneHelp(const std::string& slug) {
	static const std::map<std::string, std::map<std::string, std::string>> all = {
		{"Algo", {{"Notes played", "The notes the algorithm has played lately, newest on the right. Read only: turn Flow and Ornament to change them."}}},
		{"Stochastic", {
			{"Pattern (degrees)", "The scale degrees of the looped melody. Read only: New melody rolls a new one."},
			{"Degree tickets", "How likely each scale degree is to be picked. More tickets, more often."},
			{"Length tickets", "How likely each note length is to be picked. More tickets, more often."},
		}},
		{"Note", {
			{"Note", "The pitch, in scale steps from the root."},
			{"Gate", "Whether the step plays a note."},
			{"Gate %", "The chance the step plays (7 = always)."},
			{"Offset", "Delays the note by eighths of the step."},
			{"Slide", "The note slides from the previous one."},
			{"Ratchet", "Plays the note 1 to 4 times inside the step."},
			{"Ratch %", "The chance the ratchet happens."},
			{"Length", "Note length, in eighths of the step."},
			{"Len var", "A random change to Length."},
			{"Len var %", "The chance Len var happens."},
			{"Note var", "A random change to the pitch, up to this many scale steps."},
			{"Note var %", "The chance Note var happens."},
			{"Condition", "When the step may play: Fill, !Fill, Pre (if the last condition passed), First (first pass only), or a:b (pass a of every b)."},
			{"Accum", "Moves the accumulator when this step plays; the value is how far, negative counts the other way."},
			{"Pulses", "Holds the step for 1 to 8 clock steps before moving on."},
			{"Gate mode", "Which of the Pulses play: All, First, Hold (one long note) or First+Last."},
		}},
		{"Indexed", {
			{"Duration", "How long the step lasts before the next one (192 ticks = 1/4). Durations need not add up to a bar."},
			{"Gate", "How long the note sounds inside the step."},
			{"Note", "The pitch, in scale steps."},
			{"Slide", "The note slides from the previous one."},
			{"Groups", "Tags the step with groups A to D, so the routes can target it."},
		}},
		{"Discrete", {
			{"Threshold", "Where on the sweep the stage sits. With Spacing = Position, this sets the stage's timing."},
			{"Direction", "When the stage plays: on a Rise through its threshold, a Fall, Both, or Off."},
			{"Note", "The stage's pitch, in scale steps."},
		}},
		{"Fractal", {
			{"Pitch", "The recorded note of the cell, in semitones."},
			{"Gate", "How long the note was held: rest, trigger, a share of the step, or tie."},
			{"Onset", "Where in the step the note starts (used with Timing = Feel)."},
		}},
	};
	static const std::map<std::string, std::string> phaseFlux = {
		{"Pulses", "How many notes the stage fires (0 to 16)."},
		{"Length", "How long the stage lasts, in Rate steps."},
		{"Skip", "Leaves the stage out."},
		{"Gate", "Note length as a share of the gap to the next pulse."},
		{"Base Pitch", "The note the pitch curve starts from."},
		{"P.Range", "How far the pitch curve reaches (1/2 to 3 octaves)."},
		{"P.Dir", "Whether the pitch curve goes up, down or around Base pitch."},
		{"P.Curve", "The shape of the melody inside the burst: Ramp, Bell, Triangle or Bounce."},
		{"P.Flip V", "Mirrors the pitch curve upside down."},
		{"P.Flip H", "Mirrors the pitch curve in time."},
		{"P.Warp", "Bends the pitch curve towards the start or end."},
		{"P.Resp", "Makes the pitch curve steeper or softer."},
		{"T.Curve", "Where the pulses fall in time: Even, Bell (bunched in the middle) or Bounce."},
		{"T.Flip V", "Mirrors the time curve."},
		{"T.Flip H", "Reverses the time curve."},
		{"T.Warp", "Pushes the pulses early or late."},
		{"T.Resp", "Makes the time curve steeper or softer."},
		{"Mel.Mask", "Mutes the less tonal notes."},
		{"Mel.Tilt", "Lets only the outside notes through instead."},
		{"Phase", "Rotates the pulses in time by eighths of the stage."},
		{"Mask", "Mutes pulses in a pattern (1 in 2, 2 in 3...)."},
		{"Mask Shift", "Shifts the Mask pattern."},
		{"Note Acc", "How far the note accumulator moves for this stage."},
		{"Pulse Acc", "How far the pulse accumulator moves: the pulse count drifts each time."},
		{"N.Acc Trig", "Whether the note accumulator moves once per stage or per pulse."},
		{"P.Acc Trig", "Whether the pulse accumulator moves once per stage or per pulse."},
		{"T.Repeat", "Runs the time curve 1, 2, 3 or 5 times inside the stage."},
		{"P.Repeat", "Runs the pitch curve 1, 2, 3 or 5 times inside the stage."},
		{"T.Window", "Uses only part of the time curve: the centre (Focus) or the edges (Polar)."},
		{"P.Window", "Uses only part of the pitch curve: the centre (Focus) or the edges (Polar)."},
	};
	static const std::map<std::string, std::string> none;
	if (slug == "PhaseFlux")
		return phaseFlux;
	auto it = all.find(slug);
	return it == all.end() ? none : it->second;
}

void applyHelp(Adapter* a, const std::string& slug) {
	const HelpMap& own = engineHelp(slug);
	const HelpMap& common = commonHelp();
	for (Field& f : a->fields) {
		auto it = own.find(f.key);
		if (it != own.end())
			f.help = it->second;
		else if ((it = common.find(f.key)) != common.end())
			f.help = it->second;
	}
	const auto& lanes = laneHelp(slug);
	for (Lane& l : a->lanes) {
		auto it = lanes.find(l.label);
		if (it != lanes.end())
			l.help = it->second;
	}
}

}  // namespace xfm
