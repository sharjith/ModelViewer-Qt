#include "SceneBuilder.h"
#include "ArchiveReader.h"
#include "RepresentationReader.h"
#include <assimp/material.h>
#include <assimp/metadata.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace ThreeDXml {
namespace {
struct VertexKey {
    unsigned position;
    quint64 binding;
    bool operator==(const VertexKey& rhs) const { return position == rhs.position && binding == rhs.binding; }
};
struct VertexHash {
    size_t operator()(const VertexKey& key) const { return std::hash<quint64>{}((quint64(key.position)<<32)^key.binding); }
};
struct Primitive {
    unsigned rgb = 0;
    std::vector<aiVector3D> positions, normals;
    std::vector<std::array<unsigned,3>> triangles;
    std::unordered_map<VertexKey,unsigned,VertexHash> vertices;
};
struct BodyMeshes { QString path; std::vector<unsigned> meshes; };
void children(aiNode& parent, std::vector<std::unique_ptr<aiNode>>& nodes) {
    parent.mNumChildren = static_cast<unsigned>(nodes.size());
    parent.mChildren = new aiNode*[nodes.size()]{};
    for (size_t i = 0; i < nodes.size(); ++i) { nodes[i]->mParent = &parent; parent.mChildren[i] = nodes[i].release(); }
}
void attach(aiNode& node, const std::vector<unsigned>& meshes) {
    node.mNumMeshes = static_cast<unsigned>(meshes.size());
    node.mMeshes = new unsigned[meshes.size()]; std::copy(meshes.begin(),meshes.end(),node.mMeshes);
}
}

