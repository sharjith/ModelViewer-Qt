#include "Jt/JtImporter.h"
#include <JtData_Model.hxx>
#include <JtData_Limits.hxx>
#include <JtNode_Partition.hxx>
#include <JtNode_Part.hxx>
#include <JtNode_Instance.hxx>
#include <JtNode_LOD.hxx>
#include <JtNode_Shape_Base.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <JtElement_XTBRep.hxx>
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
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

namespace Jt {
namespace {
QString nameOf(const Handle(JtNode_Base)& node) {
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(node->Name().ToExtString()));
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
struct Builder {
    const Options& options; QString rootDirectory;
    ImportResult result;
    std::vector<std::unique_ptr<aiMesh>> meshes;
    std::vector<std::unique_ptr<aiMaterial>> materials;
    std::set<const void*> active;
    std::set<QString> activeFiles;
    std::map<QString,Handle(JtNode_Partition)> partitions;
    std::map<std::pair<const void*,unsigned>,unsigned> meshCache;
    unsigned nodes=0;
    unsigned material(aiColor4D color) {
        if(!std::isfinite(color.r)||!std::isfinite(color.g)||!std::isfinite(color.b)||!std::isfinite(color.a))
            throw std::runtime_error("JT has nonfinite material values");
        // Keep opacity in one property: exporters may multiply diffuse alpha
        // by AI_MATKEY_OPACITY when both contain the source alpha.
        const float opacity=color.a; color.a=1.0f;
        auto m=std::make_unique<aiMaterial>();m->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        m->AddProperty(&opacity,1,AI_MATKEY_OPACITY);
        materials.push_back(std::move(m));return unsigned(materials.size()-1);
    }
    unsigned add(std::unique_ptr<aiMesh> mesh,unsigned mat,const QString& name) {
        mesh->mMaterialIndex=mat;mesh->mName=aiString(name.toUtf8().constData());
        meshes.push_back(std::move(mesh)); return unsigned(meshes.size()-1);
    }
    std::unique_ptr<aiNode> visit(const Handle(JtData_Object)& object,aiColor4D color,unsigned inheritedMaterial=0,bool parentHidden=false) {
        options.check();if(object.IsNull()) throw std::runtime_error("JT has an unresolved graph reference");
        if(active.size()>=512 || ++nodes>1000000 || !active.insert(object.get()).second) throw std::runtime_error("JT graph cycle/depth limit exceeded");
        struct Pop {std::set<const void*>& s;const void* p;~Pop(){s.erase(p);}} pop{active,object.get()};
        auto source=Handle(JtNode_Base)::DownCast(object);
        if(source.IsNull()) throw std::runtime_error("Unsupported required JT graph node");
        const bool hidden=parentHidden || source->Ignored();
        const auto name=nameOf(source);auto node=std::make_unique<aiNode>();node->mName=aiString(name.toUtf8().constData());
        unsigned mat=inheritedMaterial;
        for(size_t i=0;i<source->Attributes().Count();++i) {
            const auto a=source->Attributes()[i]; const auto transform=Handle(JtAttribute_GeometricTransform)::DownCast(a);
            if(!transform.IsNull()) {
                const auto t=transform->GetTrsf();
                for(unsigned j=0;j<16;++j) if(!std::isfinite(float(t[j]))) throw std::runtime_error("JT has nonfinite transform values");
                node->mTransformation*=aiMatrix4x4(float(t[0]),float(t[4]),float(t[8]),float(t[12]),float(t[1]),float(t[5]),float(t[9]),float(t[13]),float(t[2]),float(t[6]),float(t[10]),float(t[14]),float(t[3]),float(t[7]),float(t[11]),float(t[15]));
            }
            const auto appearance=Handle(JtAttribute_Material)::DownCast(a);
            if(!appearance.IsNull()) {const auto c=appearance->DiffuseColor();color={c[0],c[1],c[2],appearance->BlendingEnabled() ? c[3] : 1.0f};mat=material(color);}
        }
        std::vector<unsigned> attached;std::vector<std::unique_ptr<aiNode>> children;
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
              catch(const Standard_Failure& e) {result.warnings.append(name+": exact geometry rejected: "+QString::fromUtf8(e.GetMessageString()));}
              catch(const std::exception& e) {result.warnings.append(name+": exact geometry rejected: "+QString::fromUtf8(e.what()));}
            prop->Unload();
        }
        if(hasBrep && !exact) {++result.fallbackParts;result.warnings.append(name+": using stored JT tessellation (exact geometry unavailable or rejected)");}
        if(!exact) {
            const auto shape=Handle(JtNode_Shape_Base)::DownCast(object);
            if(!shape.IsNull()) {
                bool loaded=false;
                for(size_t i=0;i<shape->LateLoads().Count();++i) {
                    const auto prop=shape->LateLoads()[i];if(prop->SegmentType()<6 || prop->SegmentType()>16) continue;
                    const auto key=std::make_pair(static_cast<const void*>(prop.get()),mat); const auto cached=meshCache.find(key);
                    if(cached!=meshCache.end()) {attached.push_back(cached->second);loaded=true;break;}
                    options.check();prop->Load();const auto mesh=Handle(JtElement_ShapeLOD_Vertex)::DownCast(prop->DefferedObject());
                    if(mesh.IsNull()) {prop->Unload();continue;}
                    auto output=storedMesh(mesh);prop->Unload();const unsigned id=add(std::move(output),mat,name);meshCache[key]=id;attached.push_back(id);++result.storedMeshes;loaded=true;break;
                }
                if(!loaded) throw std::runtime_error((name+": no supported JT display tessellation").toStdString());
            }
            const auto instance=Handle(JtNode_Instance)::DownCast(object);
            if(!instance.IsNull()) children.push_back(visit(instance->Object(),color,mat,hidden));
            const auto group=Handle(JtNode_Group)::DownCast(object);
            if(!group.IsNull()) {
                const auto partition=Handle(JtNode_Partition)::DownCast(object);
                if(!partition.IsNull() && group->Children().IsEmpty()) {
                    const QString reference=QString::fromUtf16(reinterpret_cast<const char16_t*>(partition->FileName().ToExtString()));
                    if(!reference.isEmpty()) {
                        const QString origin=QString::fromUtf16(reinterpret_cast<const char16_t*>(partition->OriginFileName().ToExtString()));
                        const QString file=QFileInfo(QFileInfo(origin).dir().filePath(reference)).canonicalFilePath();
                        if(file.isEmpty() || !file.startsWith(rootDirectory+"/",Qt::CaseInsensitive) || activeFiles.count(file)) throw std::runtime_error("Missing, cyclic or external JT partition");
                        activeFiles.insert(file);
                        auto& root=partitions[file];
                        if(root.IsNull()) {Handle(JtData_Model) m=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(file.utf16())));root=m->Init();}
                        children.push_back(visit(root,color,mat,hidden));activeFiles.erase(file);
                    }
                } else {
                    const bool lod=!Handle(JtNode_LOD)::DownCast(object).IsNull();
                    const size_t count=lod ? (std::min)(size_t(1),size_t(group->Children().Count())) : group->Children().Count();
                    for(size_t i=0;i<count;++i) children.push_back(visit(group->Children()[i],color,mat,hidden));
                }
            }
        }
        if(hasBrep && !exact && attached.empty() && std::none_of(children.begin(),children.end(),[](const auto& child){return containsMesh(*child);}))
            throw std::runtime_error("JT part has B-Rep but no usable tessellation fallback");
        if(hidden) result.hiddenNodes.insert(node.get());
        node->mNumMeshes=unsigned(attached.size());if(!attached.empty()) {node->mMeshes=new unsigned[attached.size()];std::copy(attached.begin(),attached.end(),node->mMeshes);}
        node->mNumChildren=unsigned(children.size());if(!children.empty()) {node->mChildren=new aiNode*[children.size()]{};for(size_t i=0;i<children.size();++i) {children[i]->mParent=node.get();node->mChildren[i]=children[i].release();}}
        node->mMetaData=aiMetadata::Alloc(1);node->mMetaData->Set(0,"JTRepresentation",aiString(exact ? "BRep" : "StoredTessellation"));
        if(options.progress) options.progress("Building JT scene",int(nodes),0);
        return node;
    }
};
}
ImportResult Importer::read(const QString& path,const Options& options) try {
    JtData_Limits::Scope scope([&]{options.check();});
    options.check();const QFileInfo info(path);if(!info.exists() || info.size()<105) throw std::runtime_error("Cannot read JT header: file missing or truncated");
    Builder b{options,info.canonicalPath()};b.activeFiles.insert(info.canonicalFilePath());b.material({0.75f,0.75f,0.78f,1});
    Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(info.absoluteFilePath().utf16())));
    const auto root=model->Init();if(root.IsNull()) throw std::runtime_error("Cannot decode JT product structure");
    auto tree=b.visit(root,{0.75f,0.75f,0.78f,1});if(b.meshes.empty()) throw std::runtime_error("JT contains no supported tessellation and no validated exact geometry");
    b.result.scene=std::make_unique<aiScene>();auto& scene=*b.result.scene;
    scene.mRootNode=tree.release();scene.mNumMeshes=unsigned(b.meshes.size());scene.mMeshes=new aiMesh*[scene.mNumMeshes]{};
    for(unsigned i=0;i<scene.mNumMeshes;++i) scene.mMeshes[i]=b.meshes[i].release();
    scene.mNumMaterials=unsigned(b.materials.size());scene.mMaterials=new aiMaterial*[scene.mNumMaterials]{};
    for(unsigned i=0;i<scene.mNumMaterials;++i) scene.mMaterials[i]=b.materials[i].release();
    options.check();return std::move(b.result);
} catch(const Standard_Failure& error) {
    throw std::runtime_error(std::string("JT kernel error: ")+error.GetMessageString());
}
}
