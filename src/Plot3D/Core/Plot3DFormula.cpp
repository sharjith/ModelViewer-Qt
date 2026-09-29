#include "Plot3DFormula.h"

#include <algorithm>
#include <cmath>

namespace
{
class Parser
{
public:
	Parser(const QString& expression, double x, double y, const QHash<QString, double>& parameters)
		: _text(expression), _x(x), _y(y), _parameters(parameters) {}
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
			if(name=="x"||name=="u"||name=="t")return _x; if(name=="y"||name=="v")return _y; if(name=="pi")return 3.14159265358979323846; if(name=="e")return 2.71828182845904523536;
			if (_parameters.contains(name)) return _parameters.value(name); e=QStringLiteral("Unknown variable: %1.").arg(name); return 0;
		}
		e = QStringLiteral("Expected a number, variable, or expression."); return 0;
	}
	const QString& _text; int _pos=0; double _x, _y; const QHash<QString,double>& _parameters;
};
}

QVector<Plot3DFormulaPreset> plot3DFormulaPresets()
{
	auto parameter = [](const char* name, double value) { return Plot3DFormulaParameter{ QString::fromLatin1(name), value }; };
	return {
		{ QStringLiteral("Plane"), QStringLiteral("Plane"), QStringLiteral("a*x + b*y + c"), -5,5,-5,5,61,61, {parameter("a",0.2),parameter("b",0.15),parameter("c",0.0)} },
		{ QStringLiteral("Saddle"), QStringLiteral("Saddle Surface"), QStringLiteral("a*(x^2 - y^2)"), -4,4,-4,4,81,81, {parameter("a",0.2)} },
		{ QStringLiteral("Paraboloid"), QStringLiteral("Elliptic Paraboloid"), QStringLiteral("a*(x^2+y^2)"), -4,4,-4,4,81,81, {parameter("a",0.15)} },
		{ QStringLiteral("Cone"), QStringLiteral("Circular Cone"), QStringLiteral("a*sqrt(x^2+y^2)"), -5,5,-5,5,81,81, {parameter("a",0.4)} },
		{ QStringLiteral("Gaussian"), QStringLiteral("Gaussian Surface"), QStringLiteral("a*exp(-((x-x0)^2+(y-y0)^2)/(2*s^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("x0",0.0),parameter("y0",0.0),parameter("s",1.5)} },
		{ QStringLiteral("Sinc Ripple"), QStringLiteral("Sinc Ripple"), QStringLiteral("sin(sqrt(x^2+y^2))/(sqrt(x^2+y^2)+0.000001)"), -10,10,-10,10,101,101, {} },
		{ QStringLiteral("Wave"), QStringLiteral("Standing Wave"), QStringLiteral("a*sin(kx*x)*cos(ky*y)"), -6,6,-6,6,101,101, {parameter("a",1.0),parameter("kx",1.0),parameter("ky",1.0)} },
		{ QStringLiteral("Mexican Hat"), QStringLiteral("Mexican Hat"), QStringLiteral("a*(1-(x^2+y^2)/s^2)*exp(-(x^2+y^2)/(2*s^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("s",1.5)} },
		{ QStringLiteral("Bivariate Normal"), QStringLiteral("Bivariate Normal Density"), QStringLiteral("a*exp(-0.5*((x/sx)^2+(y/sy)^2))"), -5,5,-5,5,81,81, {parameter("a",1.0),parameter("sx",1.2),parameter("sy",2.0)} },
		{ QStringLiteral("Logistic Regression"), QStringLiteral("Logistic Response Surface"), QStringLiteral("1/(1+exp(-(b0+bx*x+by*y)))"), -5,5,-5,5,81,81, {parameter("b0",0.0),parameter("bx",0.8),parameter("by",1.1)} },
		{ QStringLiteral("Rosenbrock"), QStringLiteral("Rosenbrock Function"), QStringLiteral("a*(y-x^2)^2+(b-x)^2"), -2,2,-1,3,101,101, {parameter("a",20.0),parameter("b",1.0)} },
	};
}
QVector<Plot3DParametricPreset> plot3DParametricPresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	const double tau=6.283185307179586;
	return {
		{QStringLiteral("Torus"),QStringLiteral("Torus"),QStringLiteral("(r+a*cos(v))*cos(u)"),QStringLiteral("(r+a*cos(v))*sin(u)"),QStringLiteral("a*sin(v)"),0,tau,0,tau,81,49,{p("r",3.0),p("a",1.0)}},
		{QStringLiteral("Ellipsoid"),QStringLiteral("Ellipsoid"),QStringLiteral("a*sin(v)*cos(u)"),QStringLiteral("b*sin(v)*sin(u)"),QStringLiteral("c*cos(v)"),0,tau,0,3.141592653589793,81,49,{p("a",3.0),p("b",2.0),p("c",1.25)}},
		{QStringLiteral("Mobius Strip"),QStringLiteral("Mobius Strip"),QStringLiteral("(r+v*cos(u/2))*cos(u)"),QStringLiteral("(r+v*cos(u/2))*sin(u)"),QStringLiteral("v*sin(u/2)"),0,tau,-1.0,1.0,101,31,{p("r",2.5)}},
		{QStringLiteral("Klein Bottle"),QStringLiteral("Klein Bottle"),QStringLiteral("(6*cos(u)*(1+sin(u))+4*(1-cos(u)/2)*cos(u)*cos(v))/6"),QStringLiteral("4*(1-cos(u)/2)*sin(v)/6"),QStringLiteral("(16*sin(u)+4*(1-cos(u)/2)*sin(u)*cos(v))/6"),0,tau,0,tau,101,61,{}},
		{QStringLiteral("Superellipsoid"),QStringLiteral("Superellipsoid"),QStringLiteral("a*sign(cos(v))*abs(cos(v))^e1*sign(cos(u))*abs(cos(u))^e2"),QStringLiteral("a*sign(cos(v))*abs(cos(v))^e1*sign(sin(u))*abs(sin(u))^e2"),QStringLiteral("a*sign(sin(v))*abs(sin(v))^e1"),0,tau,-1.5707963267948966,1.5707963267948966,81,49,{p("a",2.0),p("e1",0.5),p("e2",0.5)}},
		{QStringLiteral("Helicoid"),QStringLiteral("Helicoid"),QStringLiteral("v*cos(u)"),QStringLiteral("v*sin(u)"),QStringLiteral("p*u"),-tau,tau,-2.0,2.0,101,41,{p("p",0.18)}},
		{QStringLiteral("Catenoid"),QStringLiteral("Catenoid"),QStringLiteral("a*cosh(v/a)*cos(u)"),QStringLiteral("a*cosh(v/a)*sin(u)"),QStringLiteral("v"),0,tau,-2.0,2.0,101,49,{p("a",0.8)}},
		{QStringLiteral("Enneper Surface"),QStringLiteral("Enneper Surface"),QStringLiteral("u-u^3/3+u*v^2"),QStringLiteral("v-v^3/3+v*u^2"),QStringLiteral("u^2-v^2"),-2.0,2.0,-2.0,2.0,81,81,{}},
		{QStringLiteral("Spherical Harmonic"),QStringLiteral("Spherical Harmonic"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*sin(v)*cos(u)"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*sin(v)*sin(u)"),QStringLiteral("(r+a*sin(m*v)*cos(n*u))*cos(v)"),0,tau,0,3.141592653589793,101,61,{p("r",2.0),p("a",0.35),p("m",4.0),p("n",3.0)}},
	};
}
QVector<Plot3DParametricCurvePreset> plot3DParametricCurvePresets()
{
	auto p=[](const char* name,double value){return Plot3DFormulaParameter{QString::fromLatin1(name),value};};
	const double tau=6.283185307179586;
	return {
		{QStringLiteral("Helix"),QStringLiteral("Helix"),QStringLiteral("r*cos(t)"),QStringLiteral("r*sin(t)"),QStringLiteral("pitch*t/(2*pi)"),0,4*tau,321,{p("r",2.0),p("pitch",1.0)}},
		{QStringLiteral("Lissajous"),QStringLiteral("Lissajous Curve"),QStringLiteral("a*sin(p*t+d)"),QStringLiteral("b*sin(q*t)"),QStringLiteral("c*sin(r*t)"),0,tau,401,{p("a",2.5),p("b",2.0),p("c",1.5),p("p",3.0),p("q",4.0),p("r",5.0),p("d",0.5)}},
		{QStringLiteral("Trefoil Knot"),QStringLiteral("Trefoil Knot"),QStringLiteral("(r+a*cos(3*t))*cos(2*t)"),QStringLiteral("(r+a*cos(3*t))*sin(2*t)"),QStringLiteral("a*sin(3*t)"),0,tau,401,{p("r",2.0),p("a",0.8)}},
		{QStringLiteral("Viviani Curve"),QStringLiteral("Viviani Curve"),QStringLiteral("a*(1+cos(t))"),QStringLiteral("a*sin(t)"),QStringLiteral("2*a*sin(t/2)"),0,2*tau,361,{p("a",1.5)}},
		{QStringLiteral("Damped Spiral"),QStringLiteral("Damped Spiral"),QStringLiteral("r*exp(-d*t)*cos(w*t)"),QStringLiteral("r*exp(-d*t)*sin(w*t)"),QStringLiteral("pitch*t"),0,6*tau,481,{p("r",3.0),p("d",0.08),p("w",1.0),p("pitch",0.08)}},
	};
}
bool evaluatePlot3DFormula(const QString& expression,double x,double y,const QHash<QString,double>& parameters,double& result,QString* error)
{ QString local; Parser parser(expression,x,y,parameters); const bool ok=parser.parse(result,local); if(error)*error=local; return ok; }
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
