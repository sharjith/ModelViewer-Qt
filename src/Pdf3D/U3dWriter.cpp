// The u3d headers are included first and only here: they pull in platform headers and define macros that Qt code should not
// see. Everything u3d related stays inside this translation unit.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "IFXResult.h"
#include "IFXOSLoader.h"
#include "ConverterResult.h"
#include "IFXDebug.h"
#include "IFXCOM.h"
#include "ConverterOptions.h"
#include "SceneConverterLib.h"
#include "SceneUtilities.h"
#include "IFXOSUtilities.h"
#include "File.h"
#include "Tokens.h"
#include "AnimationModifier.h"
#include "MotionResourceList.h"

#include "U3dWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QFile>
#include <QStringList>
#include <QUuid>
#include <QVector3D>

using namespace U3D_IDTF;

namespace Pdf3D
{
namespace
{

std::wstring wide(const QString& s)
{
    return s.toStdWString();
}

// "%.6f" round trip, the same precision the textual IDTF path used; keeps output identical to the converter's.
float f6(double v)
{
    char b[64];
    snprintf(b, sizeof b, "%.6f", v);
    return strtof(b, nullptr);
}

void setParent(Node& node, const float m[16], const std::wstring& parentName = L"<NULL>")
{
    ParentList parents;
    ParentData parent;
    parent.SetParentName(parentName.c_str());
    parent.SetParentTM(IFXMatrix4x4(m));
    parents.AddParentData(parent);
    node.SetParentList(parents);
}

QString resultText(IFXRESULT r)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(r), 8, 16, QLatin1Char('0'));
}

bool addMeshResource(SceneResources& res, const U3dMesh& m, QString* error)
{
    const int vertexCount = m.positions.size() / 3;
    const int faceCount = m.indices.size() / 3;
    if (vertexCount < 3 || faceCount < 1)
    {
        *error = QStringLiteral("mesh %1 has no triangles").arg(m.name);
        return false;
    }
    const bool perVertexNormals = (m.normals.size() == m.positions.size());

    ModelResourceList* list = static_cast<ModelResourceList*>(res.GetResourceList(IDTF_MODEL));
    list->SetType(IDTF_MODEL);

    MeshResource mesh;
    mesh.SetName(wide(m.name).c_str());
    mesh.m_type = IDTF_MESH;
    mesh.faceCount = faceCount;
    mesh.m_modelDescription.positionCount = vertexCount;
    mesh.m_modelDescription.basePositionCount = 0;
    mesh.m_modelDescription.normalCount = perVertexNormals ? vertexCount : faceCount;
    const bool vertexColors = (m.colors.size() == vertexCount * 4);
    mesh.m_modelDescription.diffuseColorCount = vertexColors ? vertexCount : 0;
    mesh.m_modelDescription.specularColorCount = 0;
    mesh.m_modelDescription.textureCoordCount = 0;
    mesh.m_modelDescription.boneCount = 0;
    mesh.m_modelDescription.shadingCount = 1;
    ShadingDescription sd;
    sd.m_shaderId = 0;
    sd.m_textureLayerCount = 0;
    mesh.m_shadingDescriptions.AddShadingDescription(sd);

    for (int i = 0; i < vertexCount; ++i)
        mesh.m_positions.CreateNewElement().SetPoint(IFXVector3(m.positions[3 * i], m.positions[3 * i + 1], m.positions[3 * i + 2]));

    if (vertexColors)
    {
        for (int i = 0; i < vertexCount; ++i)
            mesh.m_diffuseColors.CreateNewElement().SetColor(
                IFXVector4(m.colors[4 * i], m.colors[4 * i + 1], m.colors[4 * i + 2], m.colors[4 * i + 3]));
    }

    if (perVertexNormals)
    {
        for (int i = 0; i < vertexCount; ++i)
            mesh.m_normals.CreateNewElement().SetPoint(IFXVector3(m.normals[3 * i], m.normals[3 * i + 1], m.normals[3 * i + 2]));
    }

    for (int f = 0; f < faceCount; ++f)
    {
        const quint32 a = m.indices[3 * f], b = m.indices[3 * f + 1], c = m.indices[3 * f + 2];
        if (a >= static_cast<quint32>(vertexCount) || b >= static_cast<quint32>(vertexCount) || c >= static_cast<quint32>(vertexCount))
        {
            *error = QStringLiteral("mesh %1 has a triangle index out of range").arg(m.name);
            return false;
        }
        mesh.m_facePositions.CreateNewElement().SetData(a, b, c);
        if (vertexColors)
            mesh.m_faceDiffuseColors.CreateNewElement().SetData(a, b, c);
        mesh.m_faceShaders.CreateNewElement() = 0;

        if (perVertexNormals)
        {
            mesh.m_faceNormals.CreateNewElement().SetData(a, b, c);
        }
        else
        {
            const QVector3D pa(m.positions[3 * a], m.positions[3 * a + 1], m.positions[3 * a + 2]);
            const QVector3D pb(m.positions[3 * b], m.positions[3 * b + 1], m.positions[3 * b + 2]);
            const QVector3D pc(m.positions[3 * c], m.positions[3 * c + 1], m.positions[3 * c + 2]);
            QVector3D n = QVector3D::crossProduct(pb - pa, pc - pa);
            if (n.lengthSquared() > 0.0f) n.normalize(); else n = QVector3D(0.0f, 0.0f, 1.0f);
            mesh.m_normals.CreateNewElement().SetPoint(IFXVector3(n.x(), n.y(), n.z()));
            mesh.m_faceNormals.CreateNewElement().SetData(f, f, f);
        }
    }

    list->AddResource(&mesh);
    return true;
}

} // namespace