ImportResult SceneBuilder::build(const Document& doc, ArchiveReader& archive, const Options& options)
{
    ImportResult result; result.scene = std::make_unique<aiScene>();
    std::vector<std::unique_ptr<aiMesh>> meshes;
    std::vector<std::unique_ptr<aiMaterial>> materials;
    std::map<unsigned,unsigned> palette;
    auto material = [&](unsigned rgb) {
        auto found = palette.find(rgb); if (found != palette.end()) return found->second;
        auto mat = std::make_unique<aiMaterial>();
        aiString name(QStringLiteral("face_rgb_%1").arg(rgb,6,16,QLatin1Char('0')).toUtf8().constData());
        aiColor4D color(ai_real((rgb>>16)&255)/255,ai_real((rgb>>8)&255)/255,ai_real(rgb&255)/255,1);
        aiColor3D ambient(0,0,0), specular(.2f,.2f,.2f);
        ai_real metal = 0, rough = static_cast<ai_real>(std::sqrt(2.0/26)), shininess = 24, opacity = 1;
        int shading = aiShadingMode_PBR_BRDF;
        mat->AddProperty(&name,AI_MATKEY_NAME);
        mat->AddProperty(&color,1,AI_MATKEY_BASE_COLOR); mat->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        mat->AddProperty(&ambient,1,AI_MATKEY_COLOR_AMBIENT); mat->AddProperty(&specular,1,AI_MATKEY_COLOR_SPECULAR);
        mat->AddProperty(&metal,1,AI_MATKEY_METALLIC_FACTOR); mat->AddProperty(&rough,1,AI_MATKEY_ROUGHNESS_FACTOR);
        mat->AddProperty(&shininess,1,AI_MATKEY_SHININESS); mat->AddProperty(&opacity,1,AI_MATKEY_OPACITY);
        mat->AddProperty(&shading,1,AI_MATKEY_SHADING_MODEL);
        unsigned index = static_cast<unsigned>(materials.size()); palette[rgb] = index; materials.push_back(std::move(mat)); return index;
    };
    std::map<QString,std::vector<BodyMeshes>> resources;
    QStringList members;
    for (auto it = doc.representations.cbegin(); it != doc.representations.cend(); ++it) {
        if (it->format == QLatin1String("UVR")) members.append(it->member);
        else if (it->format == QLatin1String("TECHREP")) result.warnings.append(QStringLiteral("Skipped TECHREP review attachment: %1").arg(it->member));
        else throw Error(QStringLiteral("Unsupported 3DXML representation format: %1").arg(it->format));
    }
    members.removeDuplicates(); members.sort();
    for (int memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
        options.check(); const QString member = members[memberIndex];
        if (options.progress) options.progress(member, memberIndex, members.size());
        Geometry geometry;
        try { geometry = RepresentationReader::read(archive.read(member),options); }
        catch (const Error& e) { throw Error(QStringLiteral("%1: %2").arg(member,QString::fromUtf8(e.what()))); }
        std::vector<std::vector<unsigned>> bodyResources;
        for (const auto& body : geometry.bodies) {
            options.check();
            std::vector<Primitive> primitives; std::map<unsigned,size_t> byColor;
            for (size_t fi = 0; fi < body.faces.size(); ++fi) {
                options.check(); const auto& face = body.faces[fi];
                if (face.triangles.empty()) continue;
                if (!byColor.count(face.rgb)) { byColor[face.rgb] = primitives.size(); Primitive p; p.rgb = face.rgb; primitives.push_back(std::move(p)); }
                auto& p = primitives[byColor.at(face.rgb)];
                aiVector3D constant = face.normal;
                bool validConstant = face.constant && std::isfinite(constant.SquareLength()) && constant.SquareLength() > 0;
                if (validConstant) constant.Normalize();
                for (size_t ti = 0; ti < face.triangles.size(); ++ti) {
                    if ((ti & 4095) == 0) options.check();
                    const auto& tri = face.triangles[ti];
                    const aiVector3D a = body.positions[tri[0]], b = body.positions[tri[1]], c = body.positions[tri[2]];
                    // Compute fallback in double precision, including small source triangles.
                    const double ux=double(b.x)-a.x, uy=double(b.y)-a.y, uz=double(b.z)-a.z;
                    const double vx=double(c.x)-a.x, vy=double(c.y)-a.y, vz=double(c.z)-a.z;
                    const double x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx,len=std::sqrt(x*x+y*y+z*z);
                    if (!(len > 0) || !std::isfinite(len)) throw Error(QStringLiteral("Degenerate/nonfinite triangle in %1").arg(member));
                    aiVector3D flat(static_cast<ai_real>(x/len),static_cast<ai_real>(y/len),static_cast<ai_real>(z/len));
                    std::array<unsigned,3> output;
                    for (size_t corner = 0; corner < 3; ++corner) {
                        const unsigned source = tri[corner]; aiVector3D normal = flat;
                        // High bit distinguishes fallback triangle bindings from source pool/face bindings.
                        quint64 binding = (quint64(1)<<63) | (quint64(fi)<<32) | ti;
                        if (options.parsedNormals && validConstant) { normal=constant; binding=(quint64(1)<<62)|fi; }
                        else if (options.parsedNormals && !face.constant) {
                            if (source >= body.normals.size()) throw Error(QStringLiteral("Missing pool normal for triangle corner in %1").arg(member));
                            const auto n = body.normals[source];
                            if (std::isfinite(n.SquareLength()) && n.SquareLength() > 0) { normal=n; binding=0; }
                        }
                        VertexKey key{source,binding};
                        auto found = p.vertices.find(key);
                        if (found == p.vertices.end()) {
                            unsigned index = static_cast<unsigned>(p.positions.size());
                            p.vertices.emplace(key,index); p.positions.push_back(body.positions[source]); p.normals.push_back(normal); output[corner]=index;
                        } else output[corner]=found->second;
                    }
                    p.triangles.push_back(output);
                }
            }
            std::vector<unsigned> bodyMeshes;
            for (auto& p : primitives) {
                auto mesh = std::make_unique<aiMesh>();
                mesh->mName = aiString(QStringLiteral("%1__body_%2").arg(member).arg(body.offset).toUtf8().constData());
                mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE; mesh->mMaterialIndex=material(p.rgb);
                mesh->mNumVertices=static_cast<unsigned>(p.positions.size()); mesh->mVertices=new aiVector3D[p.positions.size()]; mesh->mNormals=new aiVector3D[p.normals.size()];
                std::copy(p.positions.begin(),p.positions.end(),mesh->mVertices); std::copy(p.normals.begin(),p.normals.end(),mesh->mNormals);
                mesh->mNumFaces=static_cast<unsigned>(p.triangles.size()); mesh->mFaces=new aiFace[p.triangles.size()];
                for (size_t i=0;i<p.triangles.size();++i) { mesh->mFaces[i].mNumIndices=3; mesh->mFaces[i].mIndices=new unsigned[3]; std::copy(p.triangles[i].begin(),p.triangles[i].end(),mesh->mFaces[i].mIndices); }
                bodyMeshes.push_back(static_cast<unsigned>(meshes.size())); meshes.push_back(std::move(mesh));
            }
            bodyResources.push_back(std::move(bodyMeshes));
        }
        auto& group = resources[member];
        for (const auto& occurrence : geometry.occurrences) group.push_back({occurrence.path,bodyResources.at(occurrence.body)});
    }
    QSet<QString> active, visitedHidden; size_t nodeCount=0;
    std::function<std::unique_ptr<aiNode>(const QString&,const QString&,const QString&,const QString&,const aiMatrix4x4&,bool,int)> build;
    build = [&](const QString& reference,const QString& name,const QString& path,const QString& id,const aiMatrix4x4& local,bool parentVisible,int depth) {
        options.check();
        if (depth > 128 || ++nodeCount > 100000 || active.contains(reference)) throw Error(QStringLiteral("Cyclic/excessive product graph at %1").arg(path));
        active.insert(reference); const auto& ref=doc.references[reference];
        bool visible=parentVisible && !doc.hiddenPaths.contains(path);
        if (doc.hiddenPaths.contains(path)) visitedHidden.insert(path);
        auto node=std::make_unique<aiNode>(name.isEmpty() ? ref.name.toUtf8().constData() : name.toUtf8().constData()); node->mTransformation=local;
        node->mMetaData=aiMetadata::Alloc(3);
        node->mMetaData->Set(0,"3DXMLReference",aiString(reference.toUtf8().constData()));
        node->mMetaData->Set(1,"3DXMLPath",aiString(path.toUtf8().constData()));
        node->mMetaData->Set(2,"3DXMLInstance",aiString(id.toUtf8().constData()));
        result.occurrencePaths.insert(node.get(),path);
        if (!visible) result.hiddenNodes.insert(node.get());
        std::vector<std::unique_ptr<aiNode>> nodes;
        for (const auto& repId : ref.representations) {
            const auto rep=doc.representations[repId];
            if (rep.format != QLatin1String("UVR")) continue;
            const auto& bodies=resources.at(rep.member);
            if (bodies.size()==1 && ref.representations.size()==1) attach(*node,bodies.front().meshes);
            else for (const auto& body : bodies) {
                if (++nodeCount > 100000) throw Error(QStringLiteral("Expanded body node limit exceeded"));
                auto child=std::make_unique<aiNode>(QStringLiteral("%1__body_%2").arg(rep.name,body.path).toUtf8().constData());
                attach(*child,body.meshes);
                if (!visible) result.hiddenNodes.insert(child.get());
                nodes.push_back(std::move(child));
            }
        }
        for (const auto& childId : ref.instances) {
            const auto child=doc.instances[childId];
            nodes.push_back(build(child.reference,child.name,path.isEmpty() ? child.id : path+'/'+child.id,child.id,child.local,visible,depth+1));
        }
        children(*node,nodes); active.remove(reference); return node;
    };
    result.scene->mRootNode=build(doc.root,doc.references[doc.root].name,{},{},aiMatrix4x4(),true,0).release();
    if (visitedHidden != doc.hiddenPaths) throw Error(QStringLiteral("Unresolved visibility occurrence path"));
    // Transfer ownership only after all potentially throwing allocations succeed.
    result.scene->mMeshes=new aiMesh*[meshes.size()]{};
    result.scene->mNumMeshes=static_cast<unsigned>(meshes.size());
    for (size_t i=0;i<meshes.size();++i) result.scene->mMeshes[i]=meshes[i].release();
    result.scene->mMaterials=new aiMaterial*[materials.size()]{};
    result.scene->mNumMaterials=static_cast<unsigned>(materials.size());
    for (size_t i=0;i<materials.size();++i) result.scene->mMaterials[i]=materials[i].release();
    if (!result.scene->mNumMeshes) throw Error(QStringLiteral("3DXML contains no supported triangle geometry"));
    if (options.progress) options.progress(QStringLiteral("Complete"),members.size(),members.size());
    return result;
}
}
