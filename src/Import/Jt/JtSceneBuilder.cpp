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
#include <assimp/material.h>
#include <cmath>
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
std::unique_ptr<aiMesh> storedMesh(const Handle(JtElement_ShapeLOD_Vertex)& source) {
    const auto& p=source->Vertices();const auto& n=source->Normals();const auto& ix=source->Indices();
    if(p.Count()<=0 || p.CompCount()!=3 || ix.Count()<=0 || ix.Count()%3)
        throw std::runtime_error("Unsupported/empty JT tessellation layout");
    auto out=std::make_unique<aiMesh>();out->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    out->mNumVertices=unsigned(p.Count());out->mNumFaces=unsigned(ix.Count()/3);
    out->mVertices=new aiVector3D[out->mNumVertices];out->mNormals=new aiVector3D[out->mNumVertices]{};
    const bool storedNormals=n.Count()==p.Count() && n.CompCount()==3;
    for(unsigned i=0;i<out->mNumVertices;++i) {out->mVertices[i]={p[i][0],p[i][1],p[i][2]};if(storedNormals) out->mNormals[i]={n[i][0],n[i][1],n[i][2]};}
    out->mFaces=new aiFace[out->mNumFaces];
    for(unsigned i=0;i<out->mNumFaces;++i) {
        auto& f=out->mFaces[i];f.mNumIndices=3;f.mIndices=new unsigned[3];
        for(unsigned j=0;j<3;++j) {const int index=ix[3*i+j];if(index<0 || unsigned(index)>=out->mNumVertices) throw std::runtime_error("JT tessellation index out of range");f.mIndices[j]=unsigned(index);}
        if(!storedNormals) {auto v=(out->mVertices[f.mIndices[1]]-out->mVertices[f.mIndices[0]])^(out->mVertices[f.mIndices[2]]-out->mVertices[f.mIndices[0]]);for(unsigned j=0;j<3;++j)out->mNormals[f.mIndices[j]]+=v;}
    }
    for(unsigned i=0;i<out->mNumVertices;++i) {
        if(!storedNormals && out->mNormals[i].SquareLength()<1e-20f) out->mNormals[i]={0,0,1};
        out->mNormals[i].Normalize();
    }
    validate(*out);return out;
}
struct Appearance {
    aiColor4D diffuse{0.75f,0.75f,0.78f,1};
    aiColor3D ambient{},specular{},emission{};
    float bumpiness=1,reflectivity=0;
    unsigned finalFields=0,present=0;
    unsigned blendFlags=0;
    void apply(const JtAttribute_Material& source) {
        const unsigned allowed=~source.FieldInhibitFlags() & (source.AccumulationForced() ? ~0u : ~finalFields);
        const auto rgb=[](const float* c){return aiColor3D(c[0],c[1],c[2]);};
        const auto c=source.DiffuseColor();
        if(allowed&(1u<<0)) ambient=rgb(source.AmbientColor());
        if(allowed&(1u<<1)) specular=rgb(source.SpecularColor());
        if(allowed&(1u<<2)) emission=rgb(source.EmissionColor());
        if(allowed&(1u<<3)) blendFlags=source.DataFlags()&0xffd0u;
        if(allowed&(1u<<5)) reflectivity=source.Reflectivity();
        if(allowed&(1u<<6)) {diffuse.r=c[0];diffuse.g=c[1];diffuse.b=c[2];}
        if(allowed&(1u<<7)) diffuse.a=c[3];
        if(allowed&(1u<<8)) bumpiness=source.Bumpiness();
        present|=allowed&0x1efu;
        finalFields|=source.FieldFinalFlags()&allowed;
    }
};
struct Builder {
    const Options& options; QString rootDirectory;
    ImportResult result;
    std::vector<std::unique_ptr<aiMesh>> meshes;
    std::vector<std::unique_ptr<aiMaterial>> materials;
    std::set<const void*> active;
    std::set<QString> activeFiles;
    std::map<QString,Handle(JtNode_Partition)> partitions;
    std::map<std::pair<const void*,unsigned>,unsigned> meshCache;
    std::map<std::vector<float>,unsigned> materialCache;
    std::map<const void*,unsigned> geometryCache;
    std::vector<const aiNode*> hiddenJournal;
    unsigned nodes=0;
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
    struct Checkpoint {
        size_t meshes,materials,shapes,hidden;
        qsizetype warnings;
        unsigned stored,exact,fallback;
    };
    Checkpoint checkpoint() const {return {meshes.size(),materials.size(),result.exactShapes.size(),hiddenJournal.size(),result.warnings.size(),result.storedMeshes,result.exactParts,result.fallbackParts};}
    void rollback(const Checkpoint& c) {
        meshes.resize(c.meshes);materials.resize(c.materials);result.exactShapes.resize(c.shapes);
        while(result.warnings.size()>c.warnings) result.warnings.removeLast();
        while(hiddenJournal.size()>c.hidden) {result.hiddenNodes.remove(hiddenJournal.back());hiddenJournal.pop_back();}
        for(auto it=meshCache.begin();it!=meshCache.end();) {if(it->second>=c.meshes) it=meshCache.erase(it);else ++it;}
        for(auto it=geometryCache.begin();it!=geometryCache.end();) {if(it->second>=c.meshes) it=geometryCache.erase(it);else ++it;}
        for(auto it=materialCache.begin();it!=materialCache.end();) {if(it->second>=c.materials) it=materialCache.erase(it);else ++it;}
        result.storedMeshes=c.stored;result.exactParts=c.exact;result.fallbackParts=c.fallback;
    }
    unsigned material(const Appearance& style) {
        const auto c=style.diffuse;
        const bool blending=(style.blendFlags&0x10)!=0;
        const unsigned src=(style.blendFlags>>6)&31,dst=(style.blendFlags>>11)&31;
        const float opacity=(!blending || (src==1 && dst==0)) ? 1.0f : c.a;
        std::vector<float> key{c.r,c.g,c.b,opacity,style.ambient.r,style.ambient.g,style.ambient.b,
            style.specular.r,style.specular.g,style.specular.b,style.emission.r,style.emission.g,style.emission.b,
            style.bumpiness,style.reflectivity,float(style.present)};
        for(float value:key) if(!std::isfinite(value)) throw std::runtime_error("JT has nonfinite material values");
        if(!std::isfinite(c.a) || c.a<0 || c.a>1 || opacity<0 || opacity>1) throw std::runtime_error("JT diffuse alpha is out of range");
        const auto found=materialCache.find(key);if(found!=materialCache.end()) return found->second;
        if(blending && !(src==6 && dst==7) && !(src==1 && dst==0))
            result.warnings.append("JT blend factors are approximated using conventional alpha blending");
        auto m=std::make_unique<aiMaterial>();const aiColor4D color{c.r,c.g,c.b,1};
        m->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);m->AddProperty(&opacity,1,AI_MATKEY_OPACITY);
        if(style.present&(1u<<0)) m->AddProperty(&style.ambient,1,AI_MATKEY_COLOR_AMBIENT);
        if(style.present&(1u<<1)) m->AddProperty(&style.specular,1,AI_MATKEY_COLOR_SPECULAR);
        if(style.present&(1u<<2)) m->AddProperty(&style.emission,1,AI_MATKEY_COLOR_EMISSIVE);
        if(style.present&(1u<<8)) m->AddProperty(&style.bumpiness,1,AI_MATKEY_BUMPSCALING);
        if(style.present&(1u<<5)) m->AddProperty(&style.reflectivity,1,AI_MATKEY_REFLECTIVITY);
        const unsigned id=unsigned(materials.size());materials.push_back(std::move(m));materialCache.emplace(std::move(key),id);return id;
    }
    std::unique_ptr<aiMesh> copyGeometry(unsigned index) {
        const auto& source=*meshes.at(index);auto copy=std::make_unique<aiMesh>();
        copy->mPrimitiveTypes=source.mPrimitiveTypes;copy->mNumVertices=source.mNumVertices;copy->mNumFaces=source.mNumFaces;
        copy->mVertices=new aiVector3D[copy->mNumVertices];copy->mNormals=new aiVector3D[copy->mNumVertices];
        std::copy_n(source.mVertices,copy->mNumVertices,copy->mVertices);std::copy_n(source.mNormals,copy->mNumVertices,copy->mNormals);
        copy->mFaces=new aiFace[copy->mNumFaces];
        for(unsigned i=0;i<copy->mNumFaces;++i) {copy->mFaces[i].mNumIndices=3;copy->mFaces[i].mIndices=new unsigned[3];std::copy_n(source.mFaces[i].mIndices,3,copy->mFaces[i].mIndices);}
        return copy;
    }
    unsigned add(std::unique_ptr<aiMesh> mesh,unsigned mat,const QString& name) {
        mesh->mMaterialIndex=mat;mesh->mName=aiString(name.toUtf8().constData());
        meshes.push_back(std::move(mesh)); return unsigned(meshes.size()-1);
    }
    std::unique_ptr<aiNode> visit(const Handle(JtData_Object)& object,Appearance style={},bool parentHidden=false,bool transformLocked=false,const aiMatrix4x4& parentWorld=aiMatrix4x4()) {
        options.check();if(object.IsNull()) throw std::runtime_error("JT has an unresolved graph reference");
        if(active.size()>=512 || ++nodes>1000000 || !active.insert(object.get()).second) throw std::runtime_error("JT graph cycle/depth limit exceeded");
        struct Pop {std::set<const void*>& s;const void* p;~Pop(){s.erase(p);}} pop{active,object.get()};
        auto source=Handle(JtNode_Base)::DownCast(object);
        if(source.IsNull()) throw std::runtime_error("Unsupported required JT graph node");
        const bool hidden=parentHidden || source->Ignored();
        const auto name=nameOf(source);const auto label=name.isEmpty()?QStringLiteral("Unnamed JT node"):name;auto node=std::make_unique<aiNode>();node->mName=aiString(name.toUtf8().constData());
        for(size_t i=0;i<source->Attributes().Count();++i) {
            const auto a=source->Attributes()[i];
            const auto attribute=Handle(JtAttribute_Base)::DownCast(a);
            if(!attribute.IsNull() && attribute->AccumulationIgnored()) continue;
            const auto transform=Handle(JtAttribute_GeometricTransform)::DownCast(a);
            if(!transform.IsNull() && (!transformLocked || transform->AccumulationForced()) && !(transform->FieldInhibitFlags()&1)) {
                const auto t=transform->GetTrsf();
                for(unsigned j=0;j<16;++j) if(!std::isfinite(float(t[j]))) throw std::runtime_error("JT has nonfinite transform values");
                if(t[3]!=0 || t[7]!=0 || t[11]!=0 || t[15]!=1) throw std::runtime_error("Unsupported projective JT transform");
                transformLocked=transformLocked || (transform->FieldFinalFlags()&1);
                node->mTransformation*=aiMatrix4x4(float(t[0]),float(t[4]),float(t[8]),float(t[12]),float(t[1]),float(t[5]),float(t[9]),float(t[13]),float(t[2]),float(t[6]),float(t[10]),float(t[14]),float(t[3]),float(t[7]),float(t[11]),float(t[15]));
            }
            const auto appearance=Handle(JtAttribute_Material)::DownCast(a);
            if(!appearance.IsNull()) style.apply(*appearance);
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
            if(!options.exactDecoder) continue;
            try {
                options.check();prop->Load();const auto xt=Handle(JtElement_XTBRep)::DownCast(prop->DefferedObject());
                if(xt.IsNull()) throw std::runtime_error("Unsupported exact JT payload");
                const auto& bytes=xt->Data();const auto shape=options.exactDecoder(QByteArray(reinterpret_cast<const char*>(bytes.Data()),int(bytes.Count())));
                options.check();auto m=exactMesh(shape,options);attached.push_back(add(std::move(m),mat,name));result.exactShapes.push_back(shape);++result.exactParts;exact=true;prop->Unload();break;
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
                    const auto key=std::make_pair(static_cast<const void*>(prop.get()),mat); const auto cached=meshCache.find(key);
                    if(cached!=meshCache.end()) {attached.push_back(cached->second);loaded=true;break;}
                    try {
                        const auto geometry=geometryCache.find(prop.get());
                        if(geometry!=geometryCache.end()) {
                            const unsigned id=add(copyGeometry(geometry->second),mat,name);meshCache[key]=id;attached.push_back(id);++result.storedMeshes;loaded=true;break;
                        }
                        struct Unload {Handle(JtProperty_LateLoaded) p;~Unload(){p->Unload();}} unload{prop};
                        options.check();prop->Load();const auto mesh=Handle(JtElement_ShapeLOD_Vertex)::DownCast(prop->DefferedObject());
                        if(mesh.IsNull()) throw std::runtime_error("Unsupported JT tessellation payload");
                        auto output=storedMesh(mesh);const unsigned id=add(std::move(output),mat,name);meshCache[key]=id;geometryCache[prop.get()]=id;attached.push_back(id);++result.storedMeshes;loaded=true;break;
                    } catch(const Cancelled&) {throw;}
                      catch(const std::bad_alloc&) {throw;}
                      catch(const Standard_Failure& e) {result.warnings.append(label+": display alternative rejected: "+QString::fromUtf8(e.GetMessageString()));}
                      catch(const std::exception& e) {result.warnings.append(label+": display alternative rejected: "+QString::fromUtf8(e.what()));}
                }
                if(!loaded) throw std::runtime_error((label+": no supported JT display tessellation").toStdString());
            }
            const auto instance=Handle(JtNode_Instance)::DownCast(object);
            if(!instance.IsNull()) children.push_back(visit(instance->Object(),style,hidden,transformLocked,world));
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
                        children.push_back(visit(root,style,hidden,transformLocked,world));
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
                            children.push_back(visit(group->Children()[selected],style,hidden,transformLocked,world));
                            selectedChild=int(selected);
                        }
                    } else {
                        bool selected=false;
                        for(size_t attempt=0;attempt<childCount;++attempt) {
                            // Preferred, then the remaining source-order alternatives.
                            const size_t i=!lod ? attempt : attempt==0 ? preferred : attempt<=preferred ? attempt-1 : attempt;
                            if(!lod) {children.push_back(visit(group->Children()[i],style,hidden,transformLocked,world));continue;}
                            const auto saved=checkpoint();
                            try {
                                auto child=visit(group->Children()[i],style,hidden,transformLocked,world);
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
                        if(lod && !selected) throw std::runtime_error((label+": no usable JT LOD alternative").toStdString());
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
        node->mMetaData=aiMetadata::Alloc(1+(selectionNode?1:0)+(lodDistance?1:0)+(sourceProperties?1:0));
        node->mMetaData->Set(0,"JTRepresentation",aiString(exact ? "BRep" : attached.empty() ? "Assembly" : "StoredTessellation"));
        if(selectionNode) node->mMetaData->Set(1,"JTSelectedChild",int32_t(selectedChild));
        if(lodDistance) node->mMetaData->Set(2,"JTRangeLODDistance",*lodDistance);
        if(sourceProperties) node->mMetaData->Set(node->mMetaData->mNumProperties-1,"JTProperties",*sourceProperties);
        if(options.progress) options.progress("Building JT scene",int(nodes),0);
        return node;
    }
};
}
ImportResult Detail::buildScene(const Handle(JtNode_Partition)& root,const QString& path,const Options& options) {
    std::unique_ptr<JtData_Limits::Scope> scope;
    if(!JtData_Limits::check) scope=std::make_unique<JtData_Limits::Scope>([&]{options.check();});
    options.check();const QFileInfo info(path);
    if(options.lodEyePoint) for(double coordinate:*options.lodEyePoint)
        if(!std::isfinite(coordinate)) throw std::runtime_error("Invalid JT RangeLOD eye point");
    Builder b{options,info.canonicalPath()};b.activeFiles.insert(fileKey(info.canonicalFilePath()));b.material(Appearance{});
    auto tree=b.visit(root);if(b.meshes.empty()) throw std::runtime_error("JT contains no supported tessellation and no validated exact geometry");
    b.result.scene=std::make_unique<aiScene>();auto& scene=*b.result.scene;
    scene.mRootNode=tree.release();scene.mNumMeshes=unsigned(b.meshes.size());scene.mMeshes=new aiMesh*[scene.mNumMeshes]{};
    for(unsigned i=0;i<scene.mNumMeshes;++i) scene.mMeshes[i]=b.meshes[i].release();
    scene.mNumMaterials=unsigned(b.materials.size());scene.mMaterials=new aiMaterial*[scene.mNumMaterials]{};
    for(unsigned i=0;i<scene.mNumMaterials;++i) scene.mMaterials[i]=b.materials[i].release();
    options.check();return std::move(b.result);
}
}
