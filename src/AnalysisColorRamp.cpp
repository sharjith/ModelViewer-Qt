#include "AnalysisColorRamp.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

std::vector<float> AnalysisColorRamp::mapToRGBA(
	const std::vector<float>& scalarPerSample,
	const std::vector<bool>& validPerSample,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap)
{
	std::vector<float> rgba;
	rgba.reserve(scalarPerSample.size() * 4);

	const float range = rangeMax - rangeMin;
	// A degenerate (zero-width) range means every valid sample has the same
	// value - map it to the middle of the ramp rather than dividing by zero.
	const bool degenerateRange = std::fabs(range) < 1.0e-9f;

	for (size_t i = 0; i < scalarPerSample.size(); ++i)
	{
		const bool valid = (i < validPerSample.size()) ? validPerSample[i] : true;
		QColor color;
		if (!valid)
		{
			color = invalidSampleColor();
		}
		else
		{
			const float t = degenerateRange
				? 0.5f
				: std::clamp((scalarPerSample[i] - rangeMin) / range, 0.0f, 1.0f);
			color = colorForNormalized(t, colormap);
		}
		rgba.push_back(static_cast<float>(color.redF()));
		rgba.push_back(static_cast<float>(color.greenF()));
		rgba.push_back(static_cast<float>(color.blueF()));
		rgba.push_back(1.0f);
	}
	return rgba;
}

QColor AnalysisColorRamp::colorForNormalized(float t, AnalysisColormap colormap)
{
	t = std::clamp(t, 0.0f, 1.0f);
	switch (colormap)
	{
	case AnalysisColormap::Sequential:
	{
		// Blue (hue 240 degrees) -> Red (hue 0), full saturation/value - a
		// standard "Jet-like" rainbow ramp. HSV-based rather than hand-
		// tuned RGB breakpoints, for a clean, easily-verified implementation.
		const qreal hueDegrees = (1.0 - static_cast<qreal>(t)) * 240.0;
		return QColor::fromHsvF(hueDegrees / 360.0, 1.0, 1.0);
	}
	case AnalysisColormap::Diverging:
	default:
	{
		if (t < 0.5f)
		{
			// Blue -> White
			const qreal f = static_cast<qreal>(t) * 2.0;
			const int channel = static_cast<int>(f * 255.0);
			return QColor(channel, channel, 255);
		}
		else
		{
			// White -> Red
			const qreal f = (static_cast<qreal>(t) - 0.5) * 2.0;
			const int channel = static_cast<int>(255.0 - f * 255.0);
			return QColor(255, channel, channel);
		}
	}
	}
}

QColor AnalysisColorRamp::invalidSampleColor()
{
	return QColor(128, 128, 128);
}

QPixmap AnalysisColorRamp::legendGradient(
	int width, int height,
	float rangeMin, float rangeMax,
	AnalysisColormap colormap,
	const QString& unitSuffix)
{
	QPixmap pixmap(std::max(width, 1), std::max(height, 1));
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);

	// Reserve the bottom ~40% of the pixmap for the min/max text labels,
	// keeping the gradient strip itself as the dominant visual element.
	const int barHeight = std::max(static_cast<int>(height * 0.6), 4);
	for (int x = 0; x < width; ++x)
	{
		const float t = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
		painter.setPen(colorForNormalized(t, colormap));
		painter.drawLine(x, 0, x, barHeight - 1);
	}

	painter.setPen(Qt::black);
	const QString minLabel = QString::number(rangeMin, 'g', 3) + unitSuffix;
	const QString maxLabel = QString::number(rangeMax, 'g', 3) + unitSuffix;
	painter.drawText(QRect(0, barHeight, width / 2, height - barHeight), Qt::AlignLeft | Qt::AlignVCenter, minLabel);
	painter.drawText(QRect(width / 2, barHeight, width - width / 2, height - barHeight), Qt::AlignRight | Qt::AlignVCenter, maxLabel);

	return pixmap;
}
