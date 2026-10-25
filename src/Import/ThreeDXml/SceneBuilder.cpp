#include "SceneBuilder.h"
#include "ArchiveReader.h"
#include "RepresentationReader.h"
#include "TessellatedReader.h"
#include <assimp/material.h>
#include <assimp/metadata.h>
#include <assimp/SceneCombiner.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

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
    ai_real alpha = 1;
    std::vector<aiVector3D> positions, normals;
    std::vector<std::array<unsigned,3>> triangles;
    std::unordered_map<VertexKey,unsigned,VertexHash> vertices;
    std::unordered_set<quint64> edgeHits; // edge polyline segments that are edges of this primitive's own triangles
};
quint64 segmentKey(unsigned a, unsigned b) { return (quint64(std::min(a,b))<<32) | std::max(a,b); }
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
    std::map<std::pair<unsigned,ai_real>,unsigned> palette;
    auto material = [&](unsigned rgb, ai_real alpha = 1) {
        const auto key=std::make_pair(rgb,alpha);
        auto found = palette.find(key); if (found != palette.end()) return found->second;
        auto mat = std::make_unique<aiMaterial>();
        aiString name(QStringLiteral("face_rgb_%1").arg(rgb,6,16,QLatin1Char('0')).toUtf8().constData());
        aiColor4D color(ai_real((rgb>>16)&255)/255,ai_real((rgb>>8)&255)/255,ai_real(rgb&255)/255,alpha);
        aiColor3D ambient(0,0,0), specular(.2f,.2f,.2f);
        ai_real metal = 0, rough = static_cast<ai_real>(std::sqrt(2.0/26)), shininess = 24, opacity = alpha;
        int shading = aiShadingMode_PBR_BRDF;
        mat->AddProperty(&name,AI_MATKEY_NAME);
        mat->AddProperty(&color,1,AI_MATKEY_BASE_COLOR); mat->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        mat->AddProperty(&ambient,1,AI_MATKEY_COLOR_AMBIENT); mat->AddProperty(&specular,1,AI_MATKEY_COLOR_SPECULAR);
        mat->AddProperty(&metal,1,AI_MATKEY_METALLIC_FACTOR); mat->AddProperty(&rough,1,AI_MATKEY_ROUGHNESS_FACTOR);
        mat->AddProperty(&shininess,1,AI_MATKEY_SHININESS); mat->AddProperty(&opacity,1,AI_MATKEY_OPACITY);
        mat->AddProperty(&shading,1,AI_MATKEY_SHADING_MODEL);
        unsigned index = static_cast<unsigned>(materials.size()); palette[key] = index; materials.push_back(std::move(mat)); return index;
    };
    // Material from a CATIA rendering definition. An occurrence colour, when present, replaces only the base colour.
    std::map<std::pair<const MaterialRendering*,qint64>,unsigned> renderedPalette;
    auto renderedMaterial = [&](const MaterialRendering& source, const aiColor4D* colorOverride) {
        const qint64 overrideKey = colorOverride ? qint64(std::lround(colorOverride->r*255))<<24 | qint64(std::lround(colorOverride->g*255))<<16
            | qint64(std::lround(colorOverride->b*255))<<8 | qint64(std::lround(colorOverride->a*255)) : -1;
        const auto key = std::make_pair(&source, overrideKey);
        auto found = renderedPalette.find(key); if (found != renderedPalette.end()) return found->second;
        auto clamp01 = [](double v) { return ai_real(std::min(1.0, std::max(0.0, v))); };
        const ai_real opacity = clamp01((1 - source.transparency) * (colorOverride ? colorOverride->a : 1));
        aiColor4D color(clamp01(source.diffuse.r*source.diffuseCoef), clamp01(source.diffuse.g*source.diffuseCoef), clamp01(source.diffuse.b*source.diffuseCoef), opacity);
        if (colorOverride) color = aiColor4D(colorOverride->r, colorOverride->g, colorOverride->b, opacity);
        aiColor3D ambient(0,0,0), emissive(clamp01(source.emissive.r*source.emissiveCoef), clamp01(source.emissive.g*source.emissiveCoef), clamp01(source.emissive.b*source.emissiveCoef));
        aiColor3D specular(clamp01(source.specular.r*source.specularCoef), clamp01(source.specular.g*source.specularCoef), clamp01(source.specular.b*source.specularCoef));
        // Published relation: 3dxml exponent = 1 - (normalised OpenGL exponent)^3; OpenGL exponents run to 128.
        const double normalised = std::cbrt(std::max(0.0, 1 - source.specularExponent));
        ai_real shininess = ai_real(std::max(1.0, normalised * 128)), metal = 0;
        ai_real rough = static_cast<ai_real>(std::sqrt(2.0 / (double(shininess) + 2)));
        auto mat = std::make_unique<aiMaterial>();
        aiString name(QStringLiteral("rendering_material_%1").arg(renderedPalette.size()).toUtf8().constData());
        int shading = aiShadingMode_PBR_BRDF; ai_real opacityValue = opacity;
        mat->AddProperty(&name,AI_MATKEY_NAME);
        mat->AddProperty(&color,1,AI_MATKEY_BASE_COLOR); mat->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        mat->AddProperty(&ambient,1,AI_MATKEY_COLOR_AMBIENT); mat->AddProperty(&specular,1,AI_MATKEY_COLOR_SPECULAR);
        mat->AddProperty(&emissive,1,AI_MATKEY_COLOR_EMISSIVE);
        mat->AddProperty(&metal,1,AI_MATKEY_METALLIC_FACTOR); mat->AddProperty(&rough,1,AI_MATKEY_ROUGHNESS_FACTOR);
        mat->AddProperty(&shininess,1,AI_MATKEY_SHININESS); mat->AddProperty(&opacityValue,1,AI_MATKEY_OPACITY);
        mat->AddProperty(&shading,1,AI_MATKEY_SHADING_MODEL);
        const unsigned index = static_cast<unsigned>(materials.size()); renderedPalette[key] = index; materials.push_back(std::move(mat)); return index;
    };
    // Exporter edge polylines, shared by every mesh variant of a body. They are registered with the viewer's
    // precomputed-edge store (not drawn as geometry), so only the edge display modes show them.
    std::map<unsigned,std::shared_ptr<const MeshEdges>> meshEdges;
    quint64 collapsedTriangles = 0;
    std::map<QString,std::vector<BodyMeshes>> resources;
    QStringList members;
    QHash<QString,QString> formats;
    for (auto it = doc.representations.cbegin(); it != doc.representations.cend(); ++it) {
        if (it->format == QLatin1String("UVR") || it->format == QLatin1String("TESSELLATED")) {
            if(formats.contains(it->member)&&formats.value(it->member)!=it->format) throw Error(QStringLiteral("Conflicting representation formats"));
            formats[it->member]=it->format; members.append(it->member);
        }
        else if (it->format == QLatin1String("TECHREP")) result.warnings.append(QStringLiteral("Skipped TECHREP review attachment: %1").arg(it->member));
        else throw Error(QStringLiteral("Unsupported 3DXML representation format: %1").arg(it->format));
    }
    members.removeDuplicates(); members.sort();
    for (int memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
        options.check(); const QString member = members[memberIndex];
        if (options.progress) options.progress(member, memberIndex, members.size());
        Geometry geometry;
        try { geometry = formats.value(member)==QLatin1String("TESSELLATED") ? TessellatedReader::read(archive.read(member),options) : RepresentationReader::read(archive.read(member),options); }
        catch (const Error& e) { throw Error(QStringLiteral("%1: %2").arg(member,QString::fromUtf8(e.what()))); }
        std::vector<std::vector<unsigned>> bodyResources;
        for (const auto& body : geometry.bodies) {
            options.check();
            std::vector<Primitive> primitives; std::map<quint64,size_t> byColor;
            std::unordered_set<quint64> wantedSegments;
            for (const auto& group : body.lines) for (const auto& polyline : group.polylines)
                for (size_t i = 1; i < polyline.size(); ++i) if (polyline[i-1] != polyline[i]) wantedSegments.insert(segmentKey(polyline[i-1],polyline[i]));
            for (size_t fi = 0; fi < body.faces.size(); ++fi) {
                options.check(); const auto& face = body.faces[fi];
                if (face.triangles.empty()) continue;
                const auto styleKey = (quint64(face.alphaByte) << 32) | face.rgb;
                if (!byColor.count(styleKey)) { byColor[styleKey] = primitives.size(); Primitive p; p.rgb = face.rgb; p.alpha = ai_real(face.alphaByte) / 255; primitives.push_back(std::move(p)); }
                auto& p = primitives[byColor.at(styleKey)];
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
                    if (!std::isfinite(len)) throw Error(QStringLiteral("Nonfinite triangle in %1").arg(member));
                    if (!(len > 0)) { ++collapsedTriangles; continue; } // Collapsed source tessellation has no renderable area.
                    if (!wantedSegments.empty())
                        for (size_t corner = 0; corner < 3; ++corner) {
                            const quint64 key = segmentKey(tri[corner],tri[(corner+1)%3]);
                            if (wantedSegments.count(key)) p.edgeHits.insert(key);
                        }
                    aiVector3D flat(static_cast<ai_real>(x/len),static_cast<ai_real>(y/len),static_cast<ai_real>(z/len));
                    std::array<unsigned,3> output;
                    for (size_t corner = 0; corner < 3; ++corner) {
                        const unsigned source = tri[corner]; aiVector3D normal = flat;
                        // High bit distinguishes fallback triangle bindings from source pool/face bindings.
                        quint64 binding = (quint64(1)<<63) | (quint64(fi)<<32) | ti;
                        if (options.parsedNormals && validConstant) { normal=constant; binding=(quint64(1)<<62)|fi; }
                        else if (options.parsedNormals && !face.constant && !body.normals.empty()) {
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
                if (p.triangles.empty()) continue;
                auto mesh = std::make_unique<aiMesh>();
                mesh->mName = aiString(QStringLiteral("%1__body_%2").arg(member).arg(body.offset).toUtf8().constData());
                mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE; mesh->mMaterialIndex=material(p.rgb,p.alpha);
                mesh->mNumVertices=static_cast<unsigned>(p.positions.size()); mesh->mVertices=new aiVector3D[p.positions.size()]; mesh->mNormals=new aiVector3D[p.normals.size()];
                std::copy(p.positions.begin(),p.positions.end(),mesh->mVertices); std::copy(p.normals.begin(),p.normals.end(),mesh->mNormals);
                mesh->mNumFaces=static_cast<unsigned>(p.triangles.size()); mesh->mFaces=new aiFace[p.triangles.size()];
                for (size_t i=0;i<p.triangles.size();++i) { mesh->mFaces[i].mNumIndices=3; mesh->mFaces[i].mIndices=new unsigned[3]; std::copy(p.triangles[i].begin(),p.triangles[i].end(),mesh->mFaces[i].mIndices); }
                // Edges belong to the mesh whose triangles they border (a segment between two colour groups goes to both),
                // so hiding or isolating a mesh hides or isolates its own edges. Flat {x0,y0,z0,x1,y1,z1,...} in mesh
                // vertex space, one boundary entry per polyline run; a segment stored in both tail-group kinds is kept once.
                if (!p.edgeHits.empty()) {
                    auto edges = std::make_shared<MeshEdges>(); std::unordered_set<quint64> emitted;
                    for (const auto& group : body.lines) {
                        options.check();
                        for (const auto& polyline : group.polylines) {
                            const size_t before = edges->segments.size();
                            for (size_t i = 1; i < polyline.size(); ++i) {
                                const unsigned a = polyline[i-1], b = polyline[i];
                                const quint64 key = segmentKey(a,b);
                                if (a == b || !p.edgeHits.count(key) || !emitted.insert(key).second) continue;
                                for (unsigned v : {a,b}) { const auto& q = body.positions.at(v); edges->segments.insert(edges->segments.end(), {q.x,q.y,q.z}); }
                            }
                            if (edges->segments.size() != before) {
                                if (edges->bounds.empty()) edges->bounds.push_back(0);
                                edges->bounds.push_back(static_cast<int>(edges->segments.size()/3));
                            }
                        }
                    }
                    if (!edges->segments.empty()) meshEdges[static_cast<unsigned>(meshes.size())] = std::move(edges);
                }
                bodyMeshes.push_back(static_cast<unsigned>(meshes.size())); meshes.push_back(std::move(mesh));
            }
            bodyResources.push_back(std::move(bodyMeshes));
        }
        auto& group = resources[member];
        for (const auto& occurrence : geometry.occurrences) group.push_back({occurrence.path,bodyResources.at(occurrence.body)});
    }
    // aiMesh owns its material binding. Reuse the first binding in place and
    // deep-copy only when the same source mesh needs another occurrence color.
    std::vector<unsigned> baseMaterials;
    for (const auto& mesh : meshes) baseMaterials.push_back(mesh->mMaterialIndex);
    std::map<std::pair<unsigned,unsigned>,unsigned> variants;
    QSet<unsigned> assigned;
    auto appearance = [&](const std::vector<unsigned>& source, const aiColor4D* overrideColor, const MaterialRendering* rendering) {
        std::vector<unsigned> output;
        for (unsigned index : source) {
            unsigned target=baseMaterials.at(index);
            if (rendering) target=renderedMaterial(*rendering,overrideColor);
            else if (overrideColor) {
                unsigned rgb=0;
                for (int k=0;k<3;++k) rgb=(rgb<<8)|unsigned(std::lround((*overrideColor)[k]*255));
                target=material(rgb,overrideColor->a);
            }
            const auto key=std::make_pair(index,target);
            auto found=variants.find(key);
            if (found!=variants.end()) { output.push_back(found->second); continue; }
            unsigned variant=index;
            if (assigned.contains(index)) {
                options.check(); aiMesh* copy=nullptr;
                Assimp::SceneCombiner::Copy(&copy,meshes.at(index).get());
                std::unique_ptr<aiMesh> owned(copy);
                owned->mMaterialIndex=target; variant=static_cast<unsigned>(meshes.size()); meshes.push_back(std::move(owned));
                const auto edgeSource=meshEdges.find(index); if (edgeSource!=meshEdges.end()) meshEdges[variant]=edgeSource->second;
            } else { assigned.insert(index); meshes.at(index)->mMaterialIndex=target; }
            variants[key]=variant; output.push_back(variant);
        }
        return output;
    };
    // Covering materials by occurrence path; the highest layer wins (later definitions win ties).
    QHash<QString,std::pair<int,const MaterialRendering*>> dressed;
    for (const auto& link : doc.materialLinks) {
        const auto rendering = doc.materials.constFind(link.materialDocument + QLatin1Char('#') + link.materialReference);
        if (rendering == doc.materials.cend()) continue;
        auto existing = dressed.find(link.path);
        if (existing == dressed.end() || existing->first <= link.layer) dressed.insert(link.path, {link.layer, &rendering.value()});
    }
    QSet<QString> active, visitedHidden, visitedColors, visitedMaterials; size_t nodeCount=0;
    std::function<std::unique_ptr<aiNode>(const QString&,const QString&,const QString&,const QString&,const aiMatrix4x4&,bool,const aiColor4D*,const MaterialRendering*,int)> build;
    build = [&](const QString& reference,const QString& name,const QString& path,const QString& id,const aiMatrix4x4& local,bool parentVisible,const aiColor4D* inheritedColor,const MaterialRendering* inheritedMaterial,int depth) {
        options.check();
        if (depth > 128 || ++nodeCount > 100000 || active.contains(reference)) throw Error(QStringLiteral("Cyclic/excessive product graph at %1").arg(path));
        active.insert(reference); const auto& ref=doc.references[reference];
        auto override=doc.occurrenceColors.constFind(path);
        const aiColor4D* color=inheritedColor;
        if (override!=doc.occurrenceColors.cend()) { color=&override.value(); visitedColors.insert(path); }
        const MaterialRendering* rendering=inheritedMaterial;
        const auto dress=dressed.constFind(path);
        if (dress!=dressed.cend()) { rendering=dress->second; visitedMaterials.insert(path); }
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
            if (rep.format != QLatin1String("UVR") && rep.format != QLatin1String("TESSELLATED")) continue;
            const auto& bodies=resources.at(rep.member);
            if (bodies.size()==1 && ref.representations.size()==1) attach(*node,appearance(bodies.front().meshes,color,rendering));
            else for (const auto& body : bodies) {
                if (++nodeCount > 100000) throw Error(QStringLiteral("Expanded body node limit exceeded"));
                auto child=std::make_unique<aiNode>(QStringLiteral("%1__body_%2").arg(rep.name,body.path).toUtf8().constData());
                attach(*child,appearance(body.meshes,color,rendering));
                if (!visible) result.hiddenNodes.insert(child.get());
                nodes.push_back(std::move(child));
            }
        }
        for (const auto& childId : ref.instances) {
            const auto child=doc.instances[childId];
            nodes.push_back(build(child.reference,child.name,path.isEmpty() ? child.id : path+'/'+child.id,child.id,child.local,visible,color,rendering,depth+1));
        }
        children(*node,nodes); active.remove(reference); return node;
    };
    result.scene->mRootNode=build(doc.root,doc.references[doc.root].name,{},{},aiMatrix4x4(),true,nullptr,nullptr,0).release();
    if (visitedHidden != doc.hiddenPaths) throw Error(QStringLiteral("Unresolved visibility occurrence path"));
    if (visitedColors.size()!=doc.occurrenceColors.size()) throw Error(QStringLiteral("Unresolved color occurrence path"));
    if (visitedMaterials.size()!=dressed.size()) result.warnings.append(QStringLiteral("%1 material link(s) address occurrences that were not found; ignored.").arg(dressed.size()-visitedMaterials.size()));
    // Transfer ownership only after all potentially throwing allocations succeed.
    result.scene->mMeshes=new aiMesh*[meshes.size()]{};
    result.scene->mNumMeshes=static_cast<unsigned>(meshes.size());
    for (size_t i=0;i<meshes.size();++i) {
        const auto edges=meshEdges.find(static_cast<unsigned>(i));
        if (edges!=meshEdges.end()) result.meshEdges.insert(meshes[i].get(),edges->second);
        result.scene->mMeshes[i]=meshes[i].release();
    }
    result.scene->mMaterials=new aiMaterial*[materials.size()]{};
    result.scene->mNumMaterials=static_cast<unsigned>(materials.size());
    for (size_t i=0;i<materials.size();++i) result.scene->mMaterials[i]=materials[i].release();
    if (!result.scene->mNumMeshes) throw Error(QStringLiteral("3DXML contains no supported triangle geometry"));
    if (options.progress) options.progress(QStringLiteral("Complete"),members.size(),members.size());
    if (collapsedTriangles) result.warnings.append(QStringLiteral("Skipped %1 collapsed source triangles.").arg(collapsedTriangles));
    return result;
}
}
