#pragma once

// One clock for the playback bar when a Simulation result and an animated pathline plot play together ("All together").
//
// The two items do not share a time axis by themselves, so the clock matches them:
//  - BY TIME when both are in plain time (the result's steps have no label and no unit other than seconds, and rise in time): the bar
//    spans the union of the two time ranges, an item holds its first / last frame outside its own range, and at clock time T the result
//    shows its last step at or before T while the pathlines are drawn up to T. This is what a real flow study needs (the result's 0..2 s
//    and the pathlines' 0..1.5 s must line up at 1 s, not at 50 %).
//  - BY PROGRESS otherwise (a modal result is in Hz, pathlines in seconds; or the step times are not usable): both span the same
//    0..100 % of the bar. The bar says so.
//
// Pure functions (QtCore-free), unit-tested in result_tests.

#include <vector>

struct PlaybackClock
{
	bool byTime = true;       // false: matched by progress
	double t0 = 0.0, t1 = 1.0; // by time: the union of the items' time ranges
	int frames = 200;         // positions of the bar (frame 0 .. frames - 1)
};

// Builds the clock for a result with the given step times (`stepsArePlainTime`: no label, no unit other than seconds) and a pathline plot
// spanning pathT0..pathT1. False when the pathline range is empty or `frames` is below 2.
bool buildPlaybackClock(const std::vector<double>& stepTimes, bool stepsArePlainTime, double pathT0, double pathT1, int frames, PlaybackClock& clock);

// frame / (frames - 1), clamped to 0..1.
double playbackFraction(const PlaybackClock& clock, int frame);
// By time: the clock time at `frame`; by progress: the fraction.
double playbackTime(const PlaybackClock& clock, int frame);
// The time the pathlines are drawn up to: by time the clock time held inside the pathlines' own range, by progress the same fraction of it.
double playbackPathlineTime(const PlaybackClock& clock, double pathT0, double pathT1, int frame);
// The result's step at `frame`: by time its last step at or before the clock time (the first step before any), by progress the step at
// the same fraction of its steps. 0 when there are no steps.
int playbackResultStep(const PlaybackClock& clock, const std::vector<double>& stepTimes, int frame);

// The inverse, for seeking from a chart: the first frame at which the result shows `step` (by time the first frame whose clock time is at or
// after the step's time; by progress the frame at the step's fraction). Clamped to the bar.
int playbackFrameForStep(const PlaybackClock& clock, const std::vector<double>& stepTimes, int step);
