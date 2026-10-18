#include "TessellatedReader.h"
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <cmath>

namespace ThreeDXml {
namespace {
struct Element { QString name, text; QHash<QString,QString> attributes; std::vector<Element> children; };
void require(bool ok, const char* reason) { if (!ok) throw Error(QStringLiteral("Invalid TESSELLATED representation: ")+QLatin1String(reason)); }
Element element(QXmlStreamReader& xml, const Options& options, int depth, unsigned& count) {
    options.check(); require(depth <= 128 && ++count <= 1000000,"XML element limit");
    Element e; e.name=xml.name().toString();
    for (const auto& a:xml.attributes()) e.attributes[a.name().toString()]=a.value().toString();
    while (!xml.atEnd()) {
        xml.readNext();
        require(!xml.isDTD() && !xml.isEntityReference(),"DTD/entity not supported");
        if (xml.isStartElement()) e.children.push_back(element(xml,options,depth+1,count));
        else if (xml.isCharacters()) e.text+=xml.text();
        else if (xml.isEndElement()) break;
    }
    return e;
}
const Element* child(const Element& e,const QString& name) {
    for(const auto& c:e.children) if(c.name==name) return &c;
    return nullptr;
}
QStringList tokens(QString value) { value.replace(',', ' '); return value.split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts); }
std::vector<aiVector3D> vectors(const Element& e,const Options& options) {
    const auto values=tokens(e.text); require(values.size()%3==0 && values.size()<=3000000,"vector count");
    std::vector<aiVector3D> result; result.reserve(values.size()/3);
    for(qsizetype i=0;i<values.size();i+=3) {
        if((i&4095)==0) options.check(); aiVector3D v;
        for(int j=0;j<3;++j) { bool ok=false; v[j]=values[i+j].toFloat(&ok); require(ok&&std::isfinite(v[j]),"nonfinite vector"); }
        result.push_back(v);
    }
    return result;
}
unsigned color(const Element& e,unsigned inherited) {
    const Element* attributes=child(e,QStringLiteral("SurfaceAttributes"));
    if(!attributes) return inherited;
    const Element* c=child(*attributes,QStringLiteral("Color")); if(!c) return inherited;
    unsigned rgb=0;
    for(const auto& key:{QStringLiteral("red"),QStringLiteral("green"),QStringLiteral("blue")}) {
        bool ok=false; double v=c->attributes.value(key).toDouble(&ok); require(ok&&std::isfinite(v)&&v>=0&&v<=1,"color component");
        rgb=(rgb<<8)|unsigned(std::lround(v*255));
    }
    return rgb;
}
void faces(const Element& e,Body& body,unsigned inherited,const Options& options) {
    options.check(); if(e.name==QLatin1String("PolygonalLOD")||e.name==QLatin1String("Rep")) return;
    inherited=color(e,inherited);
    if(e.name==QLatin1String("Face")) {
        Face face; face.rgb=inherited;
        for(const auto& kind:{QStringLiteral("triangles"),QStringLiteral("strips"),QStringLiteral("fans")}) {
            const auto groups=e.attributes.value(kind).split(',',Qt::SkipEmptyParts);
            for(const auto& group:groups) {
                const auto words=tokens(group); require(words.size()<=10000000,"index count");
                std::vector<unsigned> indices; indices.reserve(words.size());
                for(const auto& word:words) { bool ok=false; unsigned index=word.toUInt(&ok); require(ok&&index<body.positions.size(),"index range"); indices.push_back(index); }
                if(kind==QLatin1String("triangles")) {
                    require(indices.size()%3==0,"triangle list count");
                    for(size_t i=0;i<indices.size();i+=3) face.triangles.push_back({indices[i],indices[i+1],indices[i+2]});
                } else {
                    require(indices.size()>=3,"strip/fan count");
                    for(size_t i=2;i<indices.size();++i) {
                        if(kind==QLatin1String("fans")) face.triangles.push_back({indices[0],indices[i-1],indices[i]});
                        else if(i%2) face.triangles.push_back({indices[i-1],indices[i-2],indices[i]});
                        else face.triangles.push_back({indices[i-2],indices[i-1],indices[i]});
                    }
                }
            }
        }
        if(!face.triangles.empty()) body.faces.push_back(std::move(face));
    }
    for(const auto& c:e.children) faces(c,body,inherited,options);
}
}
Geometry TessellatedReader::read(const QByteArray& member,const Options& options) {
    QXmlStreamReader xml(member); unsigned count=0;
    while(!xml.atEnd()&&!xml.isStartElement()) { xml.readNext(); require(!xml.isDTD()&&!xml.isEntityReference(),"DTD/entity not supported"); }
    require(xml.isStartElement()&&xml.name()==QLatin1String("XMLRepresentation"),"root element");
    auto root=element(xml,options,0,count);
    while(!xml.atEnd()) { xml.readNext(); require(!xml.isDTD()&&!xml.isEntityReference(),"DTD/entity not supported"); }
    require(!xml.hasError(),"malformed XML"); Geometry geometry;
    std::function<void(const Element&,unsigned)> visit=[&](const Element& e,unsigned inherited) {
        options.check(); inherited=color(e,inherited);
        if(e.attributes.value(QStringLiteral("type"))==QLatin1String("PolygonalRepType")) {
            const auto buffer=child(e,QStringLiteral("VertexBuffer")); require(buffer,"missing vertex buffer");
            const auto positions=child(*buffer,QStringLiteral("Positions")); require(positions,"missing positions");
            Body body; body.offset=geometry.bodies.size(); body.positions=vectors(*positions,options);
            if(const auto normals=child(*buffer,QStringLiteral("Normals"))) body.normals=vectors(*normals,options);
            require(body.normals.empty()||body.normals.size()==body.positions.size(),"normal count differs from positions");
            for(const auto& c:e.children) faces(c,body,inherited,options);
            const auto index=geometry.bodies.size(); geometry.bodies.push_back(std::move(body)); geometry.occurrences.push_back({index,QString::number(index)});
        }
        for(const auto& c:e.children) if(c.name!=QLatin1String("PolygonalLOD")) visit(c,inherited);
    };
    visit(root,0xb2b2b2); return geometry;
}
}
