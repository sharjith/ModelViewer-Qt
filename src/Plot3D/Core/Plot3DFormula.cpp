#include <QCoreApplication>
#include "Plot3DFormula.h"
#include "Plot3DPathlines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
class Parser
{
public:
	Parser(const QString& expression, double x, double y, double z, const QHash<QString, double>& parameters)
		: _text(expression), _x(x), _y(y), _z(z), _parameters(parameters) {}
	// With a time, `t` means the time instead of being an alias of x.
	Parser(const QString& expression, double x, double y, double z, double time, const QHash<QString, double>& parameters)
		: _text(expression), _x(x), _y(y), _z(z), _time(time), _hasTime(true), _parameters(parameters) {}
	bool parse(double& result, QString& error)
	{
		_skip(); result = _expression(error); _skip();
		if (error.isEmpty() && _pos != _text.size()) error = QStringLiteral("Unexpected character '%1'.").arg(_text[_pos]);
		if (error.isEmpty() && !std::isfinite(result)) error = QStringLiteral("The expression produced a non-finite value.");
		return error.isEmpty();
	}
private:
	void _skip() { while (_pos < _text.size() && _text[_pos].isSpace()) ++_pos; }
	bool _take(QChar ch) { _skip(); if (_pos < _text.size() && _text[_pos] == ch) { ++_pos; return true; } return false; }
	double _expression(QString& e) { double v = _term(e); while (e.isEmpty()) { if (_take('+')) v += _term(e); else if (_take('-')) v -= _term(e); else break; } return v; }
	double _term(QString& e) { double v = _power(e); while (e.isEmpty()) { if (_take('*')) v *= _power(e); else if (_take('/')) { double d = _power(e); if (d == 0.0) { e = QStringLiteral("Division by zero."); return 0.0; } v /= d; } else break; } return v; }
	double _power(QString& e) { double v = _unary(e); if (e.isEmpty() && _take('^')) v = std::pow(v, _power(e)); return v; }
	double _unary(QString& e) { if (_take('+')) return _unary(e); if (_take('-')) return -_unary(e); return _primary(e); }
	double _primary(QString& e)
	{
		_skip(); if (_take('(')) { double v = _expression(e); if (e.isEmpty() && !_take(')')) e = QStringLiteral("Missing closing parenthesis."); return v; }
		const int start = _pos;
		if (_pos < _text.size() && (_text[_pos].isDigit() || _text[_pos] == '.')) { while (_pos < _text.size() && (_text[_pos].isDigit() || _text[_pos] == '.' || _text[_pos].toLower() == 'e' || _text[_pos] == '+' || _text[_pos] == '-')) { if ((_text[_pos] == '+' || _text[_pos] == '-') && _pos > start && _text[_pos - 1].toLower() != 'e') break; ++_pos; } bool ok=false; double v=_text.mid(start,_pos-start).toDouble(&ok); if(!ok) e=QStringLiteral("Invalid number."); return v; }
		if (_pos < _text.size() && (_text[_pos].isLetter() || _text[_pos] == '_'))
		{
			while (_pos < _text.size() && (_text[_pos].isLetterOrNumber() || _text[_pos] == '_')) ++_pos;
			const QString name = _text.mid(start, _pos-start).toLower();
			if (_take('(')) { double a=_expression(e); double b=0; bool two=false; if(e.isEmpty() && _take(',')) { b=_expression(e); two=true; } if(e.isEmpty() && !_take(')')) e=QStringLiteral("Missing closing parenthesis after %1.").arg(name); if(!e.isEmpty()) return 0; if(name=="sin")return std::sin(a); if(name=="cos")return std::cos(a); if(name=="tan")return std::tan(a); if(name=="asin")return std::asin(a); if(name=="acos")return std::acos(a); if(name=="atan")return std::atan(a); if(name=="sinh")return std::sinh(a); if(name=="cosh")return std::cosh(a); if(name=="tanh")return std::tanh(a); if(name=="exp")return std::exp(a); if(name=="log")return a>0?std::log(a):(e=QStringLiteral("log requires a positive value."),0); if(name=="sqrt")return a>=0?std::sqrt(a):(e=QStringLiteral("sqrt requires a non-negative value."),0); if(name=="abs")return std::abs(a); if(name=="sign")return a<0?-1.0:(a>0?1.0:0.0); if(name=="pow"&&two)return std::pow(a,b); if(name=="min"&&two)return std::min(a,b); if(name=="max"&&two)return std::max(a,b); e=QStringLiteral("Unknown function or wrong argument count: %1.").arg(name); return 0; }
			if(_hasTime&&name=="t")return _time; if(name=="x"||name=="u"||name=="t")return _x; if(name=="y"||name=="v")return _y; if(name=="z")return _z; if(name=="pi")return 3.14159265358979323846; if(name=="e")return 2.71828182845904523536;
			if (_parameters.contains(name)) return _parameters.value(name); e=QStringLiteral("Unknown variable: %1.").arg(name); return 0;
		}
		e = QStringLiteral("Expected a number, variable, or expression."); return 0;
	}
	const QString& _text; int _pos=0; double _x, _y, _z; double _time = 0.0; bool _hasTime = false; const QHash<QString,double>& _parameters;
};
}