bool U3dWriter::modulesAvailable(QStringList* missing)
{
#ifdef _WIN32
    const QString prefix, suffix = QStringLiteral(".dll");
#else
    const QString prefix = QStringLiteral("lib"), suffix = QStringLiteral(".so");
#endif
    const QString dir = QCoreApplication::applicationDirPath();
    bool all = true;
    for (const char* module : {"IFXCore", "IFXExporting", "IFXImporting", "IFXScheduling"})
    {
        const QString file = prefix + QLatin1String(module) + suffix;
        if (!QFile::exists(dir + QLatin1Char('/') + file))
        {
            all = false;
            if (missing)
                missing->append(file);
        }
    }
    return all;
}

U3dWriteResult U3dWriter::write(const U3dScene& scene)
{
    U3dWriteResult out;

    if (scene.isEmpty())
    {
        out.error = QStringLiteral("There is no visible geometry to export.");
        return out;
    }

    QStringList missing;
    if (!modulesAvailable(&missing))
    {
        out.error = QStringLiteral("The 3D PDF export modules are missing next to the application: %1").arg(missing.join(QStringLiteral(", ")));
        return out;
    }

    QVector3D lo, hi;
    if (!scene.computeBounds(&lo, &hi))
    {
        out.error = QStringLiteral("The scene has no geometry.");
        return out;
    }
    // at least one material so every node has a shader
    QVector<U3dMaterial> materials = scene.materials;
    if (materials.isEmpty())
        materials.push_back(U3dMaterial());

    const QVector3D center = (lo + hi) * 0.5f;
    const float radius = qMax(0.5f * (hi - lo).length(), 1.0e-6f);

    // The library writes to a file; use an ASCII-only temporary name (file names go through a locale dependent conversion on
    // Linux) and read the bytes back.
    const QString tempPath = QDir::temp().filePath(QStringLiteral("mv3dpdf_%1.u3d").arg(QUuid::createUuid().toString(QUuid::Id128)));

    IFXRESULT result = IFX_OK;
    result = IFXCOMInitialize();
    if (!IFXSUCCESS(result))
    {
        out.error = QStringLiteral("The 3D library could not be initialised (%1).").arg(resultText(result));
        return out;
    }

    {
        ConverterOptions opt;
        FileOptions fopt;
        fopt.outFile = wide(tempPath).c_str();
        fopt.exportOptions = IFXExportOptions(65535);
        fopt.profile = 0;
        fopt.scalingFactor = 1.0f;
        fopt.debugLevel = 0;
        opt.positionQuality = 1000; opt.texCoordQuality = 1000; opt.normalQuality = 1000; opt.diffuseQuality = 1000;
        opt.specularQuality = 1000; opt.geoQuality = 1000; opt.textureQuality = 100; opt.animQuality = 1000; opt.textureLimit = 0;
        opt.removeZeroAreaFaces = TRUE; opt.zeroAreaFaceTolerance = 100.0f * FLT_EPSILON; opt.excludeNormals = FALSE;

        SceneUtilities sceneUtils;
        result = sceneUtils.InitializeScene(fopt.profile, fopt.scalingFactor);

        if (IFXSUCCESS(result))
        {
            SceneConverter converter(&sceneUtils, &opt);
            NodeList& nodes = converter.m_nodeList;
            SceneResources& res = converter.m_sceneResources;
            ModifierList& mods = converter.m_modifierList;

            // Default camera (the PDF writes its own views; this one is what Acrobat's "U3D own view" would use): in front of the
            // scene looking along +Y with Z up, far enough to see the bounding sphere.
            const float dist = radius / sinf(15.0f * 3.14159265f / 180.0f) * 1.25f;
            {
                ViewNode view;
                view.SetType(IDTF_VIEW);
                view.SetName(L"DefaultView");
                view.SetResourceName(L"SceneViewResource");
                const float m[16] = {1, 0, 0, 0,   0, 0, 1, 0,   0, -1, 0, 0,   f6(center.x()), f6(center.y() - dist), f6(center.z()), 1};
                setParent(view, m);
                ViewNodeData vd;
                vd.SetUnitType(IDTF_VIEW_UNIT_PIXEL);
                vd.SetClipping(VIEW_NEAR_CLIP, VIEW_FAR_CLIP);
                vd.SetViewPort(VIEW_PORT_WIDTH, VIEW_PORT_HEIGHT, VIEW_PORT_H_POSITION, VIEW_PORT_V_POSITION);
                vd.SetType(IDTF_PERSPECTIVE_VIEW);
                vd.SetProjection(30.0f);
                view.SetViewData(vd);
                nodes.AddNode(&view);
            }

            // group nodes (identity transform, children of the root) first: the model tree shows one entry per measurement/annotation
            for (int gi = 0; gi < scene.groups.size(); ++gi)
            {
                Node group;
                group.SetType(IDTF_GROUP);
                group.SetName(wide(scene.groups[gi]).c_str());
                const float identity[16] = {1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   0, 0, 0, 1};
                const QString groupParent = gi < scene.groupParents.size() ? scene.groupParents[gi] : QString();
                setParent(group, identity, groupParent.isEmpty() ? std::wstring(L"<NULL>") : wide(groupParent));
                nodes.AddNode(&group);
            }

            for (const U3dNode& n : scene.nodes)
            {
                if (n.meshIndex < 0 || n.meshIndex >= scene.meshes.size())
                    continue;
                ModelNode node;
                node.SetType(IDTF_MODEL);
                node.SetName(wide(n.name).c_str());
                node.SetResourceName(wide(scene.meshes[n.meshIndex].name).c_str());
                node.SetVisibility(L"BOTH");   // the default (FRONT) culls back faces; thin or inward-wound shells (tyres) then look see-through
                // Qt column-major == the row-vector layout with translation in the last row; relative to the group when there is one
                setParent(node, n.world.constData(), n.parent.isEmpty() ? std::wstring(L"<NULL>") : wide(n.parent));
                nodes.AddNode(&node);
            }

            {
                LightNode light;
                light.SetType(IDTF_LIGHT);
                light.SetName(L"Omni01");
                light.SetResourceName(L"DefaultPointLight");
                const float m[16] = {1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   f6(center.x()), f6(center.y() - dist), f6(center.z() + radius), 1};
                setParent(light, m);
                nodes.AddNode(&light);
            }

            {
                ViewResourceList* l = static_cast<ViewResourceList*>(res.GetResourceList(IDTF_VIEW));
                l->SetType(IDTF_VIEW);
                ViewResource r;
                r.SetName(L"SceneViewResource");
                r.AddRootNode(L"");
                l->AddResource(r);
            }
            {
                LightResourceList* l = static_cast<LightResourceList*>(res.GetResourceList(IDTF_LIGHT));
                l->SetType(IDTF_LIGHT);
                LightResource r;
                r.SetName(L"DefaultPointLight");
                r.m_type = IDTF_POINT_LIGHT;
                r.m_color.SetColor(IFXVector4(1.0f, 1.0f, 1.0f));
                r.m_attenuation.SetPoint(IFXVector3(1.0f, 0.0f, 0.0f));
                r.m_intensity = 1.0f;
                r.m_spotAngle = 0.0f;
                l->AddResource(r);
            }

            // animation: one motion resource, one track per animated node (key frames are relative to the node's own transform, see U3dScene.h)
            QStringList animatedNodes;
            if (scene.hasAnimation())
            {
                MotionResourceList* l = static_cast<MotionResourceList*>(res.GetResourceList(IDTF_MOTION));
                l->SetType(IDTF_MOTION);
                // One motion resource per animated node, named "M_<node>", with a single track, as the commercial files and the reference
                // scenes have it: sharing one resource between several animated nodes got Acrobat to disable the animation (verified).
                for (const U3dMotionTrack& track : scene.animation.tracks)
                {
                    if (track.times.isEmpty() || track.times.size() != track.keys.size())
                        continue;
                    MotionResource motion;
                    motion.SetName(wide(QStringLiteral("M_") + track.node).c_str());
                    MotionTrack t;
                    t.m_name = wide(track.node).c_str();
                    QQuaternion previous;
                    for (int k = 0; k < track.times.size(); ++k)
                    {
                        const QMatrix4x4& m = track.keys[k];
                        const QVector3D c0(m(0, 0), m(1, 0), m(2, 0)), c1(m(0, 1), m(1, 1), m(2, 1)), c2(m(0, 2), m(1, 2), m(2, 2));
                        const float sx = std::max(c0.length(), 1.0e-9f), sy = std::max(c1.length(), 1.0e-9f), sz = std::max(c2.length(), 1.0e-9f);
                        QMatrix3x3 rot;
                        for (int r = 0; r < 3; ++r)
                        {
                            rot(r, 0) = m(r, 0) / sx;
                            rot(r, 1) = m(r, 1) / sy;
                            rot(r, 2) = m(r, 2) / sz;
                        }
                        QQuaternion q = QQuaternion::fromRotationMatrix(rot).normalized();
                        if (k > 0 && QQuaternion::dotProduct(previous, q) < 0.0f)
                            q = QQuaternion(-q.scalar(), -q.vector());   // the shorter way round between key frames
                        previous = q;

                        KeyFrame kf;
                        kf.m_time = track.times[k];
                        kf.m_displacement.SetPoint(IFXVector3(f6(m(0, 3)), f6(m(1, 3)), f6(m(2, 3))));
                        kf.m_rotation.SetQuat(IFXVector4(q.scalar(), q.x(), q.y(), q.z()));   // w x y z
                        kf.m_scale.SetPoint(IFXVector3(sx, sy, sz));
                        t.AddKeyFrame(kf);
                    }
                    motion.AddMotionTrack(t);
                    animatedNodes << track.node;
                    l->AddResource(motion);
                }
            }

            for (const U3dMesh& m : scene.meshes)
            {
                QString err;
                if (!addMeshResource(res, m, &err))
                {
                    out.error = err;
                    result = IFX_E_UNDEFINED;
                    break;
                }
            }

            if (IFXSUCCESS(result))
            {
                {
                    ShaderResourceList* l = static_cast<ShaderResourceList*>(res.GetResourceList(IDTF_SHADER));
                    l->SetType(IDTF_SHADER);
                    for (int i = 0; i < materials.size(); ++i)
                    {
                        Shader s;
                        s.SetName(wide(QStringLiteral("Shader%1").arg(i)).c_str());
                        s.m_materialName = wide(QStringLiteral("Material%1").arg(i)).c_str();
                        s.m_useVertexColor = materials[i].vertexColors ? L"TRUE" : L"FALSE";
                        l->AddResource(s);
                    }
                }
                {
                    MaterialResourceList* l = static_cast<MaterialResourceList*>(res.GetResourceList(IDTF_MATERIAL));
                    l->SetType(IDTF_MATERIAL);
                    for (int i = 0; i < materials.size(); ++i)
                    {
                        const U3dMaterial& mat = materials[i];
                        Material m;
                        m.SetName(wide(QStringLiteral("Material%1").arg(i)).c_str());
                        m.m_ambient.SetColor(IFXVector4(f6(mat.diffuse.x() * 0.2), f6(mat.diffuse.y() * 0.2), f6(mat.diffuse.z() * 0.2)));
                        m.m_diffuse.SetColor(IFXVector4(f6(mat.diffuse.x()), f6(mat.diffuse.y()), f6(mat.diffuse.z())));
                        m.m_specular.SetColor(IFXVector4(0.072f, 0.072f, 0.072f));
                        m.m_emissive.SetColor(IFXVector4(f6(mat.emissive.x()), f6(mat.emissive.y()), f6(mat.emissive.z())));
                        m.m_reflectivity = 0.1f;
                        m.m_opacity = mat.opacity;
                        l->AddResource(m);
                    }
                }
                for (const U3dNode& n : scene.nodes)
                {
                    if (n.meshIndex < 0 || n.meshIndex >= scene.meshes.size())
                        continue;
                    const int mi = qBound(0, scene.meshes[n.meshIndex].materialIndex, qMax(0, static_cast<int>(materials.size()) - 1));
                    if (animatedNodes.contains(n.name))
                    {
                        AnimationModifier am;
                        am.SetName(wide(n.name).c_str());
                        am.SetType(IDTF_ANIMATION_MODIFIER);
                        am.SetChainType(IDTF_NODE);
                        am.m_playing = scene.animation.playing ? TRUE : FALSE;
                        am.m_rootLock = FALSE;
                        am.m_singleTrack = TRUE;
                        am.m_autoBlend = TRUE;
                        am.SetTimeScale(1.0f);
                        am.SetBlendTime(0.5f);
                        MotionInfo info;
                        info.m_name = wide(QStringLiteral("M_") + n.name).c_str();
                        info.m_loop = TRUE;
                        info.m_sync = FALSE;
                        info.m_timeOffset = 0.0f;
                        info.m_timeScale = 1.0f;
                        am.AddMotionInfo(info);
                        mods.AddModifier(&am);   // before the shading modifier, as in the reference files
                    }
                    ShadingModifier sm;
                    sm.SetName(wide(n.name).c_str());
                    sm.SetType(IDTF_SHADING_MODIFIER);
                    sm.SetChainType(IDTF_NODE);
                    sm.SetAttributes(ATTRMESH | ATTRLINE | ATTRPOINT | ATTRGLYPH);
                    ShaderList sl;
                    sl.AddShaderName(wide(QStringLiteral("Shader%1").arg(mi)).c_str());
                    sm.AddShaderList(sl);
                    mods.AddModifier(&sm);
                }

                result = converter.Convert();
                if (IFXSUCCESS(result))
                    result = sceneUtils.WriteSceneToFile(fopt.outFile, fopt.exportOptions);
            }
        }
    }

    IFXCOMUninitialize();

    if (!IFXSUCCESS(result))
    {
        if (out.error.isEmpty())
            out.error = QStringLiteral("The 3D library reported an error (%1).").arg(resultText(result));
        QFile::remove(tempPath);
        return out;
    }

    QFile f(tempPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        out.error = QStringLiteral("The 3D library did not write its output file.");
        return out;
    }
    out.data = f.readAll();
    f.close();
    QFile::remove(tempPath);
    out.ok = !out.data.isEmpty();
    if (!out.ok)
        out.error = QStringLiteral("The 3D library wrote an empty file.");
    return out;
}

} // namespace Pdf3D
