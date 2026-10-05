#pragma once

#include "Plot3DData.h"
#include "Plot3DMeshBuilder.h"

#include <QHash>
#include <QString>
#include <QVector>

struct Plot3DFormulaParameter
{
	QString name;
	double value = 0.0;
	double minimum = -1.0e6;
	double maximum = 1.0e6;
};

struct Plot3DFormulaPreset
{
	QString name;
	QString title;
	QString expression;
	double xMinimum = -5.0, xMaximum = 5.0;
	double yMinimum = -5.0, yMaximum = 5.0;
	int xSamples = 81, ySamples = 81;
	QVector<Plot3DFormulaParameter> parameters;
};

struct Plot3DParametricPreset
{
	QString name;
	QString title;
	QString xExpression, yExpression, zExpression;
	double uMinimum = 0.0, uMaximum = 6.283185307179586;
	double vMinimum = 0.0, vMaximum = 6.283185307179586;
	int uSamples = 80, vSamples = 48;
	QVector<Plot3DFormulaParameter> parameters;
};

// One-parameter counterparts to parametric surfaces.  They deliberately
// produce Plot3DLineData so the established GL_LINE_STRIP path keeps their
// width stable in screen pixels as the camera moves.
struct Plot3DParametricCurvePreset
{
	QString name;
	QString title;
	QString xExpression, yExpression, zExpression;
	double tMinimum = 0.0, tMaximum = 6.283185307179586;
	int samples = 240;
	QVector<Plot3DFormulaParameter> parameters;
};

struct Plot3DFormulaVectorPreset
{
	QString name;
	QString title;
	QString uExpression, vExpression, wExpression;
	double xMinimum = -4.0, xMaximum = 4.0;
	double yMinimum = -4.0, yMaximum = 4.0;
	int xSamples = 17, ySamples = 17;
	QVector<Plot3DFormulaParameter> parameters;
};

// Time-dependent vector field (u, v, w may use x, y, z AND t) whose pathlines - the trajectories of particles released
// at t = tMinimum and carried by the changing field - are traced from a column of seeds.
struct Plot3DPathlinePreset
{
	QString name;
	QString title;
	QString uExpression, vExpression, wExpression;
	double xMinimum = -4.0, xMaximum = 4.0;
	double yMinimum = -4.0, yMaximum = 4.0;
	int seeds = 12;
	double tMinimum = 0.0, tMaximum = 10.0;
	int steps = 200;
	QVector<Plot3DFormulaParameter> parameters;
};

struct Plot3DImplicitPreset
{
	QString name;
	QString title;
	QString expression;
	double xMinimum = -3.0, xMaximum = 3.0;
	double yMinimum = -3.0, yMaximum = 3.0;
	double zMinimum = -3.0, zMaximum = 3.0;
	int xSamples = 41, ySamples = 41, zSamples = 41;
	QVector<Plot3DFormulaParameter> parameters;
};

QVector<Plot3DFormulaPreset> plot3DFormulaPresets();
QVector<Plot3DParametricPreset> plot3DParametricPresets();
QVector<Plot3DParametricCurvePreset> plot3DParametricCurvePresets();
QVector<Plot3DFormulaVectorPreset> plot3DFormulaVectorPresets();
QVector<Plot3DImplicitPreset> plot3DImplicitPresets();
QVector<Plot3DPathlinePreset> plot3DPathlinePresets();
bool evaluatePlot3DFormula(const QString& expression, double x, double y,
	const QHash<QString, double>& parameters, double& result, QString* error = nullptr);
bool evaluatePlot3DFormula3D(const QString& expression, double x, double y, double z,
	const QHash<QString, double>& parameters, double& result, QString* error = nullptr);
bool buildPlot3DFormulaSurface(const QString& expression, double xMinimum, double xMaximum, int xSamples,
	double yMinimum, double yMaximum, int ySamples, const QHash<QString, double>& parameters,
	Plot3DSurfaceData& out, QString* error = nullptr);
bool buildPlot3DParametricSurface(const QString& xExpression, const QString& yExpression, const QString& zExpression,
	double uMinimum, double uMaximum, int uSamples, double vMinimum, double vMaximum, int vSamples,
	const QHash<QString, double>& parameters, Plot3DMeshData& out, QString* error = nullptr);
bool buildPlot3DParametricCurve(const QString& xExpression, const QString& yExpression, const QString& zExpression,
	double tMinimum, double tMaximum, int samples, const QHash<QString, double>& parameters,
	Plot3DLineData& out, QString* error = nullptr);
bool buildPlot3DFormulaVectorField(const QString& uExpression, const QString& vExpression, const QString& wExpression,
	double xMinimum, double xMaximum, int xSamples, double yMinimum, double yMaximum, int ySamples,
	const QHash<QString, double>& parameters, Plot3DQuiverData& out, QString* error = nullptr);
// Like evaluatePlot3DFormula3D() but with a time: here `t` is the time argument (not an alias of x, as it is in the other
// evaluators), so a field can vary in time. x, y and z keep their meaning; u and v stay aliases of x and y.
bool evaluatePlot3DFormula4D(const QString& expression, double x, double y, double z, double t,
	const QHash<QString, double>& parameters, double& result, QString* error = nullptr);
// Pathlines of the time-dependent field (u, v, w)(x, y, z, t): particles released at t = tMinimum from `seedCount` seeds
// spread along Y at the middle of the X range (z = 0) are advanced with classical RK4 through `steps` equal time steps up to
// tMaximum, stopping early when one leaves the x / y domain or the field vanishes. Output is GL_LINES-style vertex pairs
// (like the streamlines) whose value is the time at that vertex, so the trails colour by time.
bool buildPlot3DFormulaPathlines(const QString& uExpression, const QString& vExpression, const QString& wExpression,
	double xMinimum, double xMaximum, double yMinimum, double yMaximum, int seedCount,
	double tMinimum, double tMaximum, int steps, const QHash<QString, double>& parameters,
	Plot3DMeshData& out, QString* error = nullptr);
bool buildPlot3DFormulaStreamlines(const QString& uExpression, const QString& vExpression, const QString& wExpression,
	double xMinimum, double xMaximum, double yMinimum, double yMaximum, int seedCount,
	const QHash<QString, double>& parameters, Plot3DMeshData& out, QString* error = nullptr);
bool buildPlot3DImplicitSurface(const QString& expression,
	double xMinimum, double xMaximum, int xSamples, double yMinimum, double yMaximum, int ySamples,
	double zMinimum, double zMaximum, int zSamples, const QHash<QString, double>& parameters,
	Plot3DMeshData& out, QString* error = nullptr);
