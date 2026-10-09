#include "PlaybackClock.h"

#include <algorithm>
#include <cmath>

bool buildPlaybackClock(const std::vector<double>& stepTimes, bool stepsArePlainTime, double pathT0, double pathT1, int frames, PlaybackClock& clock)
{
	clock = PlaybackClock();
	if (frames < 2 || !std::isfinite(pathT0) || !std::isfinite(pathT1) || !(pathT1 > pathT0))
		return false;
	clock.frames = frames;
	bool usable = stepsArePlainTime && stepTimes.size() >= 2 && stepTimes.back() > stepTimes.front();
	for (std::size_t i = 0; usable && i < stepTimes.size(); ++i)
		if (!std::isfinite(stepTimes[i]) || (i > 0 && stepTimes[i] < stepTimes[i - 1]))
			usable = false;
	clock.byTime = usable;
	if (usable)
	{
		clock.t0 = std::min(stepTimes.front(), pathT0);
		clock.t1 = std::max(stepTimes.back(), pathT1);
	}
	else
	{
		clock.t0 = 0.0;
		clock.t1 = 1.0;
	}
	return true;
}

double playbackFraction(const PlaybackClock& clock, int frame)
{
	if (clock.frames < 2)
		return 1.0;
	return std::clamp(static_cast<double>(frame) / static_cast<double>(clock.frames - 1), 0.0, 1.0);
}

double playbackTime(const PlaybackClock& clock, int frame)
{
	const double fraction = playbackFraction(clock, frame);
	return clock.byTime ? clock.t0 + (clock.t1 - clock.t0) * fraction : fraction;
}

double playbackPathlineTime(const PlaybackClock& clock, double pathT0, double pathT1, int frame)
{
	if (clock.byTime)
		return std::clamp(playbackTime(clock, frame), pathT0, pathT1);
	return pathT0 + (pathT1 - pathT0) * playbackFraction(clock, frame);
}

int playbackResultStep(const PlaybackClock& clock, const std::vector<double>& stepTimes, int frame)
{
	if (stepTimes.empty())
		return 0;
	const int last = static_cast<int>(stepTimes.size()) - 1;
	if (!clock.byTime)
		return std::clamp(static_cast<int>(std::lround(playbackFraction(clock, frame) * last)), 0, last);
	const double now = playbackTime(clock, frame);
	// the last step whose time is at or before now (a hair of tolerance keeps a step shown at exactly its own time)
	const double tolerance = 1.0e-9 * std::max(1.0, std::abs(clock.t1 - clock.t0));
	const auto after = std::upper_bound(stepTimes.begin(), stepTimes.end(), now + tolerance);
	const int step = static_cast<int>(after - stepTimes.begin()) - 1;
	return std::clamp(step, 0, last);
}
