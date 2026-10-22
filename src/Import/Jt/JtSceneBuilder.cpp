// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtSceneBuilder.h"
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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <array>
#include <limits>

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
    int type=0,coordinate=0;
    unsigned finalFields=0;
    std::array<int,8> environment{0,2,6,2,2,2,2,0};
    std::array<float,4> borderColor{};
    std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<int,4> generation{};
    std::array<float,16> planes{};
    bool planar2D() const {
        if(generation[0]<0||generation[0]>1||generation[1]<0||generation[1]>1
            ||generation[2]!=0||generation[3]!=0)return false;
        for(unsigned component=0;component<2;++component)if(generation[component]==1){
            const auto* p=planes.data()+4*component;
            const double norm=double(p[0])*p[0]+double(p[1])*p[1]+double(p[2])*p[2];
            if(std::abs(norm-1.0)>1e-5)return false;
        }
        return true;
    }
    bool affine2D() const {
        // Serialized JT matrices use the same row-vector placement as geometric
        // attributes: translation occupies the last row. Only stored (S,T,0,1)
        // with an unchanged R/Q plane is qualified here; no projective divide.
        return transform[2]==0&&transform[3]==0&&transform[6]==0&&transform[7]==0
            &&transform[8]==0&&transform[9]==0&&transform[10]==1&&transform[11]==0
            &&transform[14]==0&&transform[15]==1;
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
        if(allowed&16)environment[6]=env[6];
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
struct Builder {
    const Options& options; QString rootDirectory;
    ImportResult result;
    std::vector<std::unique_ptr<aiMesh>> meshes;
    // Canonical decoded geometry is independent of material/flat-normal variants.
    std::vector<std::unique_ptr<aiMesh>> geometries;
    std::vector<std::unique_ptr<aiMaterial>> materials;
    std::vector<std::unique_ptr<aiTexture>> textures;
    std::map<QByteArray,unsigned> texturePixels;
    struct ImageResult {int index=-1;bool alpha=false;QString error;double scaleS=1,scaleT=1,offsetS=0,offsetT=0;};
    using TextureSourceKey=std::tuple<const void*,bool,bool,bool,std::array<float,4>>;
    std::map<TextureSourceKey,ImageResult> textureSources;
    std::vector<std::vector<int>> geometryGroups;
    std::map<std::tuple<unsigned,std::vector<int>,unsigned>,unsigned> groupMeshCache;
    std::set<const void*> active;
    std::set<QString> activeFiles;
    std::map<QString,Handle(JtNode_Partition)> partitions;
    std::map<std::pair<const void*,unsigned>,unsigned> meshCache;
    std::map<std::vector<float>,unsigned> materialCache;
    std::map<const void*,unsigned> geometryCache;
    std::vector<const aiNode*> hiddenJournal;
    unsigned nodes=0;
    void textureWarning(const QString& text) {if(!result.warnings.contains(text))result.warnings.append(text);}
    ImageResult image(const TextureState& state,bool allowAlpha) {
        const bool padS=state.environment[0]==1&&state.environment[3]==5;
        const bool padT=state.environment[0]==1&&state.environment[4]==5;
        const TextureSourceKey sourceKey{state.image.get(),allowAlpha,padS,padT,(padS||padT)?state.borderColor:std::array<float,4>{}};
        if(!state.image.IsNull()) {
            const auto cached=textureSources.find(sourceKey);
            if(cached!=textureSources.end()){if(!cached->second.error.isEmpty())textureWarning(cached->second.error);return cached->second;}
        }
        ImageResult resultImage;
        try {
            options.check();
            const auto& source=*state.image;
            if(source.Images().size()!=1)throw std::runtime_error("only one 2D image is supported");
            const auto& data=source.Images()[0];QImage pixels;
            constexpr uint64_t maxBytes=64u*1024u*1024u;
            if(source.Inline()) {
                if((padS||padT)&&data.format!=2&&data.format!=12)
                    throw std::runtime_error("sampled constant border currently requires RGBA/BGRA storage");
                const bool colorFormat=(data.format>=1&&data.format<=4)||(data.format>=7&&data.format<=12);
                if(data.dataType<1||data.dataType>9||data.dataType==4||!colorFormat
                    ||data.dimensionality!=2||data.depth!=1||data.border<0||data.border>1)
                    throw std::runtime_error("inline texture requires a supported 2D numeric colour format");
                if(data.border==1&&(data.width<=2||data.height<=2))throw std::runtime_error("inline border texture has no interior texels");
                const size_t components=(data.format==2||data.format==12)?4:(data.format==1||data.format==11)?3:data.format==4?2:1;
                const size_t componentBytes=(data.dataType==2||data.dataType==7||data.dataType==8)?4:(data.dataType==5||data.dataType==6||data.dataType==9)?2:1;
                const bool signedComponents=data.dataType==1||data.dataType==6||data.dataType==8;
                const size_t stride=(size_t(data.width)*components*componentBytes+data.alignment-1)&~size_t(data.alignment-1);
                if(uint64_t(data.width)*data.height*4>maxBytes||data.baseMip.size()!=stride*size_t(data.height))
                    throw std::runtime_error("inline texture row/image byte count mismatch");
                JtData_Limits::allocation(size_t(data.width)*data.height*4);
                pixels=QImage(data.width,data.height,QImage::Format_RGBA8888);
                if(pixels.isNull())throw std::bad_alloc();
                for(int y=0;y<data.height;++y){
                    options.check();auto* dst=pixels.scanLine(y);const auto* row=data.baseMip.data()+size_t(data.height-1-y)*stride;
                    for(int x=0;x<data.width;++x){
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
                    // Qualified wrap modes never sample this ring. Validate the
                    // complete source image above, then retain its interior only.
                    JtData_Limits::allocation(size_t(data.width-2)*(data.height-2)*4);
                    pixels=pixels.copy(1,1,data.width-2,data.height-2);
                    if(pixels.isNull())throw std::bad_alloc();
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
                if((padS||padT)&&!decoded.hasAlphaChannel())throw std::runtime_error("sampled constant border requires an RGBA external image");
                pixels=decoded.convertToFormat(QImage::Format_RGBA8888);
                if(pixels.isNull()||pixels.size()!=size)throw std::runtime_error("external image decoding failed");
            }
            if(padS||padT){
                // A constant one-texel halo plus interior-relative UVs reproduces
                // base-level nearest/linear CLAMP_TO_BORDER via clamp-to-edge.
                // Mipmap filters are excluded: a regenerated halo chain would
                // change the source interior's level sizes and filter footprint.
                const int width=pixels.width()+2*int(padS),height=pixels.height()+2*int(padT);
                if(uint64_t(width)*height*4>maxBytes)throw std::runtime_error("padded border texture exceeds image byte limit");
                JtData_Limits::allocation(size_t(width)*height*4);
                QImage padded(width,height,QImage::Format_RGBA8888);if(padded.isNull())throw std::bad_alloc();
                const auto byte=[](float v){return int(std::floor(std::max(0.0,std::min(1.0,double(v)))*255+0.5));};
                padded.fill(QColor(byte(state.borderColor[0]),byte(state.borderColor[1]),byte(state.borderColor[2]),byte(state.borderColor[3])));
                for(int y=0;y<pixels.height();++y){options.check();std::memcpy(padded.scanLine(y+int(padT))+4*int(padS),pixels.constScanLine(y),size_t(pixels.width())*4);}
                resultImage.scaleS=double(pixels.width())/width;resultImage.scaleT=double(pixels.height())/height;
                resultImage.offsetS=double(padS)/width;resultImage.offsetT=double(padT)/height;
                pixels=std::move(padded);
            }
            for(int y=0;y<pixels.height();++y){options.check();auto* row=pixels.scanLine(y);
                for(int x=0;x<pixels.width();++x){if(!allowAlpha)row[4*x+3]=255;else resultImage.alpha=resultImage.alpha||row[4*x+3]<255;}}
            QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
            if(!pixels.save(&buffer,"PNG")||uint64_t(png.size())>maxBytes)throw std::runtime_error("texture PNG encoding failed or exceeded byte limit");
            JtData_Limits::allocation(size_t(png.size()));options.check();
            const auto hash=QCryptographicHash::hash(png,QCryptographicHash::Sha256);const auto found=texturePixels.find(hash);
            if(found!=texturePixels.end())resultImage.index=int(found->second);
            else {
                if(textures.size()>=4096)throw std::runtime_error("texture resource count limit exceeded");
                auto texture=std::make_unique<aiTexture>();texture->mWidth=unsigned(png.size());texture->mHeight=0;
                std::memcpy(texture->achFormatHint,"png",4);const size_t texels=(size_t(png.size())+sizeof(aiTexel)-1)/sizeof(aiTexel);
                JtData_Limits::allocation(texels*sizeof(aiTexel));texture->pcData=new aiTexel[texels]{};
                std::memcpy(texture->pcData,png.constData(),size_t(png.size()));
                resultImage.index=int(textures.size());textures.push_back(std::move(texture));texturePixels.emplace(hash,unsigned(resultImage.index));
            }
        }catch(const Cancelled&){throw;}catch(const std::bad_alloc&){throw;}
        catch(const std::exception& e){resultImage.error="JT texture not applied: "+QString::fromUtf8(e.what());textureWarning(resultImage.error);}
        textureSources[sourceKey]=resultImage;return resultImage;
    }
    ImageResult texture(const Appearance& style) {
        const auto& state=style.texture;if(state.type==0||state.image.IsNull())return {};
        const auto& env=state.environment;
        const bool ignoresBorder=env[3]>=2&&env[3]<=4&&env[4]>=2&&env[4]<=4;
        const bool constantBorder=env[0]==1&&(env[3]==5||env[4]==5)&&env[3]>=2&&env[3]<=5&&env[4]>=2&&env[4]<=5&&env[1]>=1&&env[1]<=2&&env[2]>=1&&env[2]<=2;
        if(state.type!=9||(env[0]!=0&&!ignoresBorder&&!constantBorder)||env[6]!=2||!state.affine2D()||state.coordinate<0||state.coordinate>=4
            ||!state.planar2D()
            ||(env[3]==5&&!constantBorder)||(env[4]==5&&!constantBorder)
            ||env[3]<1||env[3]>5||env[4]<1||env[4]>5) {
            textureWarning("JT texture not applied: supported mapping is channel-0 pre-lit 2D modulate, UV0-3 with stored or model-linear S/T, affine 2D mapping, unsampled borders or non-mipmapped RGBA constant borders");return {};
        }
        if(env[0]==2&&!state.image->Inline()){
            textureWarning("JT texture not applied: explicit border requires an inline image descriptor");return {};
        }
        if(state.image->Inline()&&state.image->Images().size()==1
            &&state.image->Images()[0].border!=(env[0]==2?1:0)){
            textureWarning("JT texture not applied: border mode and image descriptor disagree");return {};
        }
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        return image(state,(style.blendFlags&0x10)&&!(src==1&&dst==0));
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
    };
    Checkpoint checkpoint() const {return {meshes.size(),materials.size(),result.exactShapes.size(),hiddenJournal.size(),geometries.size(),result.warnings.size(),result.storedMeshes,result.exactParts,result.fallbackParts};}
    void rollback(const Checkpoint& c) {
        meshes.resize(c.meshes);materials.resize(c.materials);result.exactShapes.resize(c.shapes);
        geometries.resize(c.geometries);geometryGroups.resize(c.geometries);
        for(auto it=groupMeshCache.begin();it!=groupMeshCache.end();) {if(it->second>=c.meshes)it=groupMeshCache.erase(it);else ++it;}
        while(result.warnings.size()>c.warnings) result.warnings.removeLast();
        while(hiddenJournal.size()>c.hidden) {result.hiddenNodes.remove(hiddenJournal.back());hiddenJournal.pop_back();}
        for(auto it=meshCache.begin();it!=meshCache.end();) {if(it->second>=c.meshes) it=meshCache.erase(it);else ++it;}
        for(auto it=geometryCache.begin();it!=geometryCache.end();) {if(it->second>=c.geometries) it=geometryCache.erase(it);else ++it;}
        for(auto it=materialCache.begin();it!=materialCache.end();) {if(it->second>=c.materials) it=materialCache.erase(it);else ++it;}
        result.storedMeshes=c.stored;result.exactParts=c.exact;result.fallbackParts=c.fallback;
    }
    unsigned material(const Appearance& style,bool vertexColors=false,bool vertexAlpha=false) {
        const auto mappedTexture=texture(style);
        const auto c=vertexColors ? aiColor4D(1,1,1,1) : style.diffuse;
        const bool blending=(style.blendFlags&0x10)!=0;
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        const float opacity=(!blending || (src==1 && dst==0)) ? 1.0f : c.a;
        std::vector<float> key{c.r,c.g,c.b,opacity,style.ambient.r,style.ambient.g,style.ambient.b,
            style.specular.r,style.specular.g,style.specular.b,style.emission.r,style.emission.g,style.emission.b,
            style.bumpiness,style.reflectivity,style.shininess,float(style.hasShininess),float(style.overrideColors),float(style.present),float(style.drawFlags),float(style.drawPresent),float(style.blendFlags),float(vertexAlpha),float(mappedTexture.index),float(style.texture.coordinate)};
        if(mappedTexture.index>=0){
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
        if(resolved.texture.type!=0 && resolved.texture.coordinate>=0 && resolved.texture.coordinate<4
            &&!mesh->HasTextureCoords(unsigned(resolved.texture.coordinate))
            &&(resolved.texture.generation[0]!=1||resolved.texture.generation[1]!=1)){
            textureWarning("JT texture not applied: selected UV channel is absent and both S/T components are not generated");resolved.texture.type=0;
        }
        unsigned mat=material(resolved,colored,vertexAlpha);
        if(materials[mat]->GetTextureCount(aiTextureType_DIFFUSE)>0){
            const auto mappedImage=texture(resolved);
            const auto& t=resolved.texture.transform;bool identity=true;
            for(unsigned i=0;i<16;++i)identity=identity&&t[i]==float(i%5==0);
            const bool generate=resolved.texture.generation[0]!=0||resolved.texture.generation[1]!=0;
            if(!identity||generate||mappedImage.scaleS!=1||mappedImage.scaleT!=1){
                // Bake effective mapping into this appearance-specific copy.
                // This also carries shear/reflection through scene copy and GLB
                // without relying on a restricted scale/rotation material model.
                const auto channel=unsigned(resolved.texture.coordinate);
                auto* uv=mesh->mTextureCoords[channel];
                JtData_Limits::allocation(size_t(mesh->mNumVertices)*sizeof(aiVector3D));
                std::vector<aiVector3D> mapped(mesh->mNumVertices);bool valid=true;
                for(unsigned i=0;i<mesh->mNumVertices;++i){
                    if((i&1023)==0)options.check();
                    const auto component=[&](unsigned c){
                        if(resolved.texture.generation[c]==0)return double(c==0?uv[i].x:uv[i].y);
                        const auto* plane=resolved.texture.planes.data()+4*c;const auto& p=mesh->mVertices[i];
                        return double(plane[0])*p.x+double(plane[1])*p.y+double(plane[2])*p.z+plane[3];
                    };
                    const double baseS=component(0),baseT=component(1);
                    const double s=(baseS*t[0]+baseT*t[4]+t[12])*mappedImage.scaleS+mappedImage.offsetS;
                    const double v=(baseS*t[1]+baseT*t[5]+t[13])*mappedImage.scaleT+mappedImage.offsetT;
                    if(!std::isfinite(s)||!std::isfinite(v)||std::abs(s)>std::numeric_limits<float>::max()
                        ||std::abs(v)>std::numeric_limits<float>::max()){valid=false;break;}
                    mapped[i]={float(s),float(v),0};
                }
                if(valid){
                    // Pad lower absent slots for existing Assimp scene-copy and
                    // export paths, preserving the authored selected set number.
                    for(unsigned c=0;c<=channel;++c)if(!mesh->HasTextureCoords(c)){
                        JtData_Limits::allocation(size_t(mesh->mNumVertices)*sizeof(aiVector3D));
                        mesh->mTextureCoords[c]=new aiVector3D[mesh->mNumVertices]{};mesh->mNumUVComponents[c]=2;
                    }
                    std::copy(mapped.begin(),mapped.end(),mesh->mTextureCoords[channel]);
                }
                else {
                    textureWarning("JT texture not applied: transformed UV exceeds finite render range");
                    resolved.texture.type=0;mat=material(resolved,colored,vertexAlpha);
                }
            }
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
        mesh->mMaterialIndex=mat;mesh->mName=aiString(name.toUtf8().constData());
        meshes.push_back(std::move(mesh)); return unsigned(meshes.size()-1);
    }
    std::unique_ptr<aiNode> visit(const Handle(JtData_Object)& object,Appearance style={},bool parentHidden=false,bool transformLocked=false,const aiMatrix4x4& parentWorld=aiMatrix4x4(),PaletteContext palette={},MaterialSourceContext materialSource={}) {
        options.check();if(object.IsNull()) throw std::runtime_error("JT has an unresolved graph reference");
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
            const bool mappedChannel=!textureImage.IsNull()&&textureImage->Parsed()
                &&(textureImage->Channel()==0||textureImage->TextureType()==0||textureImage->TextureType()==26);
            if(!textureImage.IsNull()&&textureImage->Parsed()&&!mappedChannel)
                textureWarning("JT texture not applied: only explicit texture channel 0 is supported");
            const auto apply=[&](Appearance& style,aiMatrix4x4& matrix,bool& locked) {
                if(!appearance.IsNull())style.apply(*appearance);
                if(!drawStyle.IsNull())style.apply(*drawStyle);
                if(mappedChannel)style.texture.apply(textureImage);
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
        const unsigned mat=material(style);
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
                            for(const auto& batch:batches) {
                                const auto key=std::make_tuple(geometryIndex,batch.groups,batch.mat);
                                const auto cached=groupMeshCache.find(key);
                                const unsigned id=cached!=groupMeshCache.end()?cached->second:add(batch.faces.size()==geometries.at(geometryIndex)->mNumFaces?copyGeometry(geometryIndex):faceSubset(*geometries.at(geometryIndex),batch.faces),batch.style,name);
                                groupMeshCache[key]=id;
                                auto child=std::make_unique<aiNode>();child->mName=aiString((name+" / face groups "+QString::number(batch.groups.front())).toUtf8().constData());
                                child->mNumMeshes=1;child->mMeshes=new unsigned[1]{id};child->mTransformation=batch.local;
                                if(batch.hidden){result.hiddenNodes.insert(child.get());hiddenJournal.push_back(child.get());}
                                children.push_back(std::move(child));
                            }
                        } else {
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
        node->mNumMeshes=unsigned(attached.size());if(!attached.empty()) {node->mMeshes=new unsigned[attached.size()];std::copy(attached.begin(),attached.end(),node->mMeshes);}
        node->mNumChildren=unsigned(children.size());if(!children.empty()) {node->mChildren=new aiNode*[children.size()]{};for(size_t i=0;i<children.size();++i) {children[i]->mParent=node.get();node->mChildren[i]=children[i].release();}}
        const bool selectionNode=!switchNode.IsNull() || !Handle(JtNode_LOD)::DownCast(object).IsNull();
        auto sourceProperties=properties(*source,label);
        auto sourceTextures=textureDescriptors(*source,label);
        node->mMetaData=aiMetadata::Alloc(1+(selectionNode?1:0)+(lodDistance?1:0)+(sourceProperties?1:0)+(sourceTextures?1:0));
        node->mMetaData->Set(0,"JTRepresentation",aiString(exact ? "BRep" : attached.empty() ? "Assembly" : "StoredTessellation"));
        if(selectionNode) node->mMetaData->Set(1,"JTSelectedChild",int32_t(selectedChild));
        if(lodDistance) node->mMetaData->Set(2,"JTRangeLODDistance",*lodDistance);
        if(sourceProperties) node->mMetaData->Set(node->mMetaData->mNumProperties-1-(sourceTextures?1:0),"JTProperties",*sourceProperties);
        if(sourceTextures) node->mMetaData->Set(node->mMetaData->mNumProperties-1,"JTTextures",*sourceTextures);
        if(options.progress) options.progress("Building JT scene",int(nodes),0);
        return node;
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