QVector<Plot3DFormulaPreset> plot3DFormulaPresets()
{
	auto parameter = [](const char* name, double value) { return Plot3DFormulaParameter{ QString::fromLatin1(name), value }; };
	return {
		{ QCoreApplication::translate("Plot3DPresets", "Plane"), QCoreApplication::translate("Plot3DPresets", "Plane"), QStringLiteral("a*x + b*y + c"), -5,5,-5,5,61,61, {parameter("a",0.2),parameter("b",0.15),parameter("c",0.0)} },
		{ QCoreApplication::translate("Plot3DPresets", "Saddle"), QCoreApplication::translate("Plot3DPresets", "Saddle Surface"), QStringLiteral("a*(x^2 - y^2)"), -4,4,-4,4,81,81, {parameter("a",0.2)} },
		{ QCoreApplication::translate("Plot3DPresets", "Paraboloid"), QCoreApplication::translate("Plot3DPresets", "Elliptic Paraboloid"), QStringLiteral("a*(x^2+y^2)"), -4,4,-4,4,81,81, {parameter("a",0.15)} },
		{ QCoreApplication::translate("Plot3DPresets", "Cone"), QCoreApplication::translate("Plot3DPresets", "Circular Cone"), QStringLiteral("a*sqrt(x^2+y^2)"), -5,5,-5,5,81,81, {parameter("a",0.4)} },
		{ QCoreApplication::translate("Plot3DPresets", "Gaussian"), QCoreApplication::translate("Plot3DPresets", "Gaussian Surface"), QStringLiteral("a*exp(-((x-x0)^2+(y-y0)^2)/(2*s^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("x0",0.0),parameter("y0",0.0),parameter("s",1.5)} },
		{ QCoreApplication::translate("Plot3DPresets", "Sinc Ripple"), QCoreApplication::translate("Plot3DPresets", "Sinc Ripple"), QStringLiteral("sin(sqrt(x^2+y^2))/(sqrt(x^2+y^2)+0.000001)"), -10,10,-10,10,101,101, {} },
		{ QCoreApplication::translate("Plot3DPresets", "Wave"), QCoreApplication::translate("Plot3DPresets", "Standing Wave"), QStringLiteral("a*sin(kx*x)*cos(ky*y)"), -6,6,-6,6,101,101, {parameter("a",1.0),parameter("kx",1.0),parameter("ky",1.0)} },
		{ QCoreApplication::translate("Plot3DPresets", "Mexican Hat"), QCoreApplication::translate("Plot3DPresets", "Mexican Hat"), QStringLiteral("a*(1-(x^2+y^2)/s^2)*exp(-(x^2+y^2)/(2*s^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("s",1.5)} },
		{ QCoreApplication::translate("Plot3DPresets", "Bivariate Normal"), QCoreApplication::translate("Plot3DPresets", "Bivariate Normal Density"), QStringLiteral("a*exp(-0.5*((x/sx)^2+(y/sy)^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("sx",1.2),parameter("sy",2.0)} },
		{ QCoreApplication::translate("Plot3DPresets", "Logistic Regression"), QCoreApplication::translate("Plot3DPresets", "Logistic Response Surface"), QStringLiteral("1/(1+exp(-(b0+bx*x+by*y)))"), -5,5,-5,5,81,81, {parameter("b0",0.0),parameter("bx",0.8),parameter("by",1.1)} },
		{ QCoreApplication::translate("Plot3DPresets", "Rosenbrock"), QCoreApplication::translate("Plot3DPresets", "Rosenbrock Function"), QStringLiteral("a*(y-x^2)^2+(b-x)^2"), -2,2,-1,3,101,101, {parameter("a",20.0),parameter("b",1.0)} },
	};
}
QVector<Plot3DParametricPreset> plot3DParametricPresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	const double tau=6.283185307179586;
	return {
		{QCoreApplication::translate("Plot3DPresets", "Torus"), QCoreApplication::translate("Plot3DPresets", "Torus"),QStringLiteral("(r+a*cos(v))*cos(u)"),QStringLiteral("(r+a*cos(v))*sin(u)"),QStringLiteral("a*sin(v)"),0,tau,0,tau,81,49,{p("r",3.0),p("a",1.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Ellipsoid"), QCoreApplication::translate("Plot3DPresets", "Ellipsoid"),QStringLiteral("a*sin(v)*cos(u)"),QStringLiteral("b*sin(v)*sin(u)"),QStringLiteral("c*cos(v)"),0,tau,0,3.141592653589793,81,49,{p("a",3.0),p("b",2.0),p("c",1.25)}},
		{QCoreApplication::translate("Plot3DPresets", "Mobius Strip"), QCoreApplication::translate("Plot3DPresets", "Mobius Strip"),QStringLiteral("(r+v*cos(u/2))*cos(u)"),QStringLiteral("(r+v*cos(u/2))*sin(u)"),QStringLiteral("v*sin(u/2)"),0,tau,-1.0,1.0,101,31,{p("r",2.5)}},
		{QCoreApplication::translate("Plot3DPresets", "Klein Bottle"), QCoreApplication::translate("Plot3DPresets", "Klein Bottle"),QStringLiteral("(6*cos(u)*(1+sin(u))+4*(1-cos(u)/2)*cos(u)*cos(v))/6"),QStringLiteral("4*(1-cos(u)/2)*sin(v)/6"),QStringLiteral("(16*sin(u)+4*(1-cos(u)/2)*sin(u)*cos(v))/6"),0,tau,0,tau,101,61,{}},
		{QCoreApplication::translate("Plot3DPresets", "Superellipsoid"), QCoreApplication::translate("Plot3DPresets", "Superellipsoid"),QStringLiteral("a*sign(cos(v))*abs(cos(v))^e1*sign(cos(u))*abs(cos(u))^e2"),QStringLiteral("a*sign(cos(v))*abs(cos(v))^e1*sign(sin(u))*abs(sin(u))^e2"),QStringLiteral("a*sign(sin(v))*abs(sin(v))^e1"),0,tau,-1.5707963267948966,1.5707963267948966,81,49,{p("a",2.0),p("e1",0.5),p("e2",0.5)}},
		{QCoreApplication::translate("Plot3DPresets", "Helicoid"), QCoreApplication::translate("Plot3DPresets", "Helicoid"),QStringLiteral("v*cos(u)"),QStringLiteral("v*sin(u)"),QStringLiteral("p*u"),-tau,tau,-2.0,2.0,101,41,{p("p",0.18)}},
		{QCoreApplication::translate("Plot3DPresets", "Catenoid"), QCoreApplication::translate("Plot3DPresets", "Catenoid"),QStringLiteral("a*cosh(v/a)*cos(u)"),QStringLiteral("a*cosh(v/a)*sin(u)"),QStringLiteral("v"),0,tau,-2.0,2.0,101,49,{p("a",0.8)}},
		{QCoreApplication::translate("Plot3DPresets", "Enneper Surface"), QCoreApplication::translate("Plot3DPresets", "Enneper Surface"),QStringLiteral("u-u^3/3+u*v^2"),QStringLiteral("v-v^3/3+v*u^2"),QStringLiteral("u^2-v^2"),-2.0,2.0,-2.0,2.0,81,81,{}},
		{QCoreApplication::translate("Plot3DPresets", "Spherical Harmonic"), QCoreApplication::translate("Plot3DPresets", "Spherical Harmonic"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*sin(v)*cos(u)"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*sin(v)*sin(u)"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*cos(v)"),0,tau,0,3.141592653589793,101,61,{p("r",2.0),p("a",0.35),p("m",4.0),p("n",3.0)}},
	};
}
QVector<Plot3DParametricCurvePreset> plot3DParametricCurvePresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	const double tau=6.283185307179586;
	return {
		{QCoreApplication::translate("Plot3DPresets", "Helix"), QCoreApplication::translate("Plot3DPresets", "Helix"),QStringLiteral("r*cos(t)"),QStringLiteral("r*sin(t)"),QStringLiteral("pitch*t/(2*pi)"),0,4*tau,321,{p("r",2.0),p("pitch",1.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Lissajous"), QCoreApplication::translate("Plot3DPresets", "Lissajous Curve"),QStringLiteral("a*sin(p*t+d)"),QStringLiteral("b*sin(q*t)"),QStringLiteral("c*sin(r*t)"),0,tau,401,{p("a",2.5),p("b",2.0),p("c",1.5),p("p",3.0),p("q",4.0),p("r",5.0),p("d",0.5)}},
		{QCoreApplication::translate("Plot3DPresets", "Trefoil Knot"), QCoreApplication::translate("Plot3DPresets", "Trefoil Knot"),QStringLiteral("(r+a*cos(3*t))*cos(2*t)"),QStringLiteral("(r+a*cos(3*t))*sin(2*t)"),QStringLiteral("a*sin(3*t)"),0,tau,401,{p("r",2.0),p("a",0.8)}},
		{QCoreApplication::translate("Plot3DPresets", "Viviani Curve"), QCoreApplication::translate("Plot3DPresets", "Viviani Curve"),QStringLiteral("a*(1+cos(t))"),QStringLiteral("a*sin(t)"),QStringLiteral("2*a*sin(t/2)"),0,2*tau,361,{p("a",1.5)}},
		{QCoreApplication::translate("Plot3DPresets", "Damped Spiral"), QCoreApplication::translate("Plot3DPresets", "Damped Spiral"),QStringLiteral("r*exp(-d*t)*cos(w*t)"),QStringLiteral("r*exp(-d*t)*sin(w*t)"),QStringLiteral("pitch*t"),0,6*tau,481,{p("r",3.0),p("d",0.08),p("w",1.0),p("pitch",0.08)}},
	};
}
QVector<Plot3DFormulaVectorPreset> plot3DFormulaVectorPresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	return {
		{QCoreApplication::translate("Plot3DPresets", "Vortex"), QCoreApplication::translate("Plot3DPresets", "Planar Vortex"),QStringLiteral("-s*y"),QStringLiteral("s*x"),QStringLiteral("0"),-4,4,-4,4,17,17,{p("s",1.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Radial"), QCoreApplication::translate("Plot3DPresets", "Radial Field"),QStringLiteral("s*x"),QStringLiteral("s*y"),QStringLiteral("0"),-4,4,-4,4,17,17,{p("s",1.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Saddle"), QCoreApplication::translate("Plot3DPresets", "Saddle Field"),QStringLiteral("s*x"),QStringLiteral("-s*y"),QStringLiteral("0"),-4,4,-4,4,17,17,{p("s",1.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Helical"), QCoreApplication::translate("Plot3DPresets", "Helical Field"),QStringLiteral("-s*y"),QStringLiteral("s*x"),QStringLiteral("rise"),-4,4,-4,4,17,17,{p("s",1.0),p("rise",0.75)}},
	};
}
QVector<Plot3DPathlinePreset> plot3DPathlinePresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	const double tau=6.283185307179586;
	return {
		// Rigid rotation whose rate pulses in time: particles keep circling but speed up and slow down, so the colour (time) bands
		// along each ring bunch up and spread out.
		{QCoreApplication::translate("Plot3DPresets", "Pulsating Vortex"), QCoreApplication::translate("Plot3DPresets", "Pulsating Vortex Pathlines"),
			QStringLiteral("-s*y*(1+a*sin(w*t))"),QStringLiteral("s*x*(1+a*sin(w*t))"),QStringLiteral("0"),
			-4,4,-4,4,10,0,12,240,{p("s",0.6),p("a",0.8),p("w",1.5)}},
		// Shadden's double gyre: two counter-rotating cells whose dividing line oscillates, the textbook unsteady flow where
		// pathlines and streamlines differ. Runs over x 0..2, y 0..1.
		{QCoreApplication::translate("Plot3DPresets", "Double Gyre"), QCoreApplication::translate("Plot3DPresets", "Double Gyre Pathlines"),
			QStringLiteral("-pi*a*sin(pi*(eps*sin(om*t)*x^2+(1-2*eps*sin(om*t))*x))*cos(pi*y)"),
			QStringLiteral("pi*a*cos(pi*(eps*sin(om*t)*x^2+(1-2*eps*sin(om*t))*x))*sin(pi*y)*(2*eps*sin(om*t)*x+1-2*eps*sin(om*t))"),QStringLiteral("0"),
			0,2,0,1,12,0,30,600,{p("a",0.1),p("eps",0.25),p("om",0.6283185307179586)}},
		// A steady stream deflected by a wave travelling in x: particles drift sideways in a pattern that depends on when they
		// pass each crest.
		{QCoreApplication::translate("Plot3DPresets", "Travelling Wave"), QCoreApplication::translate("Plot3DPresets", "Travelling Wave Pathlines"),
			QStringLiteral("u0"),QStringLiteral("a*sin(k*x-w*t)"),QStringLiteral("0"),
			-4,4,-3,3,12,0,12,240,{p("u0",0.6),p("a",0.5),p("k",1.5),p("w",1.0)}},
		// A vortex with a vertical velocity that oscillates in time: the rings climb and sink, drawing helical trails in Z.
		{QCoreApplication::translate("Plot3DPresets", "Oscillating Updraft"), QCoreApplication::translate("Plot3DPresets", "Oscillating Updraft Pathlines"),
			QStringLiteral("-s*y"),QStringLiteral("s*x"),QStringLiteral("r*sin(w*t)"),
			-3,3,-3,3,8,0,tau*2,320,{p("s",0.5),p("r",0.6),p("w",0.8)}},
	};
}
QVector<Plot3DImplicitPreset> plot3DImplicitPresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	return {
		{QCoreApplication::translate("Plot3DPresets", "Sphere"), QCoreApplication::translate("Plot3DPresets", "Implicit Sphere"),QStringLiteral("x^2+y^2+z^2-r^2"),-3,3,-3,3,-3,3,41,41,41,{p("r",2.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Torus"), QCoreApplication::translate("Plot3DPresets", "Implicit Torus"),QStringLiteral("(sqrt(x^2+y^2)-r)^2+z^2-a^2"),-4,4,-4,4,-2,2,49,49,33,{p("r",2.3),p("a",0.8)}},
		{QCoreApplication::translate("Plot3DPresets", "Gyroid"), QCoreApplication::translate("Plot3DPresets", "Gyroid Surface"),QStringLiteral("sin(x)*cos(y)+sin(y)*cos(z)+sin(z)*cos(x)-level"),-3.141592653589793,3.141592653589793,-3.141592653589793,3.141592653589793,-3.141592653589793,3.141592653589793,49,49,49,{p("level",0.0)}},
		{QCoreApplication::translate("Plot3DPresets", "Wave Interference"), QCoreApplication::translate("Plot3DPresets", "Wave Interference Isosurface"),QStringLiteral("sin(k*x)+sin(k*y)+sin(k*z)-level"),-3.141592653589793,3.141592653589793,-3.141592653589793,3.141592653589793,-3.141592653589793,3.141592653589793,49,49,49,{p("k",1.0),p("level",0.0)}},
	};
}
bool evaluatePlot3DFormula(const QString& expression,double x,double y,const QHash<QString,double>& parameters,double& result,QString* error)
{ return evaluatePlot3DFormula3D(expression, x, y, 0.0, parameters, result, error); }
bool evaluatePlot3DFormula4D(const QString& expression,double x,double y,double z,double t,const QHash<QString,double>& parameters,double& result,QString* error)
{ QString local; Parser parser(expression,x,y,z,t,parameters); const bool ok=parser.parse(result,local); if(error)*error=local; return ok; }
bool evaluatePlot3DFormula3D(const QString& expression,double x,double y,double z,const QHash<QString,double>& parameters,double& result,QString* error)
{ QString local; Parser parser(expression,x,y,z,parameters); const bool ok=parser.parse(result,local); if(error)*error=local; return ok; }
bool buildPlot3DFormulaSurface(const QString& expression,double x0,double x1,int nx,double y0,double y1,int ny,const QHash<QString,double>& parameters,Plot3DSurfaceData& out,QString* error)
{
	out.samples.clear(); if(nx<2||ny<2||nx>512||ny>512||!(x1>x0)||!(y1>y0)){if(error)*error=QStringLiteral("Formula ranges must increase and each resolution must be 2 to 512.");return false;}
	out.samples.reserve(static_cast<size_t>(nx)*ny); for(int iy=0;iy<ny;++iy) for(int ix=0;ix<nx;++ix){ double x=x0+(x1-x0)*ix/(nx-1),y=y0+(y1-y0)*iy/(ny-1),z; QString local; if(!evaluatePlot3DFormula(expression,x,y,parameters,z,&local)){if(error)*error=QStringLiteral("At x=%1, y=%2: %3").arg(x).arg(y).arg(local);out.samples.clear();return false;} out.samples.push_back({{x,y,z},z});} return true;
}
bool buildPlot3DParametricSurface(const QString& xe,const QString& ye,const QString& ze,double u0,double u1,int nu,double v0,double v1,int nv,const QHash<QString,double>& parameters,Plot3DMeshData& out,QString* error)
{
	out=Plot3DMeshData(); if(nu<2||nv<2||nu>512||nv>512||!(u1>u0)||!(v1>v0)){if(error)*error=QStringLiteral("Parametric ranges must increase and each resolution must be 2 to 512.");return false;}
	const size_t count=static_cast<size_t>(nu)*nv; out.positions.resize(count*3); out.normals.assign(count*3,0.0f); out.values.resize(count);
	for(int i=0;i<nu;++i) for(int j=0;j<nv;++j){const double u=u0+(u1-u0)*i/(nu-1),v=v0+(v1-v0)*j/(nv-1);double x,y,z;QString e;if(!evaluatePlot3DFormula(xe,u,v,parameters,x,&e)||!evaluatePlot3DFormula(ye,u,v,parameters,y,&e)||!evaluatePlot3DFormula(ze,u,v,parameters,z,&e)){if(error)*error=QStringLiteral("At u=%1, v=%2: %3").arg(u).arg(v).arg(e);out=Plot3DMeshData();return false;}const size_t k=(static_cast<size_t>(i)*nv+j)*3;out.positions[k]=float(x);out.positions[k+1]=float(y);out.positions[k+2]=float(z);out.values[static_cast<size_t>(i)*nv+j]=z;}
	for(int i=0;i+1<nu;++i)for(int j=0;j+1<nv;++j){const unsigned int a=i*nv+j,b=a+1,c=(i+1)*nv+j,d=c+1;out.indices.insert(out.indices.end(),{a,c,b,b,c,d});}
	for(size_t k=0;k<out.indices.size();k+=3){const unsigned int a=out.indices[k],b=out.indices[k+1],c=out.indices[k+2];const float ax=out.positions[a*3],ay=out.positions[a*3+1],az=out.positions[a*3+2],bx=out.positions[b*3]-ax,by=out.positions[b*3+1]-ay,bz=out.positions[b*3+2]-az,cx=out.positions[c*3]-ax,cy=out.positions[c*3+1]-ay,cz=out.positions[c*3+2]-az;const float nx=by*cz-bz*cy,ny=bz*cx-bx*cz,nz=bx*cy-by*cx;for(unsigned int q:{a,b,c}){out.normals[q*3]+=nx;out.normals[q*3+1]+=ny;out.normals[q*3+2]+=nz;}}
	for(size_t i=0;i<count;++i){float& x=out.normals[i*3];float& y=out.normals[i*3+1];float& z=out.normals[i*3+2];const float l=std::sqrt(x*x+y*y+z*z);if(l>1e-12f){x/=l;y/=l;z/=l;}else z=1.0f;}return true;
}
bool buildPlot3DParametricCurve(const QString& xe,const QString& ye,const QString& ze,double t0,double t1,int count,const QHash<QString,double>& parameters,Plot3DLineData& out,QString* error)
{
	out.samples.clear();
	if(count<2||count>8192||!(t1>t0))
	{
		if(error)*error=QStringLiteral("The parameter range must increase and the sample count must be 2 to 8192.");
		return false;
	}
	out.samples.reserve(static_cast<size_t>(count));
	for(int i=0;i<count;++i)
	{
		const double t=t0+(t1-t0)*i/(count-1);
		double x,y,z; QString e;
		if(!evaluatePlot3DFormula(xe,t,0.0,parameters,x,&e)||!evaluatePlot3DFormula(ye,t,0.0,parameters,y,&e)||!evaluatePlot3DFormula(ze,t,0.0,parameters,z,&e))
		{
			if(error)*error=QStringLiteral("At t=%1: %2").arg(t).arg(e);
			out.samples.clear();
			return false;
		}
		out.samples.push_back({{x,y,z},z});
	}
	return true;
}
bool buildPlot3DFormulaVectorField(const QString& ue,const QString& ve,const QString& we,double x0,double x1,int nx,double y0,double y1,int ny,const QHash<QString,double>& parameters,Plot3DQuiverData& out,QString* error)
{
	out.arrows.clear();
	if(nx<2||ny<2||nx>128||ny>128||!(x1>x0)||!(y1>y0)) { if(error)*error=QStringLiteral("Vector-field ranges must increase and each resolution must be 2 to 128."); return false; }
	out.arrows.reserve(static_cast<size_t>(nx)*ny);
	for(int iy=0;iy<ny;++iy) for(int ix=0;ix<nx;++ix)
	{
		const double x=x0+(x1-x0)*ix/(nx-1), y=y0+(y1-y0)*iy/(ny-1); double u,v,w; QString e;
		if(!evaluatePlot3DFormula(ue,x,y,parameters,u,&e)||!evaluatePlot3DFormula(ve,x,y,parameters,v,&e)||!evaluatePlot3DFormula(we,x,y,parameters,w,&e)) { if(error)*error=QStringLiteral("At x=%1, y=%2: %3").arg(x).arg(y).arg(e); out.arrows.clear(); return false; }
		out.arrows.push_back({{x,y,0.0},{u,v,w},std::sqrt(u*u+v*v+w*w)});
	}
	return true;
}

bool buildPlot3DFormulaStreamlines(const QString& ue, const QString& ve, const QString& we,
	double x0, double x1, double y0, double y1, int seedCount, const QHash<QString, double>& parameters, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (seedCount < 2 || seedCount > 128 || !(x1 > x0) || !(y1 > y0)) { if (error) *error = QStringLiteral("Streamline ranges must increase and the seed count must be 2 to 128."); return false; }
	const double step = std::min(x1 - x0, y1 - y0) / 120.0;
	for (int seed = 0; seed < seedCount; ++seed)
	{
		const Plot3DPoint seedPoint{ (x0 + x1) * 0.5, y0 + (y1 - y0) * seed / (seedCount - 1), 0.0 };
		// Centred seeds let a closed field complete a full orbit without drawing
		// the same path twice in opposite directions.
		for (const double direction : { 1.0 })
		{
			Plot3DPoint point = seedPoint;
			for (int iteration = 0; iteration < 480; ++iteration)
			{
				double u, v, w; QString local;
				if (!evaluatePlot3DFormula3D(ue, point.x, point.y, point.z, parameters, u, &local)
					|| !evaluatePlot3DFormula3D(ve, point.x, point.y, point.z, parameters, v, &local)
					|| !evaluatePlot3DFormula3D(we, point.x, point.y, point.z, parameters, w, &local)) { if (error) *error = local; out = Plot3DMeshData(); return false; }
				const double magnitude = std::sqrt(u*u + v*v + w*w);
				if (magnitude < 1.0e-12) break;
				const Plot3DPoint next{ point.x + direction * step * u / magnitude, point.y + direction * step * v / magnitude, point.z + direction * step * w / magnitude };
				if (next.x < x0 || next.x > x1 || next.y < y0 || next.y > y1) break;
				const double seedDistance = std::sqrt((next.x - seedPoint.x) * (next.x - seedPoint.x) + (next.y - seedPoint.y) * (next.y - seedPoint.y) + (next.z - seedPoint.z) * (next.z - seedPoint.z));
				if (iteration > 16 && seedDistance < step * 1.5) break;
				for (const Plot3DPoint& p : { point, next }) { out.positions.insert(out.positions.end(), { float(p.x), float(p.y), float(p.z) }); out.normals.insert(out.normals.end(), { 0.0f, 0.0f, 1.0f }); out.values.push_back(magnitude); }
				point = next;
			}
		}
	}
	if (out.empty()) { if (error) *error = QStringLiteral("No streamline segments were generated in the selected domain."); return false; }
	return true;
}

bool buildPlot3DFormulaPathlines(const QString& ue, const QString& ve, const QString& we,
	double x0, double x1, double y0, double y1, int seedCount, double t0, double t1, int steps,
	const QHash<QString, double>& parameters, Plot3DMeshData& out, QString* error)
{
	// The field at a position and time; fails (with the message, tagged with the time) when an expression cannot be evaluated.
	const Plot3DUnsteadyField field = [&](const Plot3DVec3& p, double t, Plot3DVec3& v, QString* message) {
		QString local;
		if (!evaluatePlot3DFormula4D(ue, p.x, p.y, p.z, t, parameters, v.x, &local)
			|| !evaluatePlot3DFormula4D(ve, p.x, p.y, p.z, t, parameters, v.y, &local)
			|| !evaluatePlot3DFormula4D(we, p.x, p.y, p.z, t, parameters, v.z, &local))
		{
			if (message) *message = QStringLiteral("At t=%1: %2").arg(t).arg(local);
			return false;
		}
		return true;
	};
	Plot3DPathlineDomain domain;
	domain.xMinimum = x0; domain.xMaximum = x1; domain.yMinimum = y0; domain.yMaximum = y1;
	return tracePlot3DPathlines(field, domain, seedCount, t0, t1, steps, out, error);
}

bool buildPlot3DImplicitSurface(const QString& expression,
	double xMinimum, double xMaximum, int xSamples, double yMinimum, double yMaximum, int ySamples,
	double zMinimum, double zMaximum, int zSamples, const QHash<QString, double>& parameters,
	Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (xSamples < 2 || ySamples < 2 || zSamples < 2 || xSamples > 64 || ySamples > 64 || zSamples > 64
		|| !(xMaximum > xMinimum) || !(yMaximum > yMinimum) || !(zMaximum > zMinimum))
	{
		if (error) *error = QStringLiteral("Implicit-surface ranges must increase and each resolution must be 2 to 64.");
		return false;
	}
	const double dx = (xMaximum - xMinimum) / (xSamples - 1);
	const double dy = (yMaximum - yMinimum) / (ySamples - 1);
	const double dz = (zMaximum - zMinimum) / (zSamples - 1);
	struct Sample { Plot3DPoint position; double value = 0.0; };
	std::vector<Sample> samples(static_cast<size_t>(xSamples) * ySamples * zSamples);
	auto index = [=](int x, int y, int z) { return (static_cast<size_t>(z) * ySamples + y) * xSamples + x; };
	for (int z = 0; z < zSamples; ++z)
		for (int y = 0; y < ySamples; ++y)
			for (int x = 0; x < xSamples; ++x)
			{
				Sample& sample = samples[index(x, y, z)];
				sample.position = { xMinimum + dx * x, yMinimum + dy * y, zMinimum + dz * z };
				QString local;
				if (!evaluatePlot3DFormula3D(expression, sample.position.x, sample.position.y, sample.position.z, parameters, sample.value, &local))
				{
					if (error) *error = QStringLiteral("At x=%1, y=%2, z=%3: %4").arg(sample.position.x).arg(sample.position.y).arg(sample.position.z).arg(local);
					out = Plot3DMeshData();
					return false;
				}
			}

	auto interpolate = [](const Sample& first, const Sample& second)
	{
		const double denominator = first.value - second.value;
		const double t = std::abs(denominator) > 1.0e-15 ? std::clamp(first.value / denominator, 0.0, 1.0) : 0.5;
		return Plot3DPoint{ first.position.x + (second.position.x - first.position.x) * t,
			first.position.y + (second.position.y - first.position.y) * t,
			first.position.z + (second.position.z - first.position.z) * t };
	};
	auto addTriangle = [&](Plot3DPoint first, Plot3DPoint second, Plot3DPoint third)
	{
		const double abx = second.x - first.x, aby = second.y - first.y, abz = second.z - first.z;
		const double acx = third.x - first.x, acy = third.y - first.y, acz = third.z - first.z;
		double nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
		const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (length < 1.0e-15) return;
		// The tetrahedra do not provide one global winding convention. Orient every
		// face against the scalar-field gradient so lighting and back-face culling
		// stay coherent across cube boundaries.
		const Plot3DPoint center{ (first.x + second.x + third.x) / 3.0, (first.y + second.y + third.y) / 3.0, (first.z + second.z + third.z) / 3.0 };
		auto valueAt = [&](double x, double y, double z, double& value) { return evaluatePlot3DFormula3D(expression, x, y, z, parameters, value, nullptr); };
		double xp, xm, yp, ym, zp, zm;
		if (valueAt(center.x + dx * 0.25, center.y, center.z, xp) && valueAt(center.x - dx * 0.25, center.y, center.z, xm)
			&& valueAt(center.x, center.y + dy * 0.25, center.z, yp) && valueAt(center.x, center.y - dy * 0.25, center.z, ym)
			&& valueAt(center.x, center.y, center.z + dz * 0.25, zp) && valueAt(center.x, center.y, center.z - dz * 0.25, zm))
		{
			const double gx = xp - xm, gy = yp - ym, gz = zp - zm;
			if (nx * gx + ny * gy + nz * gz < 0.0)
			{
				std::swap(second, third);
				nx = -nx; ny = -ny; nz = -nz;
			}
		}
		const std::uint32_t firstIndex = static_cast<std::uint32_t>(out.vertexCount());
		for (const Plot3DPoint& point : { first, second, third })
		{
			out.positions.insert(out.positions.end(), { static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z) });
			out.normals.insert(out.normals.end(), { static_cast<float>(nx / length), static_cast<float>(ny / length), static_cast<float>(nz / length) });
			out.values.push_back(static_cast<float>(point.z));
		}
		out.indices.insert(out.indices.end(), { firstIndex, firstIndex + 1, firstIndex + 2 });
	};
	auto polygoniseTetrahedron = [&](const Sample* tetra)
	{
		int inside[4], outside[4], insideCount = 0, outsideCount = 0;
		for (int i = 0; i < 4; ++i)
			(tetra[i].value <= 0.0 ? inside[insideCount++] : outside[outsideCount++]) = i;
		if (insideCount == 0 || insideCount == 4) return;
		if (insideCount == 1 || insideCount == 3)
		{
			const int pivot = insideCount == 1 ? inside[0] : outside[0];
			const int* others = insideCount == 1 ? outside : inside;
			addTriangle(interpolate(tetra[pivot], tetra[others[0]]), interpolate(tetra[pivot], tetra[others[1]]), interpolate(tetra[pivot], tetra[others[2]]));
			return;
		}
		const Plot3DPoint a = interpolate(tetra[inside[0]], tetra[outside[0]]);
		const Plot3DPoint b = interpolate(tetra[inside[0]], tetra[outside[1]]);
		const Plot3DPoint c = interpolate(tetra[inside[1]], tetra[outside[0]]);
		const Plot3DPoint d = interpolate(tetra[inside[1]], tetra[outside[1]]);
		addTriangle(a, b, c);
		addTriangle(c, b, d);
	};
	constexpr int cubeTetrahedra[6][4] = { { 0, 5, 1, 6 }, { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 } };
	for (int z = 0; z + 1 < zSamples; ++z)
		for (int y = 0; y + 1 < ySamples; ++y)
			for (int x = 0; x + 1 < xSamples; ++x)
			{
				const Sample cube[8] = { samples[index(x,y,z)], samples[index(x+1,y,z)], samples[index(x+1,y+1,z)], samples[index(x,y+1,z)],
					samples[index(x,y,z+1)], samples[index(x+1,y,z+1)], samples[index(x+1,y+1,z+1)], samples[index(x,y+1,z+1)] };
				for (const auto& tetrahedron : cubeTetrahedra)
				{
					const Sample tetra[4] = { cube[tetrahedron[0]], cube[tetrahedron[1]], cube[tetrahedron[2]], cube[tetrahedron[3]] };
					polygoniseTetrahedron(tetra);
				}
			}
	if (out.empty())
	{
		if (error) *error = QStringLiteral("The implicit field does not cross zero inside the selected ranges.");
		return false;
	}
	return true;
}
