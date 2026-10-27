#include "../../../include/Materials/TextureMipChain.h"
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtSceneBuilder.h"
#include "JtTextureBorderImage.h"
#include "JtTextureClampMesh.h"
#include "JtTextureMapping.h"
#include "JtTextureTangents.h"
#include <JtData_Model.hxx>
#include <JtData_Limits.hxx>
#include <JtNode_Partition.hxx>
#include <JtNode_Part.hxx>
#include <JtNode_Instance.hxx>
#include <JtNode_LOD.hxx>
#include <JtNode_RangeLOD.hxx>
#include <JtNode_Switch.hxx>
#include <JtNode_Shape_Base.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <JtElement_XTBRep.hxx>
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
#include <JtAttribute_DrawStyle.hxx>
#include <JtAttribute_TextureImage.hxx>
#include <JtAttribute_PaletteMap.hxx>
#include <tuple>
#include <JtProperty_String.hxx>
#include <JtProperty_Scalar.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QColor>
#include <QImageReader>
#include <QCryptographicHash>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <array>
#include <limits>
#include <numeric>
#include <functional>
#include <unordered_set>

namespace Jt {
namespace {
QString nameOf(const Handle(JtNode_Base)& node) {
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(node->Name().ToExtString()));
}
QString fileKey(const QString& path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}
bool insideDirectory(const QString& file,const QString& directory) {
    return fileKey(file).startsWith(fileKey(directory+"/"));
}
bool finite(const aiVector3D& p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool containsMesh(const aiNode& node) {
    if(node.mNumMeshes) return true;
    for(unsigned i=0;i<node.mNumChildren;++i) if(containsMesh(*node.mChildren[i])) return true;
    return false;
}
void validate(const aiMesh& mesh) {
    if(!mesh.mNumVertices || !mesh.mNumFaces || !mesh.mVertices || !mesh.mFaces)
        throw std::runtime_error("JT representation contains no triangles");
    for(unsigned i=0;i<mesh.mNumVertices;++i) {
        if(!finite(mesh.mVertices[i])) throw std::runtime_error("JT has nonfinite vertex coordinates");
        if(mesh.mNormals && (!finite(mesh.mNormals[i]) || mesh.mNormals[i].SquareLength()<0.1f))
            throw std::runtime_error("JT has invalid vertex normals");
    }
    for(unsigned i=0;i<mesh.mNumFaces;++i) {
        const auto& f=mesh.mFaces[i];
        if(f.mNumIndices!=3 || !f.mIndices) throw std::runtime_error("JT has a non-triangle render primitive");
        for(unsigned j=0;j<3;++j) if(f.mIndices[j]>=mesh.mNumVertices) throw std::runtime_error("JT triangle index out of range");
    }
}
std::unique_ptr<aiMesh> exactMesh(const TopoDS_Shape& shape,const Options& options) {
    if(shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid()) throw std::runtime_error("Exact JT shape failed B-Rep validation");
    options.check();
    BRepMesh_IncrementalMesh mesher(shape,0.1,false,0.3,false);
    if(!mesher.IsDone()) throw std::runtime_error("Cannot tessellate exact JT shape");
    std::vector<aiVector3D> positions,normals;
    for(TopExp_Explorer ex(shape,TopAbs_FACE);ex.More();ex.Next()) {
        options.check(); const auto face=TopoDS::Face(ex.Current()); TopLoc_Location location;
        const auto tri=BRep_Tool::Triangulation(face,location);
        if(tri.IsNull()) throw std::runtime_error("Exact JT face has no triangulation");
        for(int i=1;i<=tri->NbTriangles();++i) {
            int a,b,c; tri->Triangle(i).Get(a,b,c);
            if(face.Orientation()==TopAbs_REVERSED) std::swap(b,c);
            aiVector3D p[3]; const int indices[]={a,b,c};
            for(int k=0;k<3;++k) {const auto q=tri->Node(indices[k]).Transformed(location.Transformation());p[k]={float(q.X()),float(q.Y()),float(q.Z())};}
            auto n=(p[1]-p[0])^(p[2]-p[0]); if(n.SquareLength()<1e-20f) continue; n.Normalize();
            for(const auto& q:p) {positions.push_back(q);normals.push_back(n);}
        }
    }
    auto out=std::make_unique<aiMesh>(); out->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    out->mNumVertices=unsigned(positions.size()); out->mNumFaces=out->mNumVertices/3;
    out->mVertices=new aiVector3D[out->mNumVertices]; out->mNormals=new aiVector3D[out->mNumVertices];
    std::copy(positions.begin(),positions.end(),out->mVertices); std::copy(normals.begin(),normals.end(),out->mNormals);
    out->mFaces=new aiFace[out->mNumFaces];
    for(unsigned i=0;i<out->mNumFaces;++i) {out->mFaces[i].mNumIndices=3;out->mFaces[i].mIndices=new unsigned[3]{3*i,3*i+1,3*i+2};}
    validate(*out);return out;
}
std::unique_ptr<aiMesh> storedMesh(const Handle(JtElement_ShapeLOD_Vertex)& source,unsigned& repairedNormals) {
    const auto& p=source->Vertices();const auto& n=source->Normals();const auto& ix=source->Indices();
    const auto& colors=source->Colors();
    if(p.Count()<=0 || p.CompCount()!=3 || ix.Count()<=0 || ix.Count()%3)
        throw std::runtime_error("Unsupported/empty JT tessellation layout");
    auto out=std::make_unique<aiMesh>();out->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    out->mNumVertices=unsigned(p.Count());out->mNumFaces=unsigned(ix.Count()/3);
    JtData_Limits::allocation(size_t(out->mNumVertices)*2*sizeof(aiVector3D)+size_t(out->mNumFaces)*(sizeof(aiFace)+3*sizeof(unsigned)));
    out->mVertices=new aiVector3D[out->mNumVertices];out->mNormals=new aiVector3D[out->mNumVertices]{};
    const bool storedNormals=n.Count()==p.Count() && n.CompCount()==3;
    repairedNormals=0;
    for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel) {
        const auto& uv=source->TexCoords(channel);
        if(!uv.Count())continue;
        if(uv.Count()!=p.Count() || (uv.CompCount()!=1 && uv.CompCount()!=2))throw std::runtime_error("Invalid expanded JT texture coordinates");
        JtData_Limits::allocation(size_t(out->mNumVertices)*sizeof(aiVector3D));
        out->mTextureCoords[channel]=new aiVector3D[out->mNumVertices];
        // The renderer and glTF use UV pairs; embed scalar U as (U,0).
        out->mNumUVComponents[channel]=2;
        for(unsigned i=0;i<out->mNumVertices;++i) {
            if(!std::isfinite(uv[i][0]) || (uv.CompCount()==2 && !std::isfinite(uv[i][1])))throw std::runtime_error("Nonfinite JT texture coordinate");
            out->mTextureCoords[channel][i]={uv[i][0],uv.CompCount()==2?uv[i][1]:0.f,0};
        }
    }
    // Assimp counts UV arrays up to the first empty slot. Pad gaps with zeros
    // so scene copies/export retain the original higher set numbers. The JT
    // decoder still distinguishes absent source sets from populated sets.
    unsigned uvSpan=0;
    for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)
        if(out->HasTextureCoords(channel))uvSpan=channel+1;
    for(unsigned channel=0;channel<uvSpan;++channel)if(!out->HasTextureCoords(channel)) {
        JtData_Limits::allocation(size_t(out->mNumVertices)*sizeof(aiVector3D));
        out->mTextureCoords[channel]=new aiVector3D[out->mNumVertices]{};
        out->mNumUVComponents[channel]=2;
    }
    if(colors.Count()) {
        if(colors.Count()!=p.Count() || (colors.CompCount()!=3 && colors.CompCount()!=4))
            throw std::runtime_error("Invalid expanded JT vertex colours");
        JtData_Limits::allocation(size_t(out->mNumVertices)*sizeof(aiColor4D));
        out->mColors[0]=new aiColor4D[out->mNumVertices];
        for(unsigned i=0;i<out->mNumVertices;++i) {
            auto& c=out->mColors[0][i];c={colors[i][0],colors[i][1],colors[i][2],colors.CompCount()==4?colors[i][3]:1.0f};
            for(float v:{c.r,c.g,c.b,c.a}) if(!std::isfinite(v) || v<0 || v>1)
                throw std::runtime_error("Invalid JT vertex colour component");
        }
    }
    for(unsigned i=0;i<out->mNumVertices;++i) {out->mVertices[i]={p[i][0],p[i][1],p[i][2]};if(storedNormals) out->mNormals[i]={n[i][0],n[i][1],n[i][2]};}
    if(storedNormals) for(unsigned i=0;i<out->mNumVertices;++i) {
        const auto& normal=out->mNormals[i];
        if(!finite(normal) || !std::isfinite(normal.SquareLength())) throw std::runtime_error("JT has nonfinite stored normals");
        if(normal.SquareLength()<1e-20f) ++repairedNormals;
    }
    std::vector<aiVector3D> generated;
    if(repairedNormals) {JtData_Limits::allocation(size_t(out->mNumVertices)*sizeof(aiVector3D));generated.resize(out->mNumVertices);}
    out->mFaces=new aiFace[out->mNumFaces];
    for(unsigned i=0;i<out->mNumFaces;++i) {
        auto& f=out->mFaces[i];f.mNumIndices=3;f.mIndices=new unsigned[3];
        for(unsigned j=0;j<3;++j) {const int index=ix[3*i+j];if(index<0 || unsigned(index)>=out->mNumVertices) throw std::runtime_error("JT tessellation index out of range");f.mIndices[j]=unsigned(index);}
        if(!storedNormals) {auto v=(out->mVertices[f.mIndices[1]]-out->mVertices[f.mIndices[0]])^(out->mVertices[f.mIndices[2]]-out->mVertices[f.mIndices[0]]);for(unsigned j=0;j<3;++j)out->mNormals[f.mIndices[j]]+=v;}
        else if(repairedNormals) {auto v=(out->mVertices[f.mIndices[1]]-out->mVertices[f.mIndices[0]])^(out->mVertices[f.mIndices[2]]-out->mVertices[f.mIndices[0]]);for(unsigned j=0;j<3;++j)generated[f.mIndices[j]]+=v;}
    }
    for(unsigned i=0;i<out->mNumVertices;++i) {
        if(storedNormals && repairedNormals && out->mNormals[i].SquareLength()<1e-20f) out->mNormals[i]=generated[i];
        if(out->mNormals[i].SquareLength()<1e-20f) out->mNormals[i]={0,0,1};
        out->mNormals[i].Normalize();
    }
    validate(*out);return out;
}
struct TextureState {
    Handle(JtAttribute_TextureImage) image;
    Handle(JtAttribute_TextureImage) assignment;
    int type=0,coordinate=0;
    unsigned finalFields=0;
    std::array<int,8> environment{0,2,6,2,2,2,2,0};
    std::array<float,4> borderColor{};
    std::array<float,4> blendColor{};
    bool automatic=false;
    std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<int,4> generation{};
    std::array<float,16> planes{};
    bool colorRole() const {return type==9||type==2;}
    bool replaceRGB() const {
        if(environment[6]!=3||image.IsNull()||!image->Inline()||image->Images().size()!=1)return false;
        const auto format=image->Images()[0].format;
        return format==1||format==2||format==11||format==12;
    }
    bool modelLinearComponents() const {
        for(auto mode:generation)if(mode<0||mode>1)return false;
        for(unsigned component=0;component<4;++component)if(generation[component]==1){
            const auto* p=planes.data()+4*component;
            const double norm=double(p[0])*p[0]+double(p[1])*p[1]+double(p[2])*p[2];
            if(!std::isfinite(norm)||!std::isfinite(p[3])||std::abs(norm-1.0)>1e-5)return false;
        }
        return true;
    }
    bool finiteTransform() const {
        // Whether the homogeneous result can be represented by ordinary UVs is
        // checked against the actual mesh before any coordinates are committed.
        return std::all_of(transform.begin(),transform.end(),[](float v){return std::isfinite(v);});
    }
    void apply(const Handle(JtAttribute_TextureImage)& source) {
        const auto raw=source->FieldInhibitFlags();
        // Basic v9 assigns compression inhibit to bit 8; modern data uses 7.
        const unsigned inhibit=source->Legacy9()?(raw&0x7fu)|((raw&0x100u)>>1):raw;
        const unsigned allowed=~inhibit & (source->AccumulationForced()?~0u:~finalFields) & 0xffu;
        const auto& env=source->Environment();
        if(allowed&1){image=source;type=source->TextureType()==26?0:source->TextureType();
            if(source->Legacy9()&&source->LocalVersion()==1){
                if(type==2)type=9; // basic v9 2D image -> supported effective pre-lit profile
                else if(type!=0)type=-1; // basic types must not acquire advanced meanings
            }
        }
        if(allowed&2){environment[0]=env[0];for(unsigned i=0;i<4;++i)borderColor[i]=source->Colors()[4+i];}
        if(allowed&4){environment[1]=env[1];environment[2]=env[2];}
        if(allowed&8)for(unsigned i=3;i<6;++i)environment[i]=env[i];
        if(allowed&16){environment[6]=env[6];for(unsigned i=0;i<4;++i)blendColor[i]=source->Colors()[i];}
        if(allowed&32)transform=source->Transform();
        if(allowed&64){coordinate=source->CoordinateChannel();generation=source->Generation();planes=source->Planes();}
        if(allowed&128)environment[7]=env[7];
        const auto rawFinal=source->FieldFinalFlags();
        const unsigned final=source->Legacy9()?(rawFinal&0x7fu)|((rawFinal&0x100u)>>1):rawFinal;
        finalFields|=final&allowed;
    }
};
struct Appearance {
    TextureState texture;
    // Ordered source units remain separate from the primary display mapping.
    // Unit numbers are sparse identifiers, not UV coordinate-set indices.
    std::map<int,TextureState> textureUnits;
    TextureState textureDefaults;
    void refreshTexture() {
        texture=TextureState{};
        for(const auto& item:textureUnits)if(item.second.colorRole()&&item.second.environment[6]!=0){texture=item.second;break;}
    }
    void discardNormalTextures(){for(auto& unit:textureUnits)if(unit.second.type==4)unit.second.type=0;}
    void applyTexture(const Handle(JtAttribute_TextureImage)& source) {
        const auto reset=source->TextureType()==26;
        if(reset){
            // A final reset also fences units first introduced by descendants.
            textureDefaults.apply(source);
            for(auto it=textureUnits.begin();it!=textureUnits.end();){const auto type=it->second.type;
                if(type!=6&&(type<20||type>25))it->second.apply(source);
                if(it->second.type==0&&it->second.finalFields==0)it=textureUnits.erase(it);else ++it;
            }
            refreshTexture();return;
        }
        int channel=source->Channel();
        const auto vacant=[&](){int slot=0;while(textureUnits.count(slot))++slot;return slot;};
        if(channel<0){
            // Stable when an inherited attribute is encountered again; new
            // automatic roles choose the lowest unused slot, never an explicit one.
            for(const auto& item:textureUnits)if(item.second.automatic&&item.second.assignment==source){channel=item.first;break;}
            if(channel<0)channel=vacant();
        }else {
            const auto occupied=textureUnits.find(channel);
            if(source->TextureType()!=0&&occupied!=textureUnits.end()&&occupied->second.automatic){
                if(textureUnits.size()>=64)throw std::runtime_error("JT texture unit limit exceeded");
                JtData_Limits::allocation(sizeof(TextureState)+64);
                const int relocated=vacant();textureUnits.emplace(relocated,std::move(occupied->second));textureUnits.erase(occupied);
            }
        }
        if(!textureUnits.count(channel)){
            if(textureUnits.size()>=64)throw std::runtime_error("JT texture unit limit exceeded");
            JtData_Limits::allocation(sizeof(TextureState)+64);
        }
        const int role=source->TextureType();
        const bool excepted=role==6||(role>=20&&role<=25);
        auto& state=textureUnits.try_emplace(channel,excepted?TextureState{}:textureDefaults).first->second;
        state.apply(source);state.automatic=source->Channel()<0;
        if(state.automatic)state.assignment=source;else state.assignment.Nullify();
        if(state.type==0&&state.finalFields==0)textureUnits.erase(channel);
        refreshTexture();
    }
    aiColor4D diffuse{0.75f,0.75f,0.78f,1};
    aiColor3D ambient{},specular{},emission{};
    float bumpiness=1,reflectivity=0,shininess=15;
    bool hasShininess=false,shininessFinal=false;
    bool legacyShininess=false;
    bool overrideColors=false;
    unsigned finalFields=0,present=0;
    unsigned blendFlags=0;
    unsigned drawFlags=0x0a,drawPresent=0,drawFinal=0;
    bool flatNormals() const {return (drawFlags&0x10)!=0;}
    void apply(const JtAttribute_DrawStyle& source) {
        const unsigned allowed=~source.FieldInhibitFlags() & (source.AccumulationForced() ? ~0u : ~drawFinal) & 0x3f;
        // Field bits 0/1 (lighting/culling) swap places in the data byte.
        for(unsigned field=0;field<6;++field) if(allowed&(1u<<field)) {
            const unsigned dataBit=field==0 ? 2u : field==1 ? 1u : 1u<<field;
            drawFlags=(drawFlags&~dataBit) | (source.DataFlags()&dataBit);
        }
        drawPresent|=allowed;drawFinal|=source.FieldFinalFlags()&allowed;
    }
    void apply(const JtAttribute_Material& source) {
        const unsigned allowed=~source.FieldInhibitFlags() & (source.AccumulationForced() ? ~0u : ~finalFields);
        const auto rgb=[](const float* c){return aiColor3D(c[0],c[1],c[2]);};
        const auto c=source.DiffuseColor();
        if(allowed&(1u<<0)) ambient=rgb(source.AmbientColor());
        if(allowed&(1u<<1)) specular=rgb(source.SpecularColor());
        if(allowed&(1u<<2)) emission=rgb(source.EmissionColor());
        if(allowed&(1u<<3)) blendFlags=source.DataFlags()&0xffd0u;
        if(allowed&(1u<<4)) overrideColors=(source.DataFlags()&0x20)!=0;
        // Table 16 assigns no inhibit/final bit to shininess. It replaces
        // the inherited exponent unless the entire attribute is final.
        // In particular, bit 4 controls vertex-colour override, not shininess.
        if(!shininessFinal || source.AccumulationForced()) {
            shininess=source.Shininess();hasShininess=true;legacyShininess=source.IsLegacyLayout();
        }
        shininessFinal=shininessFinal || source.AccumulationFinal();
        if(allowed&(1u<<5)) reflectivity=source.Reflectivity();
        if(allowed&(1u<<6)) {diffuse.r=c[0];diffuse.g=c[1];diffuse.b=c[2];}
        if(allowed&(1u<<7)) diffuse.a=c[3];
        if(allowed&(1u<<8)) bumpiness=source.Bumpiness();
        present|=allowed&0x1efu;
        finalFields|=source.FieldFinalFlags()&allowed;
    }
};
struct MaterialSourceContext {
    bool qualifiedSolidEdgeWriter=false;
    bool solidEdgeAuthor=false;
};
// This is a measured exporter compatibility profile, not JT list-order semantics.
// Keep it limited to the tested file/writer pairs and explicit author metadata.
MaterialSourceContext materialSourceContext(const QString& path,const Options& options) {
    MaterialSourceContext context;
    if(!options.solidEdgeDuplicateMaterialCompatibility)return context;
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return context;
    const auto header=file.read(80).trimmed().simplified();
    context.qualifiedSolidEdgeWriter=header=="Version 10.9 JT DM 10.8.0.1"
        ||header=="Version 10.5 JT ISO14306-4 DM 10.8.0.1"
        ||header=="Version 10.7 JT DM 10.8.0.1"
        ||header=="Version 10.8 JT DM 10.8.0.1"
        ||header=="Version 10.10 JT DM 10.8.0.1";
    return context;
}
bool solidEdgeAuthor(const JtNode_Base& node) {
    for(const auto& property:node.Properties()) {
        const auto key=Handle(JtProperty_String)::DownCast(property.key);
        const auto value=Handle(JtProperty_String)::DownCast(property.value);
        if(key.IsNull()||value.IsNull())continue;
        const auto text=[](const TCollection_ExtendedString& x){return QString::fromUtf16(reinterpret_cast<const char16_t*>(x.ToExtString()),x.Length());};
        if(text(key->Value())=="CAD_SOURCE::"&&text(value->Value())=="Solid Edge")return true;
    }
    return false;
}
bool ordinaryFallbackMaterial(const JtAttribute_Material& material) {
    return material.BaseVersion()==2&&material.PaletteIndex()==-1
        &&!material.AccumulationIgnored()&&!material.AccumulationForced()
        &&material.FieldInhibitFlags()==0&&material.FieldFinalFlags()==0
        &&material.DataFlags()==0x3980;
}
struct PaletteEntry {
    Appearance style;
    aiMatrix4x4 world;
    bool transformLocked=false;
};
struct PaletteContext {
    using Entries=std::map<int,PaletteEntry>;
    std::shared_ptr<Entries> entries=std::make_shared<Entries>();
    Handle(JtAttribute_PaletteMap) mapping;
    bool mapLocked=false;
};
aiMatrix4x4 transformMatrix(const JtAttribute_GeometricTransform& source) {
    const auto t=source.GetTrsf();
    for(unsigned j=0;j<16;++j)if(!std::isfinite(float(t[j])))throw std::runtime_error("JT has nonfinite transform values");
    const double tolerance=64*std::numeric_limits<double>::epsilon();
    if(std::abs(t[3])>tolerance||std::abs(t[7])>tolerance||std::abs(t[11])>tolerance||std::abs(t[15]-1)>tolerance)throw std::runtime_error("Unsupported projective JT transform");
    return aiMatrix4x4(float(t[0]),float(t[4]),float(t[8]),float(t[12]),float(t[1]),float(t[5]),float(t[9]),float(t[13]),float(t[2]),float(t[6]),float(t[10]),float(t[14]),0,0,0,1);
}
std::unique_ptr<aiMesh> faceSubset(const aiMesh& source,const std::vector<unsigned>& faces) {
    // A sorted index vector avoids a tree node allocation per used vertex and
    // charges actual arrays separately, rather than treating their sum as one allocation.
    JtData_Limits::allocation(faces.size()*3*sizeof(unsigned));
    std::vector<unsigned> vertices;vertices.reserve(faces.size()*3);
    for(auto face:faces)for(unsigned j=0;j<3;++j)vertices.push_back(source.mFaces[face].mIndices[j]);
    std::sort(vertices.begin(),vertices.end());vertices.erase(std::unique(vertices.begin(),vertices.end()),vertices.end());
    const unsigned next=unsigned(vertices.size());
    auto mesh=std::make_unique<aiMesh>();mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices=next;mesh->mNumFaces=unsigned(faces.size());
    JtData_Limits::allocation(size_t(next)*sizeof(aiVector3D));mesh->mVertices=new aiVector3D[next];
    JtData_Limits::allocation(size_t(next)*sizeof(aiVector3D));mesh->mNormals=new aiVector3D[next];
    if(source.HasVertexColors(0)){JtData_Limits::allocation(size_t(next)*sizeof(aiColor4D));mesh->mColors[0]=new aiColor4D[next];}
    for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)
        if(source.HasTextureCoords(channel)){JtData_Limits::allocation(size_t(next)*sizeof(aiVector3D));mesh->mTextureCoords[channel]=new aiVector3D[next];mesh->mNumUVComponents[channel]=source.mNumUVComponents[channel];}
    for(unsigned i=0;i<next;++i) {
        mesh->mVertices[i]=source.mVertices[vertices[i]];mesh->mNormals[i]=source.mNormals[vertices[i]];
        if(mesh->mColors[0])mesh->mColors[0][i]=source.mColors[0][vertices[i]];
        for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)
            if(mesh->mTextureCoords[channel])mesh->mTextureCoords[channel][i]=source.mTextureCoords[channel][vertices[i]];
    }
    JtData_Limits::allocation(faces.size()*sizeof(aiFace));mesh->mFaces=new aiFace[faces.size()];
    JtData_Limits::allocation(faces.size()*3*sizeof(unsigned));
    for(unsigned i=0;i<faces.size();++i){
        JtData_Limits::poll();auto& f=mesh->mFaces[i];f.mNumIndices=3;f.mIndices=new unsigned[3];
        for(unsigned j=0;j<3;++j)f.mIndices[j]=unsigned(std::lower_bound(vertices.begin(),vertices.end(),source.mFaces[faces[i]].mIndices[j])-vertices.begin());
    }
    validate(*mesh);return mesh;
}
// Face-group batches that share a transform and hidden state and differ only in diffuse colour and opacity can be one mesh with
// per-vertex colours. Textured, colour-overriding or vertex-coloured material states are never merged.
bool mergeableStyle(const Appearance& s) {
    return !s.overrideColors && s.textureUnits.empty() && s.texture.image.IsNull() && s.texture.type==0;
}
// Every material field except the diffuse colour and alpha (those travel as vertex colours).
std::vector<float> styleSignature(const Appearance& s) {
    return {s.ambient.r,s.ambient.g,s.ambient.b,s.specular.r,s.specular.g,s.specular.b,s.emission.r,s.emission.g,s.emission.b,
        s.bumpiness,s.reflectivity,s.shininess,float(s.hasShininess),float(s.legacyShininess),float(s.overrideColors),float(s.present),
        float(s.drawFlags),float(s.drawPresent),float(s.blendFlags)};
}
// Concatenates triangle pieces of one source mesh into one mesh; every piece's vertices take that piece's colour.
std::unique_ptr<aiMesh> concatenateColoured(const std::vector<std::unique_ptr<aiMesh>>& pieces,const std::vector<aiColor4D>& colours) {
    size_t vertices=0,faces=0;
    for(const auto& piece:pieces){vertices+=piece->mNumVertices;faces+=piece->mNumFaces;}
    if(pieces.empty()||vertices>std::numeric_limits<unsigned>::max()||faces>std::numeric_limits<unsigned>::max())throw std::runtime_error("JT merged face groups exceed the supported size");
    JtData_Limits::allocation(vertices*(2*sizeof(aiVector3D)+sizeof(aiColor4D))+faces*(sizeof(aiFace)+3*sizeof(unsigned)));
    auto mesh=std::make_unique<aiMesh>();mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices=unsigned(vertices);mesh->mNumFaces=unsigned(faces);
    mesh->mVertices=new aiVector3D[vertices];mesh->mNormals=new aiVector3D[vertices];mesh->mColors[0]=new aiColor4D[vertices];
    const auto& first=*pieces.front();
    for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)
        if(first.HasTextureCoords(channel)){mesh->mTextureCoords[channel]=new aiVector3D[vertices];mesh->mNumUVComponents[channel]=first.mNumUVComponents[channel];}
    mesh->mFaces=new aiFace[faces];
    unsigned vertexBase=0,faceBase=0;
    for(size_t k=0;k<pieces.size();++k) {
        const auto& piece=*pieces[k];
        for(unsigned i=0;i<piece.mNumVertices;++i) {
            JtData_Limits::poll();
            mesh->mVertices[vertexBase+i]=piece.mVertices[i];mesh->mNormals[vertexBase+i]=piece.mNormals[i];mesh->mColors[0][vertexBase+i]=colours[k];
            for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)
                if(mesh->mTextureCoords[channel])mesh->mTextureCoords[channel][vertexBase+i]=piece.mTextureCoords[channel][i];
        }
        for(unsigned i=0;i<piece.mNumFaces;++i) {
            auto& f=mesh->mFaces[faceBase+i];f.mNumIndices=3;f.mIndices=new unsigned[3];
            for(unsigned j=0;j<3;++j)f.mIndices[j]=vertexBase+piece.mFaces[i].mIndices[j];
        }
        vertexBase+=piece.mNumVertices;faceBase+=piece.mNumFaces;
    }
    validate(*mesh);return mesh;
}
struct Builder {
    const Options& options; QString rootDirectory;
    ImportResult result;
    std::vector<std::unique_ptr<aiMesh>> meshes;
    // Canonical decoded geometry is independent of material/flat-normal variants.
    std::vector<std::unique_ptr<aiMesh>> geometries;
    std::vector<std::unique_ptr<aiMaterial>> materials;
    std::vector<std::unique_ptr<aiTexture>> textures;
    std::map<QByteArray,unsigned> texturePixels;
    struct ImageResult {int index=-1;bool alpha=false;QString error;double scaleS=1,scaleT=1,offsetS=0,offsetT=0;int format=0;bool composed=false,postLit=false;QImage mipPixels;};
    using TextureSourceKey=std::tuple<const void*,bool,bool,bool,int,bool,std::array<float,4>,size_t>;
    std::map<TextureSourceKey,ImageResult> textureSources;
    std::map<std::vector<float>,ImageResult> textureCompositions;
    std::vector<std::vector<int>> geometryGroups;
    std::map<std::tuple<unsigned,std::vector<int>,unsigned>,unsigned> groupMeshCache;
    // Merged face-group meshes: geometry, the merged face groups, and every batch's diffuse colour plus the shared material fields.
    std::map<std::tuple<unsigned,std::vector<int>,std::vector<float>>,unsigned> mergedGroupCache;
    std::set<const void*> active;
    std::set<QString> activeFiles;
    std::map<QString,Handle(JtNode_Partition)> partitions;
    std::map<std::pair<const void*,unsigned>,unsigned> meshCache;
    std::map<std::vector<float>,unsigned> materialCache;
    std::map<const void*,unsigned> geometryCache;
    std::vector<const aiNode*> hiddenJournal;
    std::vector<const aiNode*> partNodes; // nodes built from a JT Part; rolled back with the LOD alternatives that created them
    // The style each mesh was added with, parallel to meshes. Meshes that differ only in diffuse colour can later be one mesh.
    struct MeshStyle {Appearance style;bool unifiable;};
    std::vector<MeshStyle> meshStyles;
    unsigned nodes=0;
    void textureWarning(const QString& text) {if(!result.warnings.contains(text))result.warnings.append(text);}
    int embedImage(const QImage& pixels) {
        constexpr uint64_t maxBytes=64u*1024u*1024u;
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
        if(!pixels.save(&buffer,"PNG")||uint64_t(png.size())>maxBytes)throw std::runtime_error("texture PNG encoding failed or exceeded byte limit");
        JtData_Limits::allocation(size_t(png.size()));options.check();
        const auto hash=QCryptographicHash::hash(png,QCryptographicHash::Sha256);const auto found=texturePixels.find(hash);
        if(found!=texturePixels.end())return int(found->second);
        else {
            if(textures.size()>=4096)throw std::runtime_error("texture resource count limit exceeded");
            auto texture=std::make_unique<aiTexture>();texture->mWidth=unsigned(png.size());texture->mHeight=0;
            std::memcpy(texture->achFormatHint,"png",4);const size_t texels=(size_t(png.size())+sizeof(aiTexel)-1)/sizeof(aiTexel);
            JtData_Limits::allocation(texels*sizeof(aiTexel));texture->pcData=new aiTexel[texels]{};
            std::memcpy(texture->pcData,png.constData(),size_t(png.size()));
            const int index=int(textures.size());textures.push_back(std::move(texture));texturePixels.emplace(hash,unsigned(index));return index;
        }
    }
    ImageResult image(const TextureState& state,bool allowAlpha,size_t level=0) {
        const bool sampleS=state.environment[0]!=0&&(state.environment[3]==1||state.environment[3]==5);
        const bool sampleT=state.environment[0]!=0&&(state.environment[4]==1||state.environment[4]==5);
        const bool padS=state.environment[0]==1&&sampleS;
        const bool padT=state.environment[0]==1&&sampleT;
        const TextureSourceKey sourceKey{state.image.get(),allowAlpha,sampleS,sampleT,state.environment[0],
            (sampleS||sampleT)&&state.environment[2]>2,(padS||padT)?state.borderColor:std::array<float,4>{},level};
        if(!state.image.IsNull()) {
            const auto cached=textureSources.find(sourceKey);
            if(cached!=textureSources.end()){if(!cached->second.error.isEmpty())textureWarning(cached->second.error);return cached->second;}
        }
        ImageResult resultImage;
        try {
            options.check();
            const auto& source=*state.image;
            if(source.Images().size()!=1)throw std::runtime_error("only one 2D image is supported");
            const auto& data=source.Images()[0];QImage pixels;bool borderHasAlpha=true;
            int width=data.width,height=data.height;
            if(level>data.extraMips.size())throw std::runtime_error("authored mip index exceeds source chain");
            const bool generateBorderMips=data.extraMips.empty()&&(sampleS||sampleT)&&state.environment[2]>2;
            const bool borderMips=(!data.extraMips.empty()||generateBorderMips)&&(sampleS||sampleT);
            const int ring=(source.Inline()?data.border==1:state.environment[0]==2)?1:0;
            int innerWidth=width-2*ring,innerHeight=height-2*ring;
            if(source.Inline()&&(innerWidth<1||innerHeight<1))throw std::runtime_error("border image has no interior");
            if(borderMips&&source.Inline())TextureBorderImage::qualify(innerWidth,innerHeight,sampleS,sampleT);
            for(size_t i=0;i<level;++i){
                if(innerWidth==1&&innerHeight==1)throw std::runtime_error("authored mip chain extends past its source interior");
                innerWidth=std::max(1,innerWidth/2);innerHeight=std::max(1,innerHeight/2);
            }
            width=innerWidth+2*ring;height=innerHeight+2*ring;
            const auto& payload=level?data.extraMips[level-1]:data.baseMip;
            auto borderColor=state.borderColor;
            constexpr uint64_t maxBytes=64u*1024u*1024u;
            if(source.Inline()) {
                if((padS||padT)&&data.format!=1&&data.format!=2&&data.format!=3&&data.format!=4&&data.format!=10&&data.format!=11&&data.format!=12)
                    throw std::runtime_error("sampled constant border requires a qualified RGB, luminance or alpha format");
                borderHasAlpha=data.format==2||data.format==4||data.format==10||data.format==12;
                // Interpret Border Colour using the same internal components as
                // the image. Alpha-only MODULATE keeps neutral white RGB.
                if(data.format==3||data.format==4)borderColor[1]=borderColor[2]=borderColor[0];
                if(data.format==10)borderColor[0]=borderColor[1]=borderColor[2]=1;
                resultImage.format=int(data.format);
                const bool colorFormat=(data.format>=1&&data.format<=4)||(data.format>=7&&data.format<=12);
                if(data.dataType<1||data.dataType>9||data.dataType==4||!colorFormat
                    ||data.dimensionality!=2||data.depth!=1||data.border<0||data.border>1)
                    throw std::runtime_error("inline texture requires a supported 2D numeric colour format");
                if(data.border==1&&(width<=2||height<=2))throw std::runtime_error("inline border texture has no interior texels");
                const size_t components=(data.format==2||data.format==12)?4:(data.format==1||data.format==11)?3:data.format==4?2:1;
                const size_t componentBytes=(data.dataType==2||data.dataType==7||data.dataType==8)?4:(data.dataType==5||data.dataType==6||data.dataType==9)?2:1;
                const bool signedComponents=data.dataType==1||data.dataType==6||data.dataType==8;
                const size_t stride=(size_t(width)*components*componentBytes+data.alignment-1)&~size_t(data.alignment-1);
                if(uint64_t(width)*height*4>maxBytes||payload.size()!=stride*size_t(height))
                    throw std::runtime_error("inline texture row/image byte count mismatch");
                JtData_Limits::allocation(size_t(width)*height*4);
                pixels=QImage(width,height,QImage::Format_RGBA8888);
                if(pixels.isNull())throw std::bad_alloc();
                for(int y=0;y<height;++y){
                    options.check();auto* dst=pixels.scanLine(y);const auto* row=payload.data()+size_t(height-1-y)*stride;
                    for(int x=0;x<width;++x){
                        const auto* packed=row+size_t(x)*components*componentBytes;auto* out=dst+4*x;
                        unsigned char converted[4];
                        const auto* src=packed;
                        if(componentBytes>1||signedComponents){
                            for(size_t c=0;c<components;++c){
                                const auto* word=packed+componentBytes*c;
                                uint32_t value=0;
                                for(size_t b=0;b<componentBytes;++b)
                                    value|=uint32_t(word[b])<<(8*(source.ImageDataLittleEndian()?b:componentBytes-1-b));
                                if(signedComponents){
                                    const unsigned bits=unsigned(componentBytes*8);
                                    const uint64_t modulus=uint64_t(1)<<bits;
                                    const int64_t component=(uint64_t(value)&(modulus>>1))?int64_t(value)-int64_t(modulus):int64_t(value);
                                    // JT references the OpenGL 2 pixel-transfer profile:
                                    // (2*c+1)/(2^bits-1), followed by display clamping.
                                    const uint64_t numerator=component<0?0:uint64_t(component)*2+1;
                                    const uint64_t denominator=modulus-1;
                                    converted[c]=static_cast<unsigned char>((numerator*255+denominator/2)/denominator);
                                }else if(data.dataType==2||data.dataType==9){
                                    double component;
                                    if(data.dataType==9){
                                        const unsigned exponent=(value>>10)&31u,mantissa=value&1023u;
                                        if(exponent==31)throw std::runtime_error("nonfinite inline texture component");
                                        component=exponent==0?std::ldexp(double(mantissa),-24)
                                            :std::ldexp(double(1024u+mantissa),int(exponent)-25);
                                        if(value&0x8000u)component=-component;
                                    }else {
                                        static_assert(sizeof(float)==sizeof(uint32_t)&&std::numeric_limits<float>::is_iec559);
                                        float single;std::memcpy(&single,&value,sizeof(single));component=single;
                                        if(!std::isfinite(component))throw std::runtime_error("nonfinite inline texture component");
                                    }
                                    const double normalized=std::max(0.0,std::min(1.0,component));
                                    converted[c]=static_cast<unsigned char>(std::floor(normalized*255.0+0.5));
                                }else if(data.dataType==7){
                                    // Widen before multiplication so UINT32_MAX is safe.
                                    converted[c]=static_cast<unsigned char>((uint64_t(value)*255u+2147483647u)/4294967295u);
                                }else {
                                    // Normalize UNORM16 into the existing RGBA8 image path.
                                    converted[c]=static_cast<unsigned char>((value+128u)/257u);
                                }
                            }
                            src=converted;
                        }
                        out[0]=out[1]=out[2]=0;out[3]=255;
                        if(data.format==3||data.format==4){out[0]=out[1]=out[2]=src[0];if(data.format==4)out[3]=src[1];}
                        else if(data.format>=7&&data.format<=9)out[data.format-7]=src[0];
                        // Alpha-only MODULATE preserves incoming RGB; white is
                        // the neutral RGB factor in our canonical RGBA texture.
                        else if(data.format==10){out[0]=out[1]=out[2]=255;out[3]=src[0];}
                        else {const bool bgr=data.format==11||data.format==12;
                            out[0]=src[bgr?2:0];out[1]=src[1];out[2]=src[bgr?0:2];if(components==4)out[3]=src[3];}
                    }
                }
                if(data.border==1){
                    // Retain the ring only on sampled axes. Cropping the other
                    // axis preserves repeat/mirror/edge sampling of its interior.
                    const int innerWidth=width-2,innerHeight=height-2;
                    const int width=innerWidth+2*int(sampleS),height=innerHeight+2*int(sampleT);
                    if(!sampleS||!sampleT){
                        JtData_Limits::allocation(size_t(width)*height*4);
                        pixels=pixels.copy(int(!sampleS),int(!sampleT),width,height);
                        if(pixels.isNull())throw std::bad_alloc();
                    }
                    resultImage.scaleS=double(innerWidth)/width;resultImage.scaleT=double(innerHeight)/height;
                    resultImage.offsetS=double(sampleS)/width;resultImage.offsetT=double(sampleT)/height;
                }
            } else {
                const auto text=[](const TCollection_ExtendedString& value){return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.ToExtString()),value.Length());};
                QString reference=text(data.externalName);reference.replace('\\','/');
                if(reference.isEmpty()||QDir::isAbsolutePath(reference)||reference.contains(":")||reference.contains(QChar('\0')))
                    throw std::runtime_error("external image must be a local relative path");
                const QString path=QFileInfo(QFileInfo(text(source.OriginFileName())).dir().filePath(reference)).canonicalFilePath();
                if(path.isEmpty()||!insideDirectory(path,rootDirectory))throw std::runtime_error("missing or outside-assembly image");
                QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()<1||uint64_t(file.size())>maxBytes)
                    throw std::runtime_error("external image cannot be opened within the byte limit");
                JtData_Limits::allocation(size_t(file.size()));const auto encoded=file.read(qint64(maxBytes)+1);
                if(encoded.size()!=file.size())throw std::runtime_error("external image changed or exceeded byte limit");
                QBuffer buffer;buffer.setData(encoded);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);
                if(reader.format()!="png"&&reader.format()!="jpeg")throw std::runtime_error("external image format is not PNG/JPEG");
                const auto size=reader.size();
                if(size.width()<1||size.height()<1||uint64_t(size.width())*size.height()*4>maxBytes)
                    throw std::runtime_error("external image dimensions exceed the decoded byte limit");
                JtData_Limits::allocation(size_t(size.width())*size.height()*8);
                const auto decoded=reader.read();
                borderHasAlpha=decoded.hasAlphaChannel();resultImage.format=borderHasAlpha?2:1;
                pixels=decoded.convertToFormat(QImage::Format_RGBA8888);
                if(pixels.isNull()||pixels.size()!=size)throw std::runtime_error("external image decoding failed");
                if(!pixels.text(QLatin1String(TextureMipChain::Key)).isEmpty()||!pixels.text(QLatin1String(TextureMipChain::TerminalKey)).isEmpty())throw std::runtime_error("external PNG authored mip carrier is not qualified for JT image import");
                innerWidth=pixels.width()-2*ring;innerHeight=pixels.height()-2*ring;
                if(innerWidth<1||innerHeight<1)throw std::runtime_error("external border image has no interior");
                if(borderMips)TextureBorderImage::qualify(innerWidth,innerHeight,sampleS,sampleT);
                if(ring){
                    const int croppedW=innerWidth+2*int(sampleS),croppedH=innerHeight+2*int(sampleT);
                    if(!sampleS||!sampleT){
                        JtData_Limits::allocation(size_t(croppedW)*croppedH*4);
                        pixels=pixels.copy(int(!sampleS),int(!sampleT),croppedW,croppedH);
                        if(pixels.isNull())throw std::bad_alloc();
                    }
                    resultImage.scaleS=double(innerWidth)/croppedW;resultImage.scaleT=double(innerHeight)/croppedH;
                    resultImage.offsetS=double(sampleS)/croppedW;resultImage.offsetT=double(sampleT)/croppedH;
                }
            }
            if(padS||padT){
                // A constant one-texel halo plus interior-relative UVs reproduces
                // base-level nearest/linear CLAMP_TO_BORDER via clamp-to-edge.
                // Authored border chains are expanded below to maintain a
                // level-invariant interior mapping; base-only paths are unchanged.
                const int width=pixels.width()+2*int(padS),height=pixels.height()+2*int(padT);
                if(uint64_t(width)*height*4>maxBytes)throw std::runtime_error("padded border texture exceeds image byte limit");
                JtData_Limits::allocation(size_t(width)*height*4);
                QImage padded(width,height,QImage::Format_RGBA8888);if(padded.isNull())throw std::bad_alloc();
                const auto byte=[](float v){return int(std::floor(std::max(0.0,std::min(1.0,double(v)))*255+0.5));};
                // RGB/luminance images have no alpha component, including their
                // constant border. Do not introduce transparency from Border A.
                padded.fill(QColor(byte(borderColor[0]),byte(borderColor[1]),byte(borderColor[2]),borderHasAlpha?byte(borderColor[3]):255));
                for(int y=0;y<pixels.height();++y){options.check();std::memcpy(padded.scanLine(y+int(padT))+4*int(padS),pixels.constScanLine(y),size_t(pixels.width())*4);}
                resultImage.scaleS=double(pixels.width())/width;resultImage.scaleT=double(pixels.height())/height;
                resultImage.offsetS=double(padS)/width;resultImage.offsetT=double(padT)/height;
                pixels=std::move(padded);
            }
            const auto terminal=sampleS?(sampleT?TextureMipChain::BorderTerminal:TextureMipChain::BorderSTerminal):TextureMipChain::BorderTTerminal;
            if(borderMips){
                int w=(sampleS?3:1)*innerWidth,h=(sampleT?3:1)*innerHeight;uint64_t bytes=0;
                for(;;){
                    bytes+=uint64_t(w)*h*4;
                    if(bytes>maxBytes)throw std::runtime_error("border mip chain exceeds decoded byte limit");
                    if(w==(sampleS?3:1)&&h==(sampleT?3:1))break;
                    w=std::max(1,w/2);h=std::max(1,h/2);
                }
            }
            const auto poll=[&]{options.check();};
            const auto applyAlpha=[&](QImage& image){
                for(int y=0;y<image.height();++y){options.check();auto* row=image.scanLine(y);
                    for(int x=0;x<image.width();++x){
                        if((x&1023)==0)options.check();
                        if(!allowAlpha)row[4*x+3]=255;else resultImage.alpha=resultImage.alpha||row[4*x+3]<255;
                    }
                }
            };
            std::vector<QImage> generated;
            if(generateBorderMips){
                QImage sourceLevel=pixels;int w=innerWidth,h=innerHeight;
                while(w>1||h>1){
                    sourceLevel=TextureBorderImage::reduce(sourceLevel,w,h,sampleS,sampleT,poll);
                    w=std::max(1,w/2);h=std::max(1,h/2);
                    auto display=TextureBorderImage::expand(sourceLevel,w,h,sampleS,sampleT,poll);
                    applyAlpha(display);generated.push_back(std::move(display));
                }
            }
            if(borderMips){
                pixels=TextureBorderImage::expand(pixels,innerWidth,innerHeight,sampleS,sampleT,poll);
                resultImage.scaleS=sampleS?1.0/3:1;resultImage.scaleT=sampleT?1.0/3:1;
                resultImage.offsetS=sampleS?1.0/3:0;resultImage.offsetT=sampleT?1.0/3:0;
            }
            applyAlpha(pixels);
            if(generateBorderMips){
                pixels.setText(QLatin1String(TextureMipChain::TerminalKey),QLatin1String(terminal));
                TextureMipChain::attach(pixels,generated);
            }
            if(level==0&&source.Inline()&&!data.extraMips.empty()){
                std::vector<QImage> mips;mips.reserve(data.extraMips.size());
                for(size_t n=1;n<=data.extraMips.size();++n){
                    const auto decoded=image(state,allowAlpha,n);
                    if(decoded.mipPixels.isNull())throw std::runtime_error("authored mip could not be decoded");
                    resultImage.alpha=resultImage.alpha||decoded.alpha;mips.push_back(decoded.mipPixels);
                }
                if(borderMips)pixels.setText(QLatin1String(TextureMipChain::TerminalKey),QLatin1String(terminal));
                TextureMipChain::validate(pixels,mips);
                JtData_Limits::allocation(size_t(data.totalBytes)*8);
                TextureMipChain::attach(pixels,mips);
            }
            if(level==0)resultImage.index=embedImage(pixels);
            else resultImage.mipPixels=std::move(pixels);
        }catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
        catch(const std::exception& e){resultImage.error="JT texture not applied: "+QString::fromUtf8(e.what());textureWarning(resultImage.error);}
        textureSources[sourceKey]=resultImage;return resultImage;
    }
    ImageResult textureStage(const TextureState& state,bool allowAlpha) {
        if(state.type==0||state.image.IsNull())return {};
        const auto& env=state.environment;
        const bool ignoresBorder=env[3]>=2&&env[3]<=4&&env[4]>=2&&env[4]<=4;
        const bool legacyClamp=(env[0]==1||env[0]==2)&&(env[3]==1||env[4]==1);
        if(legacyClamp&&(env[2]>2||env[1]!=env[2])){
            textureWarning("JT texture not applied: legacy CLAMP requires equal non-mip minification/magnification filters");return {};
        }
        const bool sampledBorder=(env[0]==1||env[0]==2)&&(env[3]==1||env[3]==5||env[4]==1||env[4]==5)&&env[3]>=1&&env[3]<=5&&env[4]>=1&&env[4]<=5&&env[1]>=1&&env[1]<=2&&env[2]>=1&&env[2]<=6;
        if(!state.colorRole()||(env[0]!=0&&!ignoresBorder&&!sampledBorder)||(env[6]<1||env[6]>5)||!state.finiteTransform()||state.coordinate<0||state.coordinate>=4
            ||!state.modelLinearComponents()
            ||(env[3]==5&&!sampledBorder)||(env[4]==5&&!sampledBorder)
            ||env[3]<1||env[3]>5||env[4]<1||env[4]>5) {
            textureWarning("JT texture not applied: mapping requires a qualified 2D colour role/combiner, UV0-3 with stored or finite unit model-linear components and qualified colour borders; view-dependent generation is unsupported");return {};
        }
        if(state.image->Inline()&&state.image->Images().size()==1
            &&state.image->Images()[0].border!=(env[0]==2?1:0)){
            textureWarning("JT texture not applied: border mode and image descriptor disagree");return {};
        }
        return image(state,allowAlpha);
    }
    ImageResult texture(const Appearance& style,const aiColor4D* incoming=nullptr) {
        if(style.texture.type==0||style.texture.image.IsNull())return {};
        // Constant vertex RGB can be composed exactly like a material factor,
        // without replacing the original source material retained as metadata.
        const auto& input=incoming?*incoming:style.diffuse;
        std::vector<const TextureState*> stages;
        if(style.textureUnits.empty())stages.push_back(&style.texture);
        else for(const auto& unit:style.textureUnits){
            const auto& state=unit.second;
            if(state.colorRole()&&state.environment[6]!=0)stages.push_back(&state);
            else if(state.type!=0&&!state.automatic&&!state.colorRole()){
                textureWarning("JT texture stack not applied: an explicit image role is unsupported");return {};
            }
        }
        if(stages.empty())return {};
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        const bool allowAlpha=(style.blendFlags&0x10)&&!(src==1&&dst==0);
        if(std::any_of(stages.begin(),stages.end(),[](const TextureState* s){return s->type==2;})){
            if(stages.size()!=1){textureWarning("JT post-lit colour stack not applied: only a single complete RGB replacement is qualified");return {};}
            const auto& stage=*stages[0];auto decoded=textureStage(stage,allowAlpha);
            const bool rgb=decoded.format==1||decoded.format==11,rgba=decoded.format==2||decoded.format==12;
            if(decoded.index<0)return {};
            if(!((stage.environment[6]==3&&(rgb||rgba))||(stage.environment[6]==1&&rgb))){
                textureWarning("JT post-lit texture not applied: only RGB/RGBA Replace or RGB Decal is a complete unlit colour replacement");return {};
            }
            decoded.postLit=true;return decoded;
        }
        if(stages.size()==1&&(stages[0]->environment[6]==2||stages[0]->replaceRGB()))return textureStage(*stages[0],allowAlpha);
        ImageResult output;
        try {
            const auto& primary=*stages[0];std::vector<QImage> images;std::vector<int> formats;std::vector<ImageResult> decodedStages;
            JtData_Limits::allocation(stages.size()*(128+sizeof(ImageResult)+sizeof(QImage))+512);
            images.reserve(stages.size());formats.reserve(stages.size());decodedStages.reserve(stages.size());
            std::vector<float> key{input.r,input.g,input.b,input.a,float(allowAlpha)};
            key.reserve(5+7*stages.size());
            if(!std::isfinite(input.a)||input.a<0||input.a>1)throw std::runtime_error("invalid incoming composition alpha");
            for(const auto* stage:stages){
                const auto mode=stage->environment[6];
                if(stage->image.IsNull())throw std::runtime_error("texture stack has a missing image");
                const bool authored=stage->image->Inline()&&stage->image->Images().size()==1&&!stage->image->Images()[0].extraMips.empty();
                const bool generatedBorder=!authored&&stage->environment[0]!=0
                    &&(stage->environment[3]==5||stage->environment[4]==5)&&stage->environment[2]>2;
                const bool rgbDecal=mode==1&&stage->image->Inline()&&stage->image->Images().size()==1
                    &&(stage->image->Images()[0].format==1||stage->image->Images()[0].format==11);
                // With constant incoming material, BLEND and RGB DECAL are
                // affine in the sampled texel. They commute with spatial/mip
                // filtering. RGBA DECAL, saturated ADD and multi-unit products
                // do not generally commute and retain the nearest-only gate.
                const bool affineSingle=stages.size()==1&&(mode==4||rgbDecal);
                if(!affineSingle&&(stages.size()>1||mode==1||mode==4||mode==5)&&
                    (stage->environment[1]!=1||(stage->environment[2]!=1&&!((authored||generatedBorder)&&stage->environment[2]==3))))
                    throw std::runtime_error("composition requires nearest sampling and matching authored levels for mip selection");
                if(stage->environment[1]!=primary.environment[1]||stage->environment[2]!=primary.environment[2])
                    throw std::runtime_error("texture stack uses independent filters");
                if(stage->coordinate!=primary.coordinate||stage->transform!=primary.transform||stage->generation!=primary.generation||stage->planes!=primary.planes
                   ||stage->environment[3]!=primary.environment[3]||stage->environment[4]!=primary.environment[4])
                    throw std::runtime_error("texture stack uses independent coordinate mappings or wrapping");
                const auto decoded=textureStage(*stage,true);if(decoded.index<0)return {};
                const int format=decoded.format;
                if(format!=1&&format!=2&&format!=3&&format!=4&&format!=10&&format!=11&&format!=12)
                    throw std::runtime_error("combiner requires a qualified internal colour format");
                if(mode==1&&format!=1&&format!=2&&format!=11&&format!=12)
                    throw std::runtime_error("Decal is undefined for this internal format");
                if(decodedStages.empty())output=decoded;
                else if(decoded.scaleS!=output.scaleS||decoded.scaleT!=output.scaleT||decoded.offsetS!=output.offsetS||decoded.offsetT!=output.offsetT)
                    throw std::runtime_error("texture stack uses different border footprints");
                key.push_back(float(decoded.index));key.push_back(float(format));key.push_back(float(mode));
                for(float c:stage->blendColor)key.push_back(c);
                formats.push_back(format);
                decodedStages.push_back(decoded);
            }
            for(float v:key)if(!std::isfinite(v))throw std::runtime_error("nonfinite texture composition input");
            const auto cached=textureCompositions.find(key);
            if(cached!=textureCompositions.end()){
                auto reuse=cached->second;reuse.scaleS=output.scaleS;reuse.scaleT=output.scaleT;reuse.offsetS=output.offsetS;reuse.offsetT=output.offsetT;return reuse;
            }
            for(const auto& decoded:decodedStages){
                const auto* encoded=textures[size_t(decoded.index)].get();
                QByteArray bytes=QByteArray::fromRawData(reinterpret_cast<const char*>(encoded->pcData),int(encoded->mWidth));
                QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");const auto size=reader.size();
                if(size.width()<1||size.height()<1||uint64_t(size.width())*size.height()*4>64u*1024u*1024u)throw std::runtime_error("composed texture image exceeds limit");
                JtData_Limits::allocation(size_t(size.width())*size.height()*8);
                auto pixels=reader.read().convertToFormat(QImage::Format_RGBA8888);
                if(pixels.isNull()||pixels.size()!=size)throw std::runtime_error("composed texture decoding failed");
                images.push_back(std::move(pixels));
            }
            const auto clamp=[](double v){return std::max(0.0,std::min(1.0,v));};
            // Material factors are linear; base-colour PNG bytes are sRGB in
            // the existing raster/path-tracer pipeline. Compose in that linear
            // colour space, then encode once. Alpha is always linear.
            const auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
            const auto encoded=[](double v){return v<=.0031308?12.92*v:1.055*std::pow(v,1.0/2.4)-.055;};
            std::vector<std::vector<QImage>> tails;tails.reserve(images.size());
            for(const auto& pixels:images)tails.push_back(TextureMipChain::decode(pixels));
            for(size_t n=1;n<tails.size();++n)
                if(tails[n].size()!=tails[0].size()||images[n].text(QLatin1String(TextureMipChain::TerminalKey))!=images[0].text(QLatin1String(TextureMipChain::TerminalKey)))
                    throw std::runtime_error("texture stack uses different authored mip chains or terminal profiles");
            const auto terminal=images[0].text(QLatin1String(TextureMipChain::TerminalKey));
            QSize grid=images[0].size();
            for(const auto& image:images)if(image.size()!=grid){
                // Nearest source texel boundaries coincide on the least common
                // multiple grid. This is exact for common UVs/wraps, including
                // non-dividing resolutions; no bilinear or LOD approximation.
                if(primary.environment[1]!=1||primary.environment[2]!=1||!tails[0].empty()||!terminal.isEmpty())
                    throw std::runtime_error("different image resolutions require nearest base-level sampling");
                const auto lcm=[](int a,int b){return uint64_t(a/std::gcd(a,b))*uint64_t(b);};
                const uint64_t width=lcm(grid.width(),image.width()),height=lcm(grid.height(),image.height());
                constexpr uint64_t maxPixels=64u*1024u*1024u/4;
                if(width>maxPixels||height>maxPixels||width*height>maxPixels)
                    throw std::runtime_error("common nearest texture grid exceeds image byte limit");
                grid=QSize(int(width),int(height));
            }
            output.alpha=false;
            auto composeLevel=[&]() {
                JtData_Limits::allocation(size_t(grid.width())*grid.height()*4);
                QImage pixels(grid,QImage::Format_RGBA8888);if(pixels.isNull())throw std::bad_alloc();
                for(int y=0;y<pixels.height();++y){options.check();auto* row=pixels.scanLine(y);
                    for(int x=0;x<pixels.width();++x){
                        if((x&1023)==0)options.check();
                        double rgb[3]={clamp(input.r),clamp(input.g),clamp(input.b)},alpha=input.a;
                        for(size_t n=0;n<stages.size();++n){
                            const int sx=int((uint64_t(2)*x+1)*images[n].width()/(uint64_t(2)*pixels.width()));
                            const int sy=int((uint64_t(2)*y+1)*images[n].height()/(uint64_t(2)*pixels.height()));
                            const auto* texel=images[n].constScanLine(sy)+4*sx;const double a=double(texel[3])/255;
                            const bool hasRGB=formats[n]!=10,hasAlpha=formats[n]==2||formats[n]==4||formats[n]==10||formats[n]==12;
                            const auto mode=stages[n]->environment[6];
                            if(hasRGB)for(unsigned c=0;c<3;++c){const double t=linear(double(texel[c])/255);
                                if(mode==1)rgb[c]=hasAlpha?rgb[c]*(1-a)+t*a:t;
                                if(mode==2)rgb[c]*=t;
                                if(mode==3)rgb[c]=t;
                                if(mode==4)rgb[c]=rgb[c]*(1-t)+clamp(stages[n]->blendColor[c])*t;
                                if(mode==5)rgb[c]=clamp(rgb[c]+t);
                            }
                            if(hasAlpha){if(mode==3)alpha=a;else if(mode!=1)alpha*=a;}
                        }
                        for(unsigned c=0;c<3;++c)row[4*x+c]=static_cast<uchar>(std::floor(clamp(encoded(clamp(rgb[c])))*255+.5));
                        row[4*x+3]=allowAlpha?static_cast<uchar>(std::floor(clamp(alpha)*255+.5)):255;
                        output.alpha=output.alpha||row[4*x+3]<255;
                    }
                }
                return pixels;
            };
            auto pixels=composeLevel();std::vector<QImage> composedMips;
            for(size_t level=0;level<tails[0].size();++level){
                options.check();
                for(size_t n=0;n<images.size();++n){
                    images[n]=tails[n][level];
                    if(images[n].size()!=images[0].size())throw std::runtime_error("texture mip stack uses different level resolutions");
                }
                grid=images[0].size();
                composedMips.push_back(composeLevel());
            }
            if(!terminal.isEmpty())pixels.setText(QLatin1String(TextureMipChain::TerminalKey),terminal);
            if(!composedMips.empty())TextureMipChain::attach(pixels,composedMips);
            output.index=embedImage(pixels);output.composed=true;output.format=2;textureCompositions.emplace(std::move(key),output);return output;
        }catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
        catch(const std::exception& e){textureWarning("JT texture stack not applied: "+QString::fromUtf8(e.what()));return {};}
    }
    const TextureState* normalState(const Appearance& style){
        const TextureState* normal=nullptr;
        for(const auto& unit:style.textureUnits)if(unit.second.type==4){
            if(normal){textureWarning("JT normal maps not applied: multiple tangent-space normal roles are not qualified");return nullptr;}
            normal=&unit.second;
        }
        return normal;
    }
    ImageResult normalImage(const Appearance& style,const ImageResult& color){
        const auto* normal=normalState(style);if(!normal)return {};
        if(color.postLit){textureWarning("JT normal map retained but not applied to a post-lit unlit replacement");return {};}
        const auto& state=*normal;const auto& env=state.environment;
        bool identity=true;for(unsigned i=0;i<16;++i)identity=identity&&state.transform[i]==float(i%5==0);
        if(state.image.IsNull()||state.coordinate!=0||!identity
            ||std::any_of(state.generation.begin(),state.generation.end(),[](int mode){return mode!=0;})
            ||env[0]!=0||env[1]<1||env[1]>2||env[2]<1||env[2]>6
            ||env[3]<2||env[3]>4||env[4]<2||env[4]>4||env[6]!=2){
            textureWarning("JT normal map not applied: requires one normalized RGB tangent map, original UV0, identity mapping and no sampled border");return {};
        }
        if(state.image->Inline()){
            if(state.image->Images().size()!=1){textureWarning("JT normal map not applied: requires exactly one image");return {};}
            const auto& data=state.image->Images()[0];
            if((data.format!=1&&data.format!=2&&data.format!=11&&data.format!=12)||data.dataType!=3||data.border!=0){
                textureWarning("JT normal map not applied: only unsigned-byte RGB/BGR/RGBA/BGRA components without an image ring are qualified");return {};
            }
        }
        if(color.index>=0){
            const auto& primary=style.texture;bool changed=false;
            for(unsigned i=0;i<16;++i)changed=changed||primary.transform[i]!=float(i%5==0);
            changed=changed||std::any_of(primary.generation.begin(),primary.generation.end(),[](int mode){return mode!=0;})
                ||color.scaleS!=1||color.scaleT!=1||color.offsetS!=0||color.offsetT!=0;
            const bool refined=primary.environment[0]!=0&&(primary.environment[3]==1||primary.environment[4]==1);
            if((primary.coordinate==0&&changed)||refined){
                textureWarning("JT normal map not applied: colour UV rewriting would change the original normal-map basis");return {};
            }
        }
        // Normal RGB is vector data. Do not compose material/vertex colour into
        // it; unused image alpha must never select the transparent draw pass.
        // The existing image path validates and carries each authored mip as
        // vector bytes, without colour composition or gamma conversion.
        return image(state,false);
    }
    std::unique_ptr<aiMetadata> properties(const JtNode_Base& source,const QString& label) {
        std::vector<std::unique_ptr<aiMetadata>> records;
        size_t unsupported=0,unrepresentable=0;
        for(const auto& property:source.Properties()) {
            options.check();
            const auto key=Handle(JtProperty_String)::DownCast(property.key);
            const auto text=Handle(JtProperty_String)::DownCast(property.value);
            const auto integer=Handle(JtProperty_Integer)::DownCast(property.value);
            const auto real=Handle(JtProperty_Float)::DownCast(property.value);
            if(key.IsNull() || (text.IsNull() && integer.IsNull() && real.IsNull())) {++unsupported;continue;}
            const auto utf8=[](const TCollection_ExtendedString& value) {
                return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.ToExtString()),value.Length()).toUtf8();
            };
            const auto k=utf8(key->Value());const auto v=text.IsNull() ? QByteArray() : utf8(text->Value());
            // aiString has a fixed UTF-8 byte limit. Do not silently truncate
            // source keys/values or emit nonfinite numbers as valid metadata.
            if(k.size()>=AI_MAXLEN || v.size()>=AI_MAXLEN || k.contains('\0') || v.contains('\0') || (!real.IsNull() && !std::isfinite(real->Value()))) {
                ++unrepresentable;continue;
            }
            constexpr unsigned fields=6;
            JtData_Limits::allocation(3*(sizeof(aiMetadata)+fields*(sizeof(aiString)+sizeof(aiMetadataEntry))+2*sizeof(aiString)));
            auto record=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(fields));
            record->Set(0,"Key",aiString(k.constData()));
            if(!text.IsNull()) record->Set(1,"Value",aiString(v.constData()));
            else if(!integer.IsNull()) record->Set(1,"Value",int32_t(integer->Value()));
            else record->Set(1,"Value",float(real->Value()));
            record->Set(2,"KeyAtomId",int32_t(property.keyId));
            record->Set(3,"ValueAtomId",int32_t(property.valueId));
            record->Set(4,"KeyStateFlags",uint64_t(key->StateFlags()));
            const auto atom=Handle(JtProperty_Base)::DownCast(property.value);
            record->Set(5,"ValueStateFlags",uint64_t(atom->StateFlags()));
            records.push_back(std::move(record));
        }
        if(unsupported) result.warnings.append(label+": skipped "+QString::number(unsupported)+" unsupported or unresolved JT metadata pair(s)");
        if(unrepresentable) result.warnings.append(label+": skipped "+QString::number(unrepresentable)+" JT metadata pair(s) exceeding Assimp string/numeric limits");
        if(records.empty()) return {};
        auto output=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(unsigned(records.size())));
        for(unsigned i=0;i<records.size();++i) output->Set(i,std::to_string(i),*records[i]);
        return output;
    }
    std::unique_ptr<aiMetadata> textureDescriptors(const JtNode_Base& source,const QString& label) {
        std::vector<std::unique_ptr<aiMetadata>> records;
        const auto utf8=[](const TCollection_ExtendedString& value){return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.ToExtString()),value.Length()).toUtf8();};
        const auto numeric=[](const auto& values){
            auto record=std::unique_ptr<aiMetadata>(values.empty()?new aiMetadata:aiMetadata::Alloc(unsigned(values.size())));
            for(unsigned i=0;i<values.size();++i)record->Set(i,std::to_string(i),values[i]);
            return record;
        };
        for(size_t a=0;a<source.Attributes().Count();++a){
            const auto texture=Handle(JtAttribute_TextureImage)::DownCast(source.Attributes()[a]);
            if(texture.IsNull())continue;
            options.check();if(records.size()>=1024)throw std::runtime_error("JT texture descriptor limit exceeded");
            JtData_Limits::allocation(256*1024);
            auto record=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(texture->Parsed()?17:6));
            record->Set(0,"AttributeIndex",uint64_t(a));record->Set(1,"Parsed",texture->Parsed());
            record->Set(2,"Ignored",texture->AccumulationIgnored());
            record->Set(3,"FieldInhibit",uint64_t(texture->FieldInhibitFlags()));record->Set(4,"FieldFinal",uint64_t(texture->FieldFinalFlags()));
            record->Set(texture->Parsed()?16:5,"LocalVersion",int32_t(texture->LocalVersion()));
            if(texture->Parsed()){
                record->Set(5,"TextureType",int32_t(texture->TextureType()));record->Set(6,"Channel",int32_t(texture->Channel()));
                record->Set(7,"CoordinateChannel",int32_t(texture->CoordinateChannel()));record->Set(8,"Inline",texture->Inline());
                record->Set(9,"PaletteIndex",int32_t(texture->PaletteIndex()));
                auto environment=numeric(texture->Environment()),matrix=numeric(texture->Transform()),colors=numeric(texture->Colors()),generation=numeric(texture->Generation()),planes=numeric(texture->Planes());
                record->Set(10,"Environment",*environment);record->Set(11,"Transform",*matrix);record->Set(12,"Colors",*colors);record->Set(13,"Generation",*generation);record->Set(14,"ReferencePlanes",*planes);
                auto images=std::unique_ptr<aiMetadata>(texture->Images().empty()?new aiMetadata:aiMetadata::Alloc(unsigned(texture->Images().size())));
                for(unsigned i=0;i<texture->Images().size();++i){
                    options.check();JtData_Limits::allocation(128*1024);const auto& image=texture->Images()[i];
                    auto data=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(13));
                    const auto externalName=utf8(image.externalName);const bool omitted=externalName.size()>=AI_MAXLEN;
                    data->Set(0,"ExternalName",aiString(omitted?"":externalName.constData()));
                    data->Set(12,"ExternalNameOmitted",omitted);
                    if(omitted)result.warnings.append(label+": JT texture external name exceeds metadata string limit");
                    data->Set(1,"PixelFormat",uint64_t(image.format));data->Set(2,"DataType",uint64_t(image.dataType));
                    data->Set(3,"Dimensions",int32_t(image.dimensionality));data->Set(4,"RowAlignment",int32_t(image.alignment));
                    data->Set(5,"Width",int32_t(image.width));data->Set(6,"Height",int32_t(image.height));data->Set(7,"Depth",int32_t(image.depth));
                    data->Set(8,"Border",int32_t(image.border));data->Set(9,"Shared",uint64_t(image.shared));data->Set(10,"TotalBytes",int32_t(image.totalBytes));
                    auto mips=numeric(image.mipBytes);data->Set(11,"MipByteCounts",*mips);images->Set(i,std::to_string(i),*data);
                }
                record->Set(15,"Images",*images);
            }
            if(!texture->AccumulationIgnored()&&!texture->Parsed())
                textureWarning(label+(texture->Legacy9()?": JT 9.x texture local version "+QString::number(texture->LocalVersion())+" retained as unparsed metadata; unknown texture layout"
                    :": JT texture descriptor retained as metadata; legacy texture layout is not yet qualified"));
            records.push_back(std::move(record));
        }
        if(records.empty())return {};
        auto output=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(unsigned(records.size())));
        for(unsigned i=0;i<records.size();++i)output->Set(i,std::to_string(i),*records[i]);return output;
    }
    struct Checkpoint {
        size_t meshes,materials,shapes,hidden,geometries;
        qsizetype warnings;
        unsigned stored,exact,fallback;
        size_t parts;
    };
    Checkpoint checkpoint() const {return {meshes.size(),materials.size(),result.exactShapes.size(),hiddenJournal.size(),geometries.size(),result.warnings.size(),result.storedMeshes,result.exactParts,result.fallbackParts,partNodes.size()};}
    void rollback(const Checkpoint& c) {
        partNodes.resize(c.parts);
        meshStyles.resize(c.meshes);
        meshes.resize(c.meshes);materials.resize(c.materials);result.exactShapes.resize(c.shapes);
        geometries.resize(c.geometries);geometryGroups.resize(c.geometries);
        for(auto it=groupMeshCache.begin();it!=groupMeshCache.end();) {if(it->second>=c.meshes)it=groupMeshCache.erase(it);else ++it;}
        for(auto it=mergedGroupCache.begin();it!=mergedGroupCache.end();) {if(it->second>=c.meshes)it=mergedGroupCache.erase(it);else ++it;}
        while(result.warnings.size()>c.warnings) result.warnings.removeLast();
        while(hiddenJournal.size()>c.hidden) {result.hiddenNodes.remove(hiddenJournal.back());hiddenJournal.pop_back();}
        for(auto it=meshCache.begin();it!=meshCache.end();) {if(it->second>=c.meshes) it=meshCache.erase(it);else ++it;}
        for(auto it=geometryCache.begin();it!=geometryCache.end();) {if(it->second>=c.geometries) it=geometryCache.erase(it);else ++it;}
        for(auto it=materialCache.begin();it!=materialCache.end();) {if(it->second>=c.materials) it=materialCache.erase(it);else ++it;}
        result.storedMeshes=c.stored;result.exactParts=c.exact;result.fallbackParts=c.fallback;
    }
    unsigned material(const Appearance& style,bool vertexColors=false,bool vertexAlpha=false,const aiColor4D* textureInput=nullptr) {
        if(!std::isfinite(style.diffuse.r)||!std::isfinite(style.diffuse.g)||!std::isfinite(style.diffuse.b)
            ||!std::isfinite(style.diffuse.a)||style.diffuse.a<0||style.diffuse.a>1)
            throw std::runtime_error("JT incoming diffuse colour/alpha is invalid");
        const auto mappedTexture=texture(style,textureInput);
        const auto mappedNormal=normalImage(style,mappedTexture);
        const bool replace=mappedTexture.index>=0&&(mappedTexture.postLit||style.texture.replaceRGB());
        const auto format=replace?mappedTexture.format:0;
        const bool replaceAlpha=replace&&(format==2||format==12)&&style.texture.environment[6]==3;
        // REPLACE discards incoming RGB; RGBA also discards incoming alpha.
        // White factors let the existing modulating renderer/export path carry
        // this qualified single-unit result without baking away source pixels.
        const auto c=(vertexColors||mappedTexture.composed) ? aiColor4D(1,1,1,1) : replace?aiColor4D(1,1,1,replaceAlpha?1:style.diffuse.a):style.diffuse;
        const bool blending=(style.blendFlags&0x10)!=0;
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        const float opacity=(!blending || (src==1 && dst==0)) ? 1.0f : c.a;
        std::vector<float> key{c.r,c.g,c.b,opacity,style.ambient.r,style.ambient.g,style.ambient.b,
            style.specular.r,style.specular.g,style.specular.b,style.emission.r,style.emission.g,style.emission.b,
            style.bumpiness,style.reflectivity,style.shininess,float(style.hasShininess),float(style.overrideColors),float(style.present),float(style.drawFlags),float(style.drawPresent),float(style.blendFlags),float(vertexAlpha),float(mappedTexture.index),float(style.texture.coordinate),float(mappedTexture.postLit),float(mappedNormal.index)};
        if(mappedNormal.index>=0){
            const auto* normal=normalState(style);
            for(auto value:normal->environment)key.push_back(float(value));
        }
        if(mappedTexture.composed||replace){
            key.insert(key.end(),{style.diffuse.r,style.diffuse.g,style.diffuse.b,style.diffuse.a,float(mappedTexture.composed)});
        }
        if(mappedTexture.index>=0){
            // Mesh caches consult material(style) before examining vertex RGB.
            // Different programs can coincide for that material input, yet
            // differ for the actual vertex input (e.g. white Modulate vs Add).
            // Preserve the ordered source program, not only its baked pixels.
            const auto programKey=[&](const TextureState& stage){
                if(!stage.colorRole()||stage.environment[6]==0)return;
                const auto decoded=textureStage(stage,mappedTexture.composed||(blending&&!(src==1&&dst==0)));
                key.insert(key.end(),{float(stage.environment[6]),float(decoded.index),float(decoded.format)});
                for(float value:stage.blendColor)key.push_back(value);
            };
            if(style.textureUnits.empty())programKey(style.texture);
            else for(const auto& unit:style.textureUnits)programKey(unit.second);
            for(auto value:style.texture.environment)key.push_back(float(value));
            if(style.texture.environment[0]==1&&(style.texture.environment[3]==5||style.texture.environment[4]==5))
                for(auto value:style.texture.borderColor)key.push_back(value);
            // Mesh variants are keyed by material ID. Keep different effective
            // transforms distinct even when the image and source mesh are shared.
            for(auto value:style.texture.transform)key.push_back(value);
            for(auto value:style.texture.generation)key.push_back(float(value));
            for(auto value:style.texture.planes)key.push_back(value);
        }
        for(float value:key) if(!std::isfinite(value)) throw std::runtime_error("JT has nonfinite material values");
        if(!std::isfinite(c.a) || c.a<0 || c.a>1 || opacity<0 || opacity>1) throw std::runtime_error("JT diffuse alpha is out of range");
        // JT 10.0 section 6.1.2.2 defines a Phong exponent in [1,128].
        // The viewer's material adapter converts this to PBR roughness.
        if(style.hasShininess && (style.shininess<(style.legacyShininess?0:1) || (!style.legacyShininess && style.shininess>128)))
            throw std::runtime_error(style.legacyShininess?"Legacy JT shininess is negative":"JT shininess is out of range [1,128]");
        const auto found=materialCache.find(key);if(found!=materialCache.end()) return found->second;
        if(blending && !(src==6 && dst==7) && !(src==1 && dst==0))
            result.warnings.append("JT blend factors are approximated using conventional alpha blending");
        auto m=std::make_unique<aiMaterial>();const aiColor4D color{c.r,c.g,c.b,1};
        m->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);m->AddProperty(&opacity,1,AI_MATKEY_OPACITY);
        if(mappedTexture.composed||replace){
            m->AddProperty(&style.diffuse,1,"$mat.jt.sourceDiffuse");
            const int flags=int(style.blendFlags),composed=mappedTexture.composed?1:0;
            m->AddProperty(&flags,1,"$mat.jt.sourceBlendFlags");m->AddProperty(&composed,1,"$mat.jt.composedTexture");
        }
        if(vertexAlpha||mappedTexture.alpha) {
            // Opacity remains 1: vertex alpha is applied once by the shader.
            // Explicit BLEND also selects the renderer's transparent pass and
            // preserves alpha when exporting a neutral material to glTF.
            const aiString mode("BLEND");m->AddProperty(&mode,"$mat.gltf.alphaMode",0,0);
        }
        if(mappedTexture.index>=0){
            const aiString path("*"+std::to_string(mappedTexture.index));const int coordinate=style.texture.coordinate;
            const auto& env=style.texture.environment;
            const auto wrap=[](int mode){return int(mode==2?aiTextureMapMode_Wrap:mode==3?aiTextureMapMode_Mirror:aiTextureMapMode_Clamp);};
            const int u=wrap(env[3]),v=wrap(env[4]);const int mag=env[1]==1?9728:9729;
            const int filters[]={9729,9728,9729,9984,9985,9986,9987};const int min=filters[env[2]];
            for(auto type:{aiTextureType_BASE_COLOR,aiTextureType_DIFFUSE}){
                m->AddProperty(&path,AI_MATKEY_TEXTURE(type,0));m->AddProperty(&coordinate,1,AI_MATKEY_UVWSRC(type,0));
                m->AddProperty(&u,1,AI_MATKEY_MAPPINGMODE_U(type,0));m->AddProperty(&v,1,AI_MATKEY_MAPPINGMODE_V(type,0));
                m->AddProperty(&mag,1,AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type,0));m->AddProperty(&min,1,AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type,0));
            }
        }
        if(mappedNormal.index>=0){
            const auto& env=normalState(style)->environment;const aiString path("*"+std::to_string(mappedNormal.index));const int coordinate=0;
            const auto wrap=[](int mode){return int(mode==2?aiTextureMapMode_Wrap:mode==3?aiTextureMapMode_Mirror:aiTextureMapMode_Clamp);};
            const int u=wrap(env[3]),v=wrap(env[4]),mag=env[1]==1?9728:9729;
            const int filters[]={9729,9728,9729,9984,9985,9986,9987};const int min=filters[env[2]];
            m->AddProperty(&path,AI_MATKEY_TEXTURE(aiTextureType_NORMALS,0));m->AddProperty(&coordinate,1,AI_MATKEY_UVWSRC(aiTextureType_NORMALS,0));
            m->AddProperty(&u,1,AI_MATKEY_MAPPINGMODE_U(aiTextureType_NORMALS,0));m->AddProperty(&v,1,AI_MATKEY_MAPPINGMODE_V(aiTextureType_NORMALS,0));
            m->AddProperty(&mag,1,AI_MATKEY_GLTF_MAPPINGFILTER_MAG(aiTextureType_NORMALS,0));m->AddProperty(&min,1,AI_MATKEY_GLTF_MAPPINGFILTER_MIN(aiTextureType_NORMALS,0));
        }
        if(style.present&(1u<<0)) m->AddProperty(&style.ambient,1,AI_MATKEY_COLOR_AMBIENT);
        if(style.present&(1u<<1)) m->AddProperty(&style.specular,1,AI_MATKEY_COLOR_SPECULAR);
        if(style.present&(1u<<2)) m->AddProperty(&style.emission,1,AI_MATKEY_COLOR_EMISSIVE);
        if(style.hasShininess) {
            m->AddProperty(&style.shininess,1,AI_MATKEY_SHININESS);
            // Match MaterialProcessor's existing Phong-to-PBR approximation,
            // and expose it explicitly for Assimp's PBR/glTF export path.
            const float roughness=std::sqrt(2.0f/(style.shininess+2.0f));
            m->AddProperty(&roughness,1,AI_MATKEY_ROUGHNESS_FACTOR);
        }
        if(style.present&(1u<<8)) m->AddProperty(&style.bumpiness,1,AI_MATKEY_BUMPSCALING);
        if(style.present&(1u<<5)) m->AddProperty(&style.reflectivity,1,AI_MATKEY_REFLECTIVITY);
        if(style.drawPresent) {
            const int flags=int(style.drawFlags),fields=int(style.drawPresent);
            const int twoSided=(style.drawFlags&1)==0,wireframe=(style.drawFlags&4)!=0,unlit=(style.drawFlags&8)==0;
            m->AddProperty(&flags,1,"$mat.jt.drawStyleFlags");m->AddProperty(&fields,1,"$mat.jt.drawStyleFields");
            m->AddProperty(&twoSided,1,AI_MATKEY_TWOSIDED);
            m->AddProperty(&wireframe,1,AI_MATKEY_ENABLE_WIREFRAME);
            m->AddProperty(&unlit,1,"$mat.gltf.unlit");
            // Flat shading is expressed in geometry normals: aiShadingMode_Flat
            // is mapped to Unlit by the application's generic material adapter.
            const aiShadingMode shading=unlit ? aiShadingMode_Unlit : aiShadingMode_PBR_BRDF;
            m->AddProperty(&shading,1,AI_MATKEY_SHADING_MODEL);
            if(twoSided && !unlit && !(style.drawFlags&2))
                result.warnings.append("JT single-sided lighting without back-face culling is approximated by the renderer's two-sided lighting");
        }
        if(mappedTexture.postLit){
            const int unlit=1;const aiShadingMode shading=aiShadingMode_Unlit;
            m->AddProperty(&unlit,1,"$mat.gltf.unlit");m->AddProperty(&shading,1,AI_MATKEY_SHADING_MODEL);
            const aiColor3D zero(0,0,0);m->AddProperty(&zero,1,AI_MATKEY_COLOR_EMISSIVE);
        }
        const unsigned id=unsigned(materials.size());materials.push_back(std::move(m));materialCache.emplace(std::move(key),id);return id;
    }
    std::unique_ptr<aiMesh> copyGeometry(unsigned index) {
        const auto& source=*geometries.at(index);auto copy=std::make_unique<aiMesh>();
        copy->mPrimitiveTypes=source.mPrimitiveTypes;copy->mNumVertices=source.mNumVertices;copy->mNumFaces=source.mNumFaces;
        JtData_Limits::allocation(size_t(copy->mNumVertices)*2*sizeof(aiVector3D)+size_t(copy->mNumFaces)*(sizeof(aiFace)+3*sizeof(unsigned)));
        copy->mVertices=new aiVector3D[copy->mNumVertices];copy->mNormals=new aiVector3D[copy->mNumVertices];
        std::copy_n(source.mVertices,copy->mNumVertices,copy->mVertices);std::copy_n(source.mNormals,copy->mNumVertices,copy->mNormals);
        if(source.HasVertexColors(0)) {
            JtData_Limits::allocation(size_t(copy->mNumVertices)*sizeof(aiColor4D));
            copy->mColors[0]=new aiColor4D[copy->mNumVertices];std::copy_n(source.mColors[0],copy->mNumVertices,copy->mColors[0]);
        }
        for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)if(source.HasTextureCoords(channel)) {
            JtData_Limits::allocation(size_t(copy->mNumVertices)*sizeof(aiVector3D));
            copy->mTextureCoords[channel]=new aiVector3D[copy->mNumVertices];copy->mNumUVComponents[channel]=source.mNumUVComponents[channel];
            std::copy_n(source.mTextureCoords[channel],copy->mNumVertices,copy->mTextureCoords[channel]);
        }
        copy->mFaces=new aiFace[copy->mNumFaces];
        for(unsigned i=0;i<copy->mNumFaces;++i) {copy->mFaces[i].mNumIndices=3;copy->mFaces[i].mIndices=new unsigned[3];std::copy_n(source.mFaces[i].mIndices,3,copy->mFaces[i].mIndices);}
        return copy;
    }
    unsigned add(std::unique_ptr<aiMesh> mesh,const Appearance& style,const QString& name) {
        if(mesh->HasVertexColors(0) && style.overrideColors) {delete[] mesh->mColors[0];mesh->mColors[0]=nullptr;}
        const bool colored=mesh->HasVertexColors(0);
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        const bool activeAlpha=(style.blendFlags&0x10) && !(src==1 && dst==0);
        bool vertexAlpha=false;
        if(colored) for(unsigned i=0;i<mesh->mNumVertices;++i) {
            if((i&1023)==0)options.check();
            auto& c=mesh->mColors[0][i];
            if(!activeAlpha)c.a=1;
            vertexAlpha=vertexAlpha || c.a<1;
        }
        auto resolved=style;
        if(!mesh->HasTextureCoords(0)&&std::any_of(resolved.textureUnits.begin(),resolved.textureUnits.end(),[](const auto& unit){return unit.second.type==4;})){
            textureWarning("JT normal map not applied: stored UV0 is absent");resolved.discardNormalTextures();
        }
        bool factorableVertex=colored;
        const auto checkFactorable=[&](const TextureState& stage){
            if(stage.type==9&&stage.environment[6]!=0&&stage.environment[6]!=2&&stage.environment[6]!=3)factorableVertex=false;
        };
        if(resolved.textureUnits.empty())checkFactorable(resolved.texture);
        else for(const auto& unit:resolved.textureUnits)checkFactorable(unit.second);
        aiColor4D constantColor(1,1,1,1);bool constantVertex=colored&&mesh->mNumVertices>0;
        if(constantVertex){
            constantColor=mesh->mColors[0][0];
            for(unsigned i=1;i<mesh->mNumVertices;++i){
                if((i&1023)==0)options.check();
                const auto& c=mesh->mColors[0][i];
                if(c.r!=constantColor.r||c.g!=constantColor.g||c.b!=constantColor.b){constantVertex=false;break;}
            }
        }
        // Alpha is separable for all qualified modes: keep it on the vertices
        // unless a source-format Replace removes that dependence. This avoids
        // quantizing incoming alpha or changing vertex-alpha depth classification.
        constantColor.a=1;
        const aiColor4D neutral(1,1,1,1);
        const aiColor4D* textureInput=factorableVertex?&neutral:constantVertex?&constantColor:nullptr;
        if(colored&&!constantVertex&&!factorableVertex){
            textureWarning("JT texture not applied: this combiner/texture stack with vertex colours is not qualified");resolved.texture.type=0;
        }
        if(resolved.texture.type!=0 && resolved.texture.coordinate>=0 && resolved.texture.coordinate<4
            &&!mesh->HasTextureCoords(unsigned(resolved.texture.coordinate))
            &&(resolved.texture.generation[0]!=1||resolved.texture.generation[1]!=1)){
            textureWarning("JT texture not applied: selected UV channel is absent and both S/T components are not generated");resolved.texture.type=0;
        }
        unsigned mat=material(resolved,colored,vertexAlpha,textureInput);
        if(materials[mat]->GetTextureCount(aiTextureType_DIFFUSE)>0){
            const auto mappedImage=texture(resolved,textureInput);
            const bool clampS=resolved.texture.environment[0]!=0&&resolved.texture.environment[3]==1;
            const bool clampT=resolved.texture.environment[0]!=0&&resolved.texture.environment[4]==1;
            const unsigned selected=unsigned(resolved.texture.coordinate);
            std::array<bool,4> priorUV{};std::vector<aiVector3D> savedUV;
            if(clampS||clampT){
                for(unsigned c=0;c<4;++c)priorUV[c]=mesh->HasTextureCoords(c);
                if(priorUV[selected]){
                    JtData_Limits::allocation(size_t(mesh->mNumVertices)*sizeof(aiVector3D));
                    savedUV.assign(mesh->mTextureCoords[selected],mesh->mTextureCoords[selected]+mesh->mNumVertices);
                }
            }
            const auto& t=resolved.texture.transform;bool identity=true;
            for(unsigned i=0;i<16;++i)identity=identity&&t[i]==float(i%5==0);
            const bool generate=std::any_of(resolved.texture.generation.begin(),resolved.texture.generation.end(),[](int mode){return mode!=0;});
            if(!identity||generate||mappedImage.scaleS!=1||mappedImage.scaleT!=1){
                // Bake effective mapping into this appearance-specific copy.
                // This also carries shear/reflection through scene copy and GLB
                // without relying on a restricted scale/rotation material model.
                const auto channel=unsigned(resolved.texture.coordinate);
                try{
                    const auto mapped=TextureMapping::map(*mesh,channel,t,resolved.texture.generation,resolved.texture.planes,
                        {mappedImage.scaleS,mappedImage.scaleT,mappedImage.offsetS,mappedImage.offsetT},[&]{options.check();});
                    // Pad lower absent slots for existing Assimp scene-copy and
                    // export paths, preserving the authored selected set number.
                    for(unsigned c=0;c<=channel;++c)if(!mesh->HasTextureCoords(c)){
                        JtData_Limits::allocation(size_t(mesh->mNumVertices)*sizeof(aiVector3D));
                        mesh->mTextureCoords[c]=new aiVector3D[mesh->mNumVertices]{};mesh->mNumUVComponents[c]=2;
                    }
                    std::copy(mapped.begin(),mapped.end(),mesh->mTextureCoords[channel]);
                }catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
                catch(const std::exception& error){
                    textureWarning("JT texture not applied: "+QString::fromUtf8(error.what()));
                    resolved.texture.type=0;mat=material(resolved,colored,vertexAlpha);
                }
            }
            if(resolved.texture.type!=0&&(clampS||clampT)){
                try{
                    auto refined=TextureClampMesh::apply(*mesh,selected,clampS,clampT,
                        {float(mappedImage.offsetS),float(mappedImage.offsetS+mappedImage.scaleS),
                         float(mappedImage.offsetT),float(mappedImage.offsetT+mappedImage.scaleT)},[&]{options.check();});
                    if(refined)mesh=std::move(refined);
                }catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
                catch(const std::exception& error){
                    textureWarning("JT texture not applied: "+QString::fromUtf8(error.what()));
                    for(unsigned c=0;c<4;++c)if(!priorUV[c]&&mesh->HasTextureCoords(c)){
                        delete[] mesh->mTextureCoords[c];mesh->mTextureCoords[c]=nullptr;mesh->mNumUVComponents[c]=0;
                    }
                    if(!savedUV.empty())std::copy(savedUV.begin(),savedUV.end(),mesh->mTextureCoords[selected]);
                    resolved.texture.type=0;mat=material(resolved,colored,vertexAlpha);
                }
            }
        }
        if((factorableVertex||constantVertex)&&resolved.texture.type!=0
           &&materials[mat]->GetTextureCount(aiTextureType_DIFFUSE)>0){
            const auto mappedImage=texture(resolved,textureInput);
            // A composed constant input is already present in the pixels.
            // A Modulate/Replace stack is separable in incoming RGB/alpha:
            // Modulate retains dependence; a Replace removes it permanently.
            const bool bakedConstant=constantVertex&&!factorableVertex&&mappedImage.composed;
            bool replaceColor=bakedConstant,replaceAlpha=false;
            const auto replacedComponents=[&](const TextureState& stage){
                if(stage.type==2&&mappedImage.postLit){
                    replaceColor=true;replaceAlpha=stage.environment[6]==3&&(mappedImage.format==2||mappedImage.format==12);return;
                }
                if(stage.type!=9||stage.environment[6]!=3)return;
                const auto format=textureStage(stage,activeAlpha).format;
                replaceColor=replaceColor||format!=10;
                replaceAlpha=replaceAlpha||format==2||format==4||format==10||format==12;
            };
            if(resolved.textureUnits.empty())replacedComponents(resolved.texture);
            else for(const auto& unit:resolved.textureUnits)replacedComponents(unit.second);
            vertexAlpha=false;
            for(unsigned i=0;i<mesh->mNumVertices;++i){
                if((i&1023)==0)options.check();
                auto& color=mesh->mColors[0][i];
                if(replaceColor)color.r=color.g=color.b=1;
                if(replaceAlpha)color.a=1;
                vertexAlpha=vertexAlpha||color.a<1;
            }
            mat=material(resolved,colored,vertexAlpha,textureInput);
        }
        if(style.flatNormals()) {
            const size_t count=size_t(mesh->mNumFaces)*3;
            if(count>std::numeric_limits<unsigned>::max()) throw std::runtime_error("JT flat-normal vertex count exceeds render limits");
            JtData_Limits::allocation(count*(2*sizeof(aiVector3D)+sizeof(unsigned)));
            auto positions=std::make_unique<aiVector3D[]>(count);auto normals=std::make_unique<aiVector3D[]>(count);
            std::unique_ptr<aiColor4D[]> colors;
            std::array<std::unique_ptr<aiVector3D[]>,JtElement_ShapeLOD_Vertex::MaxTextureChannels> uv;
            for(unsigned channel=0;channel<uv.size();++channel)if(mesh->HasTextureCoords(channel)) {
                JtData_Limits::allocation(count*sizeof(aiVector3D));uv[channel]=std::make_unique<aiVector3D[]>(count);
            }
            if(colored) {JtData_Limits::allocation(count*sizeof(aiColor4D));colors=std::make_unique<aiColor4D[]>(count);}
            for(unsigned i=0;i<mesh->mNumFaces;++i) {
                options.check();const auto& f=mesh->mFaces[i];
                const auto a=mesh->mVertices[f.mIndices[0]],b=mesh->mVertices[f.mIndices[1]],c=mesh->mVertices[f.mIndices[2]];
                const double x1=double(b.x)-a.x,y1=double(b.y)-a.y,z1=double(b.z)-a.z;
                const double x2=double(c.x)-a.x,y2=double(c.y)-a.y,z2=double(c.z)-a.z;
                const double x=y1*z2-z1*y2,y=z1*x2-x1*z2,z=x1*y2-y1*x2,l=std::hypot(x,y,z);
                const aiVector3D n=l>0 ? aiVector3D(float(x/l),float(y/l),float(z/l)) : mesh->mNormals[f.mIndices[0]];
                positions[3*i]=a;positions[3*i+1]=b;positions[3*i+2]=c;
                for(unsigned j=0;j<3;++j) {normals[3*i+j]=n;if(colored)colors[3*i+j]=mesh->mColors[0][f.mIndices[j]];for(unsigned channel=0;channel<uv.size();++channel)if(uv[channel])uv[channel][3*i+j]=mesh->mTextureCoords[channel][f.mIndices[j]];mesh->mFaces[i].mIndices[j]=3*i+j;}
            }
            delete[] mesh->mVertices;delete[] mesh->mNormals;
            if(colored) {delete[] mesh->mColors[0];mesh->mColors[0]=colors.release();}
            for(unsigned channel=0;channel<uv.size();++channel)if(uv[channel]) {delete[] mesh->mTextureCoords[channel];mesh->mTextureCoords[channel]=uv[channel].release();}
            mesh->mNumVertices=unsigned(count);mesh->mVertices=positions.release();mesh->mNormals=normals.release();validate(*mesh);
        }
        if(materials[mat]->GetTextureCount(aiTextureType_NORMALS)>0){
            try{TextureTangents::apply(*mesh,[&]{options.check();});}
            catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
            catch(const std::exception& error){
                textureWarning("JT normal map not applied: "+QString::fromUtf8(error.what()));resolved.discardNormalTextures();
                mat=material(resolved,colored,vertexAlpha,textureInput);
            }
        }
        mesh->mMaterialIndex=mat;mesh->mName=aiString(name.toUtf8().constData());
        meshStyles.push_back({style,!colored&&mergeableStyle(style)});
        meshes.push_back(std::move(mesh)); return unsigned(meshes.size()-1);
    }
    std::unique_ptr<aiNode> visit(const Handle(JtData_Object)& object,Appearance style={},bool parentHidden=false,bool transformLocked=false,const aiMatrix4x4& parentWorld=aiMatrix4x4(),PaletteContext palette={},MaterialSourceContext materialSource={}) {
        options.check();if(object.IsNull()) throw std::runtime_error("JT has an unresolved graph reference");
        JtData_Limits::allocation(style.textureUnits.size()*(sizeof(TextureState)+64));
        if(active.size()>=512 || ++nodes>1000000 || !active.insert(object.get()).second) throw std::runtime_error("JT graph cycle/depth limit exceeded");
        struct Pop {std::set<const void*>& s;const void* p;~Pop(){s.erase(p);}} pop{active,object.get()};
        auto source=Handle(JtNode_Base)::DownCast(object);
        if(source.IsNull()) throw std::runtime_error("Unsupported required JT graph node");
        const bool hidden=parentHidden || source->Ignored();
        const auto name=nameOf(source);const auto label=name.isEmpty()?QStringLiteral("Unnamed JT node"):name;auto node=std::make_unique<aiNode>();node->mName=aiString(name.toUtf8().constData());
        if(materialSource.qualifiedSolidEdgeWriter&&!materialSource.solidEdgeAuthor)
            materialSource.solidEdgeAuthor=solidEdgeAuthor(*source);
        const JtAttribute_Material* skipMaterial=nullptr;
        if(materialSource.qualifiedSolidEdgeWriter&&materialSource.solidEdgeAuthor
           &&palette.mapping.IsNull()&&palette.entries->empty()&&!Handle(JtNode_Shape_Base)::DownCast(object).IsNull()) {
            const JtAttribute_Material* secondMaterial=nullptr;
            unsigned materialCount=0;
            bool ordinary=true;
            for(size_t i=0;i<source->Attributes().Count();++i) {
                const auto material=Handle(JtAttribute_Material)::DownCast(source->Attributes()[i]);
                if(!material.IsNull()){if(++materialCount==2)secondMaterial=material.get();ordinary=ordinary&&ordinaryFallbackMaterial(*material); }
                if(!Handle(JtAttribute_PaletteMap)::DownCast(source->Attributes()[i]).IsNull())ordinary=false;
            }
            if(ordinary&&materialCount==2) {
                skipMaterial=secondMaterial;
                result.warnings.append(label+": Solid Edge duplicate fallback materials: using first material (compatibility profile)");
            }
        }
        for(size_t i=0;i<source->Attributes().Count();++i) {
            const auto a=source->Attributes()[i];
            const auto attribute=Handle(JtAttribute_Base)::DownCast(a);
            if(attribute.IsNull()||attribute->AccumulationIgnored()||attribute.get()==skipMaterial)continue;
            const auto mapping=Handle(JtAttribute_PaletteMap)::DownCast(a);
            if(!mapping.IsNull()) {
                if(!(attribute->FieldInhibitFlags()&1)&&(!palette.mapLocked||attribute->AccumulationForced())) {
                    palette.mapping=mapping;palette.mapLocked=palette.mapLocked||bool(attribute->FieldFinalFlags()&1);
                }
                continue;
            }
            const auto transform=Handle(JtAttribute_GeometricTransform)::DownCast(a);
            const auto appearance=Handle(JtAttribute_Material)::DownCast(a);
            const auto drawStyle=Handle(JtAttribute_DrawStyle)::DownCast(a);
            const auto textureImage=Handle(JtAttribute_TextureImage)::DownCast(a);
            bool mappedChannel=!textureImage.IsNull()&&textureImage->Parsed();
            if(mappedChannel){
                const int type=textureImage->TextureType(),channel=textureImage->Channel();
                const bool basic=textureImage->Legacy9()&&textureImage->LocalVersion()==1;
                const bool explicitImage=basic||type==1||type==2||type==3||type==5||type==7||type==8||type==9||type==10;
                if(type!=0&&type!=26&&((explicitImage&&channel<0)||(!explicitImage&&channel>=0))){
                    mappedChannel=false;textureWarning("JT texture not applied: channel assignment contradicts the texture role");
                }else if(type!=0&&type!=26&&!(type==9||type==2||(!basic&&type==4)))
                    textureWarning("JT texture role retained in unit state but not rendered by the colour pipeline");
            }
            const auto apply=[&](Appearance& style,aiMatrix4x4& matrix,bool& locked) {
                if(!appearance.IsNull())style.apply(*appearance);
                if(!drawStyle.IsNull())style.apply(*drawStyle);
                if(mappedChannel)style.applyTexture(textureImage);
                if(!transform.IsNull()&&(!locked||attribute->AccumulationForced())&&!(attribute->FieldInhibitFlags()&1)) {
                    matrix*=transformMatrix(*transform);locked=locked||bool(attribute->FieldFinalFlags()&1);
                }
            };
            const int index=attribute->PaletteIndex();
            if((index>=0||!palette.entries->empty())&&!palette.entries.unique()) {
                JtData_Limits::allocation(palette.entries->size()*(sizeof(PaletteEntry)+64));
                palette.entries=std::make_shared<PaletteContext::Entries>(*palette.entries);
            }
            if(index==-1) {
                apply(style,node->mTransformation,transformLocked);
                for(auto& item:*palette.entries)apply(item.second.style,item.second.world,item.second.transformLocked);
            } else {
                auto it=palette.entries->find(index);
                if(it==palette.entries->end()) {
                    if(palette.entries->size()>=16384)throw std::runtime_error("JT palette entry limit exceeded");
                    JtData_Limits::allocation(sizeof(PaletteEntry)+64);
                    it=palette.entries->emplace(index,PaletteEntry{style,parentWorld*node->mTransformation,transformLocked}).first;
                }
                apply(it->second.style,it->second.world,it->second.transformLocked);
            }
        }
        const auto world=parentWorld*node->mTransformation;
        for(unsigned row=0;row<4;++row) for(unsigned column=0;column<4;++column)
            if(!std::isfinite(world[row][column])) throw std::runtime_error("JT accumulated transform is nonfinite");
        if(!std::isfinite(world.Determinant()) || world.Determinant()==0) throw std::runtime_error("JT accumulated transform is singular or exceeds render precision");
        std::vector<unsigned> attached;std::vector<std::unique_ptr<aiNode>> children;
        int selectedChild=-1;
        std::optional<double> lodDistance;
        const auto switchNode=Handle(JtNode_Switch)::DownCast(object);
        const auto rangeNode=Handle(JtNode_RangeLOD)::DownCast(object);
        const auto part=Handle(JtNode_Part)::DownCast(object);bool exact=false,hasBrep=false;
        if(!part.IsNull()) for(size_t i=0;i<part->LateLoads().Count();++i) {
            const auto prop=part->LateLoads()[i];if(prop->SegmentType()!=17 && prop->SegmentType()!=30) continue;
            hasBrep=true;
            if(!options.exactDecoder || !palette.mapping.IsNull()) continue;
            try {
                options.check();prop->Load();const auto xt=Handle(JtElement_XTBRep)::DownCast(prop->DefferedObject());
                if(xt.IsNull()) throw std::runtime_error("Unsupported exact JT payload");
                const auto& bytes=xt->Data();const auto shape=options.exactDecoder(QByteArray(reinterpret_cast<const char*>(bytes.Data()),int(bytes.Count())));
                options.check();auto m=exactMesh(shape,options);attached.push_back(add(std::move(m),style,name));result.exactShapes.push_back(shape);++result.exactParts;exact=true;prop->Unload();break;
            } catch(const Cancelled&) {prop->Unload();throw;}
              catch(const std::bad_alloc&) {prop->Unload();throw;}
              catch(const Standard_Failure& e) {result.warnings.append(label+": exact geometry rejected: "+QString::fromUtf8(e.GetMessageString()));}
              catch(const std::exception& e) {result.warnings.append(label+": exact geometry rejected: "+QString::fromUtf8(e.what()));}
            prop->Unload();
        }
        if(hasBrep && !exact) {++result.fallbackParts;result.warnings.append(label+": using stored JT tessellation (exact geometry unavailable or rejected)");}
        if(!exact) {
            const auto shape=Handle(JtNode_Shape_Base)::DownCast(object);
            if(!shape.IsNull()) {
                bool loaded=false;
                for(size_t i=0;i<shape->LateLoads().Count();++i) {
                    const auto prop=shape->LateLoads()[i];if(prop->SegmentType()<6 || prop->SegmentType()>16) continue;
                    try {
                        unsigned geometryIndex;
                        const auto geometry=geometryCache.find(prop.get());
                        if(geometry!=geometryCache.end())geometryIndex=geometry->second;
                        else {
                            struct Unload {Handle(JtProperty_LateLoaded) p;~Unload(){p->Unload();}} unload{prop};
                            options.check();prop->Load();const auto mesh=Handle(JtElement_ShapeLOD_Vertex)::DownCast(prop->DefferedObject());
                            if(mesh.IsNull())throw std::runtime_error("Unsupported JT tessellation payload");
                            if(mesh->ExplicitEmpty()) {
                                if(!shape->DeclaresEmptyGeometry())throw std::runtime_error("JT empty payload contradicts advertised shape geometry");
                                result.warnings.append(label+": explicitly empty JT shape retained without render geometry");
                                loaded=true;break;
                            }
                            unsigned repairedNormals=0;
                            geometryIndex=unsigned(geometries.size());geometries.push_back(storedMesh(mesh,repairedNormals));
                            geometryGroups.emplace_back();const auto& groups=mesh->FaceGroups();
                            if(groups.Count()) {
                                if(groups.Count()!=int(geometries.back()->mNumFaces))throw std::runtime_error("JT face-group/triangle count mismatch");
                                JtData_Limits::allocation(size_t(groups.Count())*sizeof(int));
                                geometryGroups.back().assign(groups.Data(),groups.Data()+groups.Count());
                            }
                            geometryCache[prop.get()]=geometryIndex;
                            for(unsigned channel=0;channel<JtElement_ShapeLOD_Vertex::MaxTextureChannels;++channel)if(mesh->TexCoords(channel).Count()){result.warnings.append(label+": JT texture coordinates retained; texture images and shaders are not imported");break;}
                            if(repairedNormals)result.warnings.append(label+": reconstructed "+QString::number(repairedNormals)+" zero-length stored normals from adjacent triangles");
                        }
                        const auto& groups=geometryGroups.at(geometryIndex);
                        int maxGroup=-1;for(int g:groups) {if(g<0||g>16000000)throw std::runtime_error("Invalid JT face-group index");maxGroup=std::max(maxGroup,g);}
                        const bool mapped=!palette.mapping.IsNull()&&maxGroup>=0&&palette.mapping->Entries().Count()==maxGroup+1;
                        if(!palette.mapping.IsNull()&&!mapped)result.warnings.append(label+": palette-map length mismatch; using fallback appearance");
                        if(mapped) {
                            std::map<int,std::vector<unsigned>> byGroup;
                            JtData_Limits::allocation(groups.size()*16);
                            for(unsigned f=0;f<groups.size();++f)byGroup[groups[f]].push_back(f);
                            struct Batch {Appearance style;aiMatrix4x4 local;bool hidden;unsigned mat;std::vector<int> groups;std::vector<unsigned> faces;};
                            std::vector<Batch> batches;
                            using BatchKey=std::tuple<unsigned,std::array<float,16>,bool>;
                            std::map<BatchKey,unsigned> batchIndex;
                            for(const auto& group:byGroup) {
                                options.check();const int index=palette.mapping->Entries()[group.first];
                                const auto entry=palette.entries->find(index);
                                const auto& groupStyle=entry==palette.entries->end()?style:entry->second.style;
                                const unsigned groupMat=material(groupStyle);
                                aiMatrix4x4 local;
                                if(entry!=palette.entries->end()) {
                                    const auto& target=entry->second.world;
                                    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!std::isfinite(target[r][c]))throw std::runtime_error("Nonfinite JT palette transform");
                                    if(!std::isfinite(target.Determinant())||target.Determinant()==0)throw std::runtime_error("Invalid JT palette transform");
                                    auto inverse=world;inverse.Inverse();local=inverse*target;
                                }
                                std::array<float,16> transformKey;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)transformKey[r*4+c]=local[r][c];
                                const bool groupHidden=hidden||index==-2;
                                const auto inserted=batchIndex.emplace(BatchKey{groupMat,transformKey,groupHidden},unsigned(batches.size()));
                                if(inserted.second)batches.push_back({groupStyle,local,groupHidden,groupMat,{},{}});
                                auto& batch=batches[inserted.first->second];batch.groups.push_back(group.first);batch.faces.insert(batch.faces.end(),group.second.begin(),group.second.end());
                                if(index==-2)result.warnings.append(label+": inhibited face group retained as hidden geometry; viewer visibility also controls picking");
                            }
                            // Batches with the same transform and hidden state that differ only in diffuse colour/opacity are one body:
                            // one mesh with per-vertex colours instead of one open piece per colour. Others stay separate.
                            const bool sourceColoured=geometries.at(geometryIndex)->HasVertexColors(0);
                            const auto transformKeyOf=[](const aiMatrix4x4& m){std::array<float,16> k;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)k[r*4+c]=m[r][c];return k;};
                            std::map<std::tuple<std::array<float,16>,bool,std::vector<float>>,std::vector<size_t>> mergeSets;
                            if(!sourceColoured)for(size_t b=0;b<batches.size();++b)if(mergeableStyle(batches[b].style))
                                mergeSets[std::make_tuple(transformKeyOf(batches[b].local),batches[b].hidden,styleSignature(batches[b].style))].push_back(b);
                            std::vector<int> setFirst(batches.size(),-1);
                            for(const auto& entry:mergeSets)if(entry.second.size()>1)for(size_t b:entry.second)setFirst[b]=int(entry.second.front());
                            // Shape provenance on every body node: the shape, its decoded geometry and the face groups it holds.
                            const auto addChild=[&](unsigned id,const aiMatrix4x4& local,bool hide,const std::vector<int>& groupIds){
                                auto child=std::make_unique<aiNode>();child->mName=aiString((name+" / face groups "+QString::number(groupIds.front())).toUtf8().constData());
                                child->mNumMeshes=1;child->mMeshes=new unsigned[1]{id};child->mTransformation=local;
                                QStringList groupText;for(int g:groupIds)groupText.append(QString::number(g));
                                child->mMetaData=aiMetadata::Alloc(3);
                                child->mMetaData->Set(0,"JTShape",aiString(label.toUtf8().constData()));
                                child->mMetaData->Set(1,"JTGeometryIndex",aiString(QString::number(geometryIndex).toUtf8().constData()));
                                child->mMetaData->Set(2,"JTFaceGroups",aiString(groupText.join(',').toUtf8().constData()));
                                if(hide){result.hiddenNodes.insert(child.get());hiddenJournal.push_back(child.get());}
                                children.push_back(std::move(child));
                            };
                            for(size_t b=0;b<batches.size();++b) {
                                const auto& batch=batches[b];
                                if(setFirst[b]>=0 && size_t(setFirst[b])!=b) continue; // emitted with the first batch of its set
                                if(setFirst[b]<0) {
                                    const auto key=std::make_tuple(geometryIndex,batch.groups,batch.mat);
                                    const auto cached=groupMeshCache.find(key);
                                    const unsigned id=cached!=groupMeshCache.end()?cached->second:add(batch.faces.size()==geometries.at(geometryIndex)->mNumFaces?copyGeometry(geometryIndex):faceSubset(*geometries.at(geometryIndex),batch.faces),batch.style,name);
                                    groupMeshCache[key]=id;
                                    addChild(id,batch.local,batch.hidden,batch.groups);
                                    continue;
                                }
                                std::vector<size_t> members;for(size_t k=b;k<batches.size();++k)if(setFirst[k]==int(b))members.push_back(k);
                                std::vector<int> groupIds;std::vector<float> colourKey=styleSignature(batch.style);
                                for(size_t k:members){
                                    groupIds.insert(groupIds.end(),batches[k].groups.begin(),batches[k].groups.end());
                                    const auto& d=batches[k].style.diffuse;colourKey.insert(colourKey.end(),{d.r,d.g,d.b,d.a});
                                }
                                const auto key=std::make_tuple(geometryIndex,groupIds,colourKey);
                                const auto cached=mergedGroupCache.find(key);
                                unsigned id;
                                if(cached!=mergedGroupCache.end())id=cached->second;
                                else {
                                    std::vector<std::unique_ptr<aiMesh>> pieces;std::vector<aiColor4D> colours;
                                    for(size_t k:members){
                                        pieces.push_back(batches[k].faces.size()==geometries.at(geometryIndex)->mNumFaces?copyGeometry(geometryIndex):faceSubset(*geometries.at(geometryIndex),batches[k].faces));
                                        colours.push_back(batches[k].style.diffuse);
                                    }
                                    id=add(concatenateColoured(pieces,colours),batch.style,name);
                                    mergedGroupCache[key]=id;
                                }
                                addChild(id,batch.local,batch.hidden,groupIds);
                            }
                        } else {
                            // Assembly-only nodes need accumulated state, not
                            // decoded/composed images with provisional colours.
                            const unsigned mat=material(style);
                            const auto key=std::make_pair(static_cast<const void*>(prop.get()),mat);const auto cached=meshCache.find(key);
                            const unsigned id=cached!=meshCache.end()?cached->second:add(copyGeometry(geometryIndex),style,name);
                            meshCache[key]=id;attached.push_back(id);
                        }
                        ++result.storedMeshes;loaded=true;break;
                    } catch(const Cancelled&) {throw;}
                      catch(const std::bad_alloc&) {throw;}
                      catch(const Standard_Failure& e) {result.warnings.append(label+": display alternative rejected: "+QString::fromUtf8(e.GetMessageString()));}
                      catch(const std::exception& e) {result.warnings.append(label+": display alternative rejected: "+QString::fromUtf8(e.what()));}
                }
                if(!loaded) throw std::runtime_error((label+": no supported JT display tessellation"+
                    (result.warnings.isEmpty()?QString():QString("; ")+result.warnings.last())).toStdString());
            }
            const auto instance=Handle(JtNode_Instance)::DownCast(object);
            if(!instance.IsNull()) children.push_back(visit(instance->Object(),style,hidden,transformLocked,world,palette,materialSource));
            const auto group=Handle(JtNode_Group)::DownCast(object);
            if(!group.IsNull()) {
                const auto partition=Handle(JtNode_Partition)::DownCast(object);
                if(!partition.IsNull() && group->Children().IsEmpty()) {
                    const QString reference=QString::fromUtf16(reinterpret_cast<const char16_t*>(partition->FileName().ToExtString()));
                    if(!reference.isEmpty()) {
                        const QString origin=QString::fromUtf16(reinterpret_cast<const char16_t*>(partition->OriginFileName().ToExtString()));
                        const QString file=QFileInfo(QFileInfo(origin).dir().filePath(reference)).canonicalFilePath();
                        if(file.isEmpty() || !insideDirectory(file,rootDirectory) || activeFiles.count(fileKey(file))) throw std::runtime_error("Missing, cyclic or external JT partition");
                        activeFiles.insert(fileKey(file));
                        struct FilePop {std::set<QString>& files;QString path;~FilePop(){files.erase(path);}} filePop{activeFiles,fileKey(file)};
                        auto& root=partitions[fileKey(file)];
                        if(root.IsNull()) {Handle(JtData_Model) m=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(file.utf16())));root=m->Init();}
                        children.push_back(visit(root,style,hidden,transformLocked,world,palette,materialSourceContext(file,options)));
                    }
                } else {
                    const bool lod=!Handle(JtNode_LOD)::DownCast(object).IsNull();
                    const size_t childCount=group->Children().Count();
                    size_t preferred=0;
                    if(!rangeNode.IsNull()) {
                        const auto& limits=rangeNode->RangeLimits();
                        for(size_t i=0;i<limits.Count();++i)
                            if(!std::isfinite(limits[i]) || limits[i]<0 || (i && limits[i]<limits[i-1]))
                                throw std::runtime_error("Invalid JT RangeLOD range limits");
                        const auto& c=rangeNode->Center();
                        if(!std::isfinite(c.X) || !std::isfinite(c.Y) || !std::isfinite(c.Z))
                            throw std::runtime_error("Invalid JT RangeLOD centre");
                        if(options.lodEyePoint) {
                            const auto& eye=*options.lodEyePoint;
                            // Centre is MCS; thresholds and eye distance are WCS.
                            const double x=double(world.a1)*c.X+double(world.a2)*c.Y+double(world.a3)*c.Z+world.a4;
                            const double y=double(world.b1)*c.X+double(world.b2)*c.Y+double(world.b3)*c.Z+world.b4;
                            const double z=double(world.c1)*c.X+double(world.c2)*c.Y+double(world.c3)*c.Z+world.c4;
                            lodDistance=std::hypot(x-eye[0],y-eye[1],z-eye[2]);
                            if(!std::isfinite(*lodDistance)) throw std::runtime_error("Invalid JT RangeLOD eye distance");
                            if(childCount && limits.Count()) {
                                while(preferred<limits.Count() && *lodDistance>limits[preferred]) ++preferred;
                                preferred=preferred==limits.Count() ? childCount-1 : std::min(preferred,childCount-1);
                            }
                        }
                    }
                    if(!switchNode.IsNull()) {
                        const auto selected=switchNode->SelectedChild();
                        if(selected!=0xffffffffu) {
                            if(selected>=childCount) throw std::runtime_error("JT Switch selected child is out of range");
                            children.push_back(visit(group->Children()[selected],style,hidden,transformLocked,world,palette,materialSource));
                            selectedChild=int(selected);
                        }
                    } else {
                        bool selected=false;
                        for(size_t attempt=0;attempt<childCount;++attempt) {
                            // Preferred, then the remaining source-order alternatives.
                            const size_t i=!lod ? attempt : attempt==0 ? preferred : attempt<=preferred ? attempt-1 : attempt;
                            if(!lod) {children.push_back(visit(group->Children()[i],style,hidden,transformLocked,world,palette,materialSource));continue;}
                            const auto saved=checkpoint();
                            try {
                                auto child=visit(group->Children()[i],style,hidden,transformLocked,world,palette,materialSource);
                                if(!containsMesh(*child)) throw std::runtime_error("LOD alternative contains no render geometry");
                                children.push_back(std::move(child));selected=true;selectedChild=int(i);
                                if(!rangeNode.IsNull() && i!=preferred)
                                    result.warnings.append(label+": RangeLOD preferred child unavailable; using child "+QString::number(i));
                                break;
                            } catch(const Cancelled&) {throw;}
                              catch(const std::bad_alloc&) {throw;}
                              catch(const Standard_Failure& e) {rollback(saved);result.warnings.append(label+": LOD alternative "+QString::number(i+1)+" rejected: "+QString::fromUtf8(e.GetMessageString()));}
                              catch(const std::exception& e) {rollback(saved);result.warnings.append(label+": LOD alternative "+QString::number(i+1)+" rejected: "+QString::fromUtf8(e.what()));}
                        }
                        if(lod && !selected) throw std::runtime_error((label+": no usable JT LOD alternative"+
                            (result.warnings.isEmpty()?QString():QString("; ")+result.warnings.last())).toStdString());
                    }
                }
            }
        }
        if(hasBrep && !exact && attached.empty() && std::none_of(children.begin(),children.end(),[](const auto& child){return containsMesh(*child);}))
            throw std::runtime_error("JT part has B-Rep but no usable tessellation fallback");
        if(hidden) {result.hiddenNodes.insert(node.get());hiddenJournal.push_back(node.get());}
        if(!part.IsNull()&&!exact)partNodes.push_back(node.get());
        node->mNumMeshes=unsigned(attached.size());if(!attached.empty()) {node->mMeshes=new unsigned[attached.size()];std::copy(attached.begin(),attached.end(),node->mMeshes);}
        node->mNumChildren=unsigned(children.size());if(!children.empty()) {node->mChildren=new aiNode*[children.size()]{};for(size_t i=0;i<children.size();++i) {children[i]->mParent=node.get();node->mChildren[i]=children[i].release();}}
        const bool selectionNode=!switchNode.IsNull() || !Handle(JtNode_LOD)::DownCast(object).IsNull();
        auto sourceProperties=properties(*source,label);
        auto sourceTextures=textureDescriptors(*source,label);
        const bool unitMetadata=!style.textureUnits.empty()&&(sourceTextures||!attached.empty());
        node->mMetaData=aiMetadata::Alloc(1+(selectionNode?1:0)+(lodDistance?1:0)+(sourceProperties?1:0)+(sourceTextures?1:0)+(unitMetadata?1:0));
        unsigned meta=0;node->mMetaData->Set(meta++,"JTRepresentation",aiString(exact ? "BRep" : attached.empty() ? "Assembly" : "StoredTessellation"));
        if(selectionNode) node->mMetaData->Set(meta++,"JTSelectedChild",int32_t(selectedChild));
        if(lodDistance) node->mMetaData->Set(meta++,"JTRangeLODDistance",*lodDistance);
        if(sourceProperties) node->mMetaData->Set(meta++,"JTProperties",*sourceProperties);
        if(sourceTextures) node->mMetaData->Set(meta++,"JTTextures",*sourceTextures);
        if(unitMetadata){
            // Charge the temporary records and both nested-metadata deep copies,
            // including aiString's fixed-size storage, not just the final payload.
            JtData_Limits::allocation(style.textureUnits.size()*32768);
            std::unique_ptr<aiMetadata> units(aiMetadata::Alloc(unsigned(style.textureUnits.size())));unsigned index=0;
            for(const auto& item:style.textureUnits){const auto& state=item.second;std::unique_ptr<aiMetadata> record(aiMetadata::Alloc(7));
                record->Set(0,"Unit",int32_t(item.first));record->Set(1,"Automatic",state.automatic);record->Set(2,"TextureType",int32_t(state.type));
                record->Set(3,"CoordinateChannel",int32_t(state.coordinate));record->Set(4,"Combiner",int32_t(state.environment[6]));
                record->Set(5,"Active",state.type!=0&&state.environment[6]!=0);record->Set(6,"FieldFinal",uint64_t(state.finalFields));
                units->Set(index++,std::to_string(item.first),*record);
            }
            node->mMetaData->Set(meta++,"JTTextureUnits",*units);
        }
        if(options.progress) options.progress("Building JT scene",int(nodes),0);
        return node;
    }
};
// Concatenates meshes into one, baking each piece's matrix (its frame relative to the part node) into positions and normals.
struct MergePiece {const aiMesh* mesh;aiMatrix4x4 matrix;};
std::unique_ptr<aiMesh> concatenateTransformed(const std::vector<MergePiece>& pieces,const std::vector<aiColor4D>* colours=nullptr) {
    size_t vertices=0,faces=0;
    for(const auto& piece:pieces){vertices+=piece.mesh->mNumVertices;faces+=piece.mesh->mNumFaces;}
    if(pieces.empty()||vertices>std::numeric_limits<unsigned>::max()||faces>std::numeric_limits<unsigned>::max())throw std::runtime_error("JT merged part meshes exceed the supported size");
    const aiMesh& first=*pieces.front().mesh;
    JtData_Limits::allocation(vertices*(2*sizeof(aiVector3D)+sizeof(aiColor4D)+AI_MAX_NUMBER_OF_TEXTURECOORDS*sizeof(aiVector3D))+faces*(sizeof(aiFace)+3*sizeof(unsigned)));
    auto mesh=std::make_unique<aiMesh>();
    mesh->mName=first.mName;mesh->mMaterialIndex=first.mMaterialIndex;mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices=unsigned(vertices);mesh->mNumFaces=unsigned(faces);
    mesh->mVertices=new aiVector3D[vertices];mesh->mNormals=new aiVector3D[vertices];
    if(colours||first.HasVertexColors(0))mesh->mColors[0]=new aiColor4D[vertices];
    for(unsigned channel=0;channel<AI_MAX_NUMBER_OF_TEXTURECOORDS;++channel)
        if(first.HasTextureCoords(channel)){mesh->mTextureCoords[channel]=new aiVector3D[vertices];mesh->mNumUVComponents[channel]=first.mNumUVComponents[channel];}
    mesh->mFaces=new aiFace[faces];
    unsigned vertexBase=0,faceBase=0;
    for(size_t k=0;k<pieces.size();++k) {
        const auto& piece=pieces[k];
        const aiMesh& source=*piece.mesh;
        const bool identity=piece.matrix.IsIdentity();
        aiMatrix3x3 normalMatrix(piece.matrix);normalMatrix.Inverse().Transpose();
        for(unsigned i=0;i<source.mNumVertices;++i) {
            JtData_Limits::poll();
            mesh->mVertices[vertexBase+i]=identity?source.mVertices[i]:piece.matrix*source.mVertices[i];
            aiVector3D normal=identity?source.mNormals[i]:normalMatrix*source.mNormals[i];
            if(!identity&&normal.SquareLength()>0)normal.Normalize();
            mesh->mNormals[vertexBase+i]=normal;
            if(mesh->mColors[0])mesh->mColors[0][vertexBase+i]=colours?(*colours)[k]:source.mColors[0][i];
            for(unsigned channel=0;channel<AI_MAX_NUMBER_OF_TEXTURECOORDS;++channel)
                if(mesh->mTextureCoords[channel])mesh->mTextureCoords[channel][vertexBase+i]=source.mTextureCoords[channel][i];
        }
        for(unsigned i=0;i<source.mNumFaces;++i) {
            auto& f=mesh->mFaces[faceBase+i];f.mNumIndices=3;f.mIndices=new unsigned[3];
            for(unsigned j=0;j<3;++j)f.mIndices[j]=vertexBase+source.mFaces[i].mIndices[j];
        }
        vertexBase+=source.mNumVertices;faceBase+=source.mNumFaces;
    }
    validate(*mesh);return mesh;
}
// Merges the meshes of every leaf part (a Part node with no Part below it) that share a material and vertex layout.
struct PartMerger {
    const Options& options;
    std::vector<std::unique_ptr<aiMesh>>& meshes;
    QSet<const aiNode*>& hidden;
    Builder& builder;
    std::unordered_set<const aiNode*> parts;
    // Instances of one part build identical pieces, so they share the merged mesh.
    std::map<std::vector<std::pair<unsigned,std::array<float,16>>>,unsigned> merged;
    unsigned mergedParts=0,pieceCount=0,mergedCount=0;
    // What the pass saw, reported so a run that merges nothing still says why.
    unsigned leafParts=0,assemblyParts=0,leafPartsWithMeshes=0,meshSlots=0,candidates=0,groupCount=0,multiGroups=0,rejectedHidden=0,rejectedLayout=0,rejectedMirrored=0,partsLeftSplit=0;
    // 0 = can be merged, 1 = mesh layout not supported, 2 = mirrored or non-finite frame
    int reject(unsigned id,const aiMatrix4x4& matrix) const {
        if(id>=meshes.size()||!meshes[id])return 1;
        const aiMesh& m=*meshes[id];
        if(!m.mNumVertices||!m.mNumFaces||!m.HasNormals()||m.HasTangentsAndBitangents()||m.mNumBones||m.mNumAnimMeshes||m.HasVertexColors(1))return 1;
        if(m.mPrimitiveTypes!=aiPrimitiveType_TRIANGLE)return 1;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!std::isfinite(matrix[r][c]))return 2;
        const auto det=matrix.Determinant(); // a mirrored frame would flip the winding once baked
        return std::isfinite(det)&&det>0?0:2;
    }
    // Returns true when the node is empty after its pieces were taken and must be removed.
    bool prune(aiNode* node,const std::set<const aiNode*>& touched) {
        std::vector<aiNode*> kept;bool removed=false;
        for(unsigned i=0;i<node->mNumChildren;++i) {
            aiNode* child=node->mChildren[i];
            if(prune(child,touched)){node->mChildren[i]=nullptr;delete child;removed=true;} else kept.push_back(child);
        }
        if(removed) {
            delete[] node->mChildren;node->mNumChildren=unsigned(kept.size());
            node->mChildren=kept.empty()?nullptr:new aiNode*[kept.size()];
            std::copy(kept.begin(),kept.end(),node->mChildren);
        }
        return !node->mNumMeshes&&!node->mNumChildren&&!hidden.contains(node)&&(removed||touched.count(node));
    }
    void mergePart(aiNode* part) {
        struct Entry {aiNode* node;unsigned slot;unsigned mesh;aiMatrix4x4 matrix;};
        std::vector<Entry> entries;
        const std::function<void(aiNode*,const aiMatrix4x4&)> collect=[&](aiNode* node,const aiMatrix4x4& matrix){
            options.check();
            meshSlots+=node->mNumMeshes;
            if(hidden.contains(node))rejectedHidden+=node->mNumMeshes;
            else for(unsigned slot=0;slot<node->mNumMeshes;++slot) {
                const int why=reject(node->mMeshes[slot],matrix);
                if(why==0){entries.push_back({node,slot,node->mMeshes[slot],matrix});++candidates;}
                else if(why==1)++rejectedLayout;
                else ++rejectedMirrored;
            }
            for(unsigned i=0;i<node->mNumChildren;++i)collect(node->mChildren[i],matrix*node->mChildren[i]->mTransformation);
        };
        collect(part,aiMatrix4x4());
        // Meshes whose styles differ only in diffuse colour and opacity (and carry no colours or textures of their own) form one
        // group and become one mesh with per-vertex colours; every other mesh groups by its material.
        using Key=std::tuple<bool,unsigned,std::vector<float>,bool,std::array<unsigned,AI_MAX_NUMBER_OF_TEXTURECOORDS>>;
        std::map<Key,std::vector<size_t>> groups;
        for(size_t i=0;i<entries.size();++i) {
            const aiMesh& m=*meshes[entries[i].mesh];
            std::array<unsigned,AI_MAX_NUMBER_OF_TEXTURECOORDS> uv{};
            for(unsigned c=0;c<AI_MAX_NUMBER_OF_TEXTURECOORDS;++c)uv[c]=m.HasTextureCoords(c)?m.mNumUVComponents[c]+1:0;
            const auto& info=builder.meshStyles.at(entries[i].mesh);
            // One part is one body: face colours differ, but a part's other material fields (specular, shininess) are not worth a
            // separate mesh. Draw mode and blending still split, since they change how the whole mesh is rendered.
            if(info.unifiable)groups[Key{true,0,{float(info.style.drawFlags),float(info.style.drawPresent),float(info.style.blendFlags),float(info.style.diffuse.a<1)},false,uv}].push_back(i);
            else groups[Key{false,m.mMaterialIndex,{},m.HasVertexColors(0),uv}].push_back(i);
        }
        std::map<aiNode*,std::set<unsigned>> taken;std::vector<unsigned> attach;
        if(!entries.empty())++leafPartsWithMeshes;
        groupCount+=unsigned(groups.size());
        if(groups.size()>1)++partsLeftSplit;
        for(const auto& group:groups) {
            if(group.second.size()<2)continue;
            ++multiGroups;
            std::vector<std::pair<unsigned,std::array<float,16>>> key;std::vector<MergePiece> pieces;
            for(size_t i:group.second) {
                const auto& e=entries[i];std::array<float,16> flat;
                for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)flat[r*4+c]=e.matrix[r][c];
                key.emplace_back(e.mesh,flat);pieces.push_back({meshes[e.mesh].get(),e.matrix});
                taken[e.node].insert(e.slot);
            }
            auto found=merged.find(key);
            if(found==merged.end()) {
                unsigned firstId=entries[group.second.front()].mesh; // the largest piece supplies the shared material fields and name
                for(size_t i:group.second)if(meshes[entries[i].mesh]->mNumFaces>meshes[firstId]->mNumFaces)firstId=entries[i].mesh;
                const bool unified=std::get<0>(group.first);
                std::vector<aiColor4D> colours;bool differ=false;
                if(unified)for(size_t i:group.second){
                    colours.push_back(builder.meshStyles.at(entries[i].mesh).style.diffuse);
                    differ=differ||colours.back()!=colours.front();
                }
                auto mesh=concatenateTransformed(pieces,differ?&colours:nullptr);
                unsigned id;
                const Builder::MeshStyle firstStyle=builder.meshStyles.at(firstId); // copied: add() grows the vector
                if(differ)id=builder.add(std::move(mesh),firstStyle.style,QString::fromUtf8(meshes[firstId]->mName.C_Str()));
                else {id=unsigned(meshes.size());builder.meshStyles.push_back({firstStyle.style,false});meshes.push_back(std::move(mesh));}
                found=merged.emplace(std::move(key),id).first;
            }
            attach.push_back(found->second);pieceCount+=unsigned(group.second.size());++mergedCount;
        }
        if(attach.empty())return;
        ++mergedParts;
        std::set<const aiNode*> touched;
        for(const auto& item:taken) {
            aiNode* node=item.first;touched.insert(node);
            std::vector<unsigned> kept;
            for(unsigned slot=0;slot<node->mNumMeshes;++slot)if(!item.second.count(slot))kept.push_back(node->mMeshes[slot]);
            delete[] node->mMeshes;node->mNumMeshes=unsigned(kept.size());
            node->mMeshes=kept.empty()?nullptr:new unsigned[kept.size()];std::copy(kept.begin(),kept.end(),node->mMeshes);
        }
        std::vector<unsigned> all(part->mMeshes,part->mMeshes+part->mNumMeshes);all.insert(all.end(),attach.begin(),attach.end());
        delete[] part->mMeshes;part->mNumMeshes=unsigned(all.size());part->mMeshes=new unsigned[all.size()];std::copy(all.begin(),all.end(),part->mMeshes);
        // The part itself always stays; the emptied shape nodes below it go.
        std::vector<aiNode*> kept;bool removed=false;
        for(unsigned i=0;i<part->mNumChildren;++i) {
            aiNode* child=part->mChildren[i];
            if(prune(child,touched)){delete child;removed=true;} else kept.push_back(child);
        }
        if(removed) {
            delete[] part->mChildren;part->mNumChildren=unsigned(kept.size());
            part->mChildren=kept.empty()?nullptr:new aiNode*[kept.size()];std::copy(kept.begin(),kept.end(),part->mChildren);
        }
    }
    // Returns whether the subtree holds a Part node; a Part with another Part below it is an assembly and is left alone.
    bool visit(aiNode* node) {
        options.check();bool nested=false;
        for(unsigned i=0;i<node->mNumChildren;++i)if(visit(node->mChildren[i]))nested=true;
        const bool isPart=parts.count(node)!=0;
        if(isPart)++(nested?assemblyParts:leafParts);
        if(isPart&&!nested)mergePart(node);
        return isPart||nested;
    }
    // Drops meshes no node uses any more and renumbers the rest.
    void compact(aiNode* root) {
        std::vector<char> used(meshes.size(),0);
        const std::function<void(const aiNode*)> mark=[&](const aiNode* node){
            for(unsigned i=0;i<node->mNumMeshes;++i)used.at(node->mMeshes[i])=1;
            for(unsigned i=0;i<node->mNumChildren;++i)mark(node->mChildren[i]);
        };
        mark(root);
        std::vector<unsigned> remap(meshes.size(),0);std::vector<std::unique_ptr<aiMesh>> next;
        for(size_t i=0;i<meshes.size();++i)if(used[i]){remap[i]=unsigned(next.size());next.push_back(std::move(meshes[i]));}
        const std::function<void(aiNode*)> apply=[&](aiNode* node){
            for(unsigned i=0;i<node->mNumMeshes;++i)node->mMeshes[i]=remap[node->mMeshes[i]];
            for(unsigned i=0;i<node->mNumChildren;++i)apply(node->mChildren[i]);
        };
        apply(root);meshes=std::move(next);
    }
};
}
ImportResult Detail::buildScene(const Handle(JtNode_Partition)& root,const QString& path,const Options& options) {
    std::unique_ptr<JtData_Limits::Scope> scope;
    if(!JtData_Limits::check) scope=std::make_unique<JtData_Limits::Scope>([&]{options.check();},options.decodedAllocationBudget);
    options.check();const QFileInfo info(path);
    if(options.lodEyePoint) for(double coordinate:*options.lodEyePoint)
        if(!std::isfinite(coordinate)) throw std::runtime_error("Invalid JT RangeLOD eye point");
    Builder b{options,info.canonicalPath()};b.activeFiles.insert(fileKey(info.canonicalFilePath()));b.material(Appearance{});
    auto tree=b.visit(root,{},false,false,aiMatrix4x4(),{},materialSourceContext(info.absoluteFilePath(),options));if(b.meshes.empty()) throw std::runtime_error("JT contains no supported tessellation and no validated exact geometry");
    if(options.mergePartMeshes) {
        const size_t before=b.meshes.size();
        PartMerger merger{options,b.meshes,b.result.hiddenNodes,b,{}};
        merger.parts.insert(b.partNodes.begin(),b.partNodes.end());
        merger.visit(tree.get());
        b.result.warnings.append(QStringLiteral("JT part merge check: %1 Part nodes (%2 leaf, %3 holding other Parts); %4 mesh slots in leaf parts, %5 mergeable "
            "(not mergeable: %6 hidden, %7 unsupported layout, %8 mirrored); %9 groups, %10 with several meshes; %11 parts keep more than one mesh.")
            .arg(b.partNodes.size()).arg(merger.leafParts).arg(merger.assemblyParts).arg(merger.meshSlots).arg(merger.candidates)
            .arg(merger.rejectedHidden).arg(merger.rejectedLayout).arg(merger.rejectedMirrored).arg(merger.groupCount).arg(merger.multiGroups).arg(merger.partsLeftSplit));
        if(merger.mergedParts) {
            merger.compact(tree.get());
            b.result.warnings.append(QStringLiteral("JT part meshes merged: %1 meshes became %2 in %3 parts (the shape nodes below a part are not kept; MODELVIEWER_JT_MERGE_PART_MESHES=0 turns this off).")
                .arg(before).arg(b.meshes.size()).arg(merger.mergedParts));
        }
    }
    {
        // Nodes the file leaves unnamed (wrapper levels, shape nodes) take the nearest named ancestor's name rather than showing blank.
        const std::function<void(aiNode*,const aiString&)> fill=[&](aiNode* node,const aiString& inherited){
            if(!node->mName.length)node->mName=inherited;
            for(unsigned i=0;i<node->mNumChildren;++i)fill(node->mChildren[i],node->mName);
        };
        fill(tree.get(),aiString(info.completeBaseName().toUtf8().constData()));
    }
    b.result.scene=std::make_unique<aiScene>();auto& scene=*b.result.scene;
    scene.mRootNode=tree.release();scene.mNumMeshes=unsigned(b.meshes.size());scene.mMeshes=new aiMesh*[scene.mNumMeshes]{};
    for(unsigned i=0;i<scene.mNumMeshes;++i) scene.mMeshes[i]=b.meshes[i].release();
    scene.mNumMaterials=unsigned(b.materials.size());scene.mMaterials=new aiMaterial*[scene.mNumMaterials]{};
    for(unsigned i=0;i<scene.mNumMaterials;++i) scene.mMaterials[i]=b.materials[i].release();
    scene.mNumTextures=unsigned(b.textures.size());
    if(scene.mNumTextures){scene.mTextures=new aiTexture*[scene.mNumTextures]{};for(unsigned i=0;i<scene.mNumTextures;++i)scene.mTextures[i]=b.textures[i].release();}
    options.check();return std::move(b.result);
}
}
