// Phase 0 spike: drive the u3d library IN-PROCESS (no IDTF file, no IDTFConverter main).
//
// Builds the same scene as tools/3dpdf/make_test_scene_idtf.py's first two parts (LBracket + Block, view, light, shaders,
// materials, shading modifiers) in memory through the libIDTF classes and writes a .u3d, so the output can be compared byte for
// byte with the converter's output for lscene.idtf (research doc, 9.8). Throwaway spike code, not part of the application.
//
//   inproc_u3d <out.u3d>
//
// Build: see tools/3dpdf/inproc/CMakeLists.txt (links the static IDTF library from a u3d build).
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <wchar.h>
#include <math.h>
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
#include "Point.h"

using namespace U3D_IDTF;

namespace
{
// Float exactly as the IDTF text round trip sees it ("%.6f" then parse), so the in-memory scene matches the file scene.
float f6(double v)
{
    char b[64];
    snprintf(b, sizeof b, "%.6f", v);
    return strtof(b, nullptr);
}

struct Cuboid { double x0, x1, y0, y1, z0, z1; };
struct Part { const wchar_t* name; std::vector<Cuboid> cuboids; double r, g, b; };

const unsigned TRIS[12][3] = {{0, 2, 3}, {3, 1, 0}, {4, 5, 7}, {7, 6, 4}, {0, 1, 5}, {5, 4, 0}, {1, 3, 7}, {7, 5, 1}, {3, 2, 6}, {6, 7, 3}, {2, 0, 4}, {4, 6, 2}};
const double NORM[12][3] = {{0, 0, -1}, {0, 0, -1}, {0, 0, 1}, {0, 0, 1}, {0, -1, 0}, {0, -1, 0}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {-1, 0, 0}, {-1, 0, 0}};

void addParent(Node& node, const float m[16])
{
    ParentList parents;
    ParentData parent;
    parent.SetParentName(L"<NULL>");
    parent.SetParentTM(IFXMatrix4x4(m));
    parents.AddParentData(parent);
    node.SetParentList(parents);
}

void addMesh(SceneResources& res, const Part& p)
{
    ModelResourceList* list = static_cast<ModelResourceList*>(res.GetResourceList(IDTF_MODEL));
    list->SetType(IDTF_MODEL);
    MeshResource mesh;
    mesh.SetName(p.name);
    mesh.m_type = IDTF_MESH;
    const int nc = (int)p.cuboids.size();
    mesh.faceCount = 12 * nc;
    mesh.m_modelDescription.positionCount = 8 * nc;
    mesh.m_modelDescription.basePositionCount = 0;
    mesh.m_modelDescription.normalCount = 36 * nc;
    mesh.m_modelDescription.diffuseColorCount = 0;
    mesh.m_modelDescription.specularColorCount = 0;
    mesh.m_modelDescription.textureCoordCount = 0;
    mesh.m_modelDescription.boneCount = 0;
    mesh.m_modelDescription.shadingCount = 1;
    ShadingDescription sd;
    sd.m_shaderId = 0;
    sd.m_textureLayerCount = 0;
    mesh.m_shadingDescriptions.AddShadingDescription(sd);

    unsigned nBase = 0, nNorm = 0;
    for (const Cuboid& c : p.cuboids)
    {
        for (int f = 0; f < 12; ++f)
        {
            mesh.m_facePositions.CreateNewElement().SetData(nBase + TRIS[f][0], nBase + TRIS[f][1], nBase + TRIS[f][2]);
            mesh.m_faceNormals.CreateNewElement().SetData(nNorm, nNorm + 1, nNorm + 2);
            nNorm += 3;
            mesh.m_faceShaders.CreateNewElement() = 0;
        }
        for (int i = 0; i < 8; ++i)
            mesh.m_positions.CreateNewElement().SetPoint(IFXVector3(f6((i & 1) ? c.x1 : c.x0), f6((i & 2) ? c.y1 : c.y0), f6((i & 4) ? c.z1 : c.z0)));
        for (int f = 0; f < 12; ++f)
            for (int k = 0; k < 3; ++k)
                mesh.m_normals.CreateNewElement().SetPoint(IFXVector3((float)NORM[f][0], (float)NORM[f][1], (float)NORM[f][2]));
        nBase += 8;
    }
    list->AddResource(&mesh);
}
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: inproc_u3d <out.u3d>\n"); return 2; }
    std::wstring outPath;
    for (const char* s = argv[1]; *s; ++s) outPath += (wchar_t)*s;

    // The application must not change the process-wide locale; set INPROC_SKIP_LOCALE=1 to test that nothing needs it.
    const bool skipLocale = getenv("INPROC_SKIP_LOCALE") != nullptr;
    IFXRESULT result = skipLocale ? IFX_OK : IFXSetDefaultLocale();
    printf("IFXSetDefaultLocale %x (skipped=%d)\n", (unsigned)result, skipLocale ? 1 : 0);
    if (IFXSUCCESS(result)) { IFXDEBUG_STARTUP(); result = IFXCOMInitialize(); }
    printf("IFXCOMInitialize %x\n", (unsigned)result);

    {
        ConverterOptions opt;
        FileOptions fopt;
        fopt.outFile = outPath.c_str();
        fopt.exportOptions = IFXExportOptions(65535);
        fopt.profile = 0;
        fopt.scalingFactor = 1.0f;
        fopt.debugLevel = 0;
        opt.positionQuality = 1000; opt.texCoordQuality = 1000; opt.normalQuality = 1000; opt.diffuseQuality = 1000;
        opt.specularQuality = 1000; opt.geoQuality = 1000; opt.textureQuality = 100; opt.animQuality = 1000; opt.textureLimit = 0;
        opt.removeZeroAreaFaces = TRUE; opt.zeroAreaFaceTolerance = 100.0f * FLT_EPSILON; opt.excludeNormals = FALSE;

        SceneUtilities sceneUtils;
        if (IFXSUCCESS(result)) result = sceneUtils.InitializeScene(fopt.profile, fopt.scalingFactor);
        printf("InitializeScene %x\n", (unsigned)result);

        SceneConverter converter(&sceneUtils, &opt);
        NodeList& nodes = converter.m_nodeList;
        SceneResources& res = converter.m_sceneResources;
        ModifierList& mods = converter.m_modifierList;

        const std::vector<Part> parts = {
            {L"LBracket", {{-30, 30, -10, 10, 0, 10}, {-30, -20, -10, 10, 10, 50}}, 0.88, 0.56, 0.34},
            {L"Block", {{35, 55, -10, 10, 0, 20}}, 0.25, 0.45, 0.85},
        };

        // nodes: view, parts, light (same order as the IDTF file)
        {
            ViewNode view;
            view.SetType(IDTF_VIEW);
            view.SetName(L"DefaultView");
            view.SetResourceName(L"SceneViewResource");
            const float m[16] = {1, 0, 0, 0, 0, 0.258819f, 0.965926f, 0, 0, -0.965926f, 0.258819f, 0, 12.5f, -241.481461f, 89.704765f, 1};
            addParent(view, m);
            ViewNodeData vd;
            vd.SetUnitType(IDTF_VIEW_UNIT_PIXEL);
            vd.SetClipping(VIEW_NEAR_CLIP, VIEW_FAR_CLIP);
            vd.SetViewPort(VIEW_PORT_WIDTH, VIEW_PORT_HEIGHT, VIEW_PORT_H_POSITION, VIEW_PORT_V_POSITION);
            vd.SetType(IDTF_PERSPECTIVE_VIEW);
            vd.SetProjection(34.515877f);
            view.SetViewData(vd);
            nodes.AddNode(&view);
        }
        const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (const Part& p : parts)
        {
            ModelNode node;
            node.SetType(IDTF_MODEL);
            node.SetName(p.name);
            node.SetResourceName(p.name);
            addParent(node, I);
            nodes.AddNode(&node);
        }
        {
            LightNode light;
            light.SetType(IDTF_LIGHT);
            light.SetName(L"Omni01");
            light.SetResourceName(L"DefaultPointLight");
            const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 60, -150, 120, 1};
            addParent(light, m);
            nodes.AddNode(&light);
        }

        // resources: view, light, models, shaders, materials
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
        for (const Part& p : parts) addMesh(res, p);
        {
            ShaderResourceList* l = static_cast<ShaderResourceList*>(res.GetResourceList(IDTF_SHADER));
            l->SetType(IDTF_SHADER);
            for (const Part& p : parts)
            {
                Shader s;
                s.SetName((std::wstring(p.name) + L"Shader").c_str());
                s.m_materialName = (std::wstring(p.name) + L"Mat").c_str();
                l->AddResource(s);
            }
        }
        {
            MaterialResourceList* l = static_cast<MaterialResourceList*>(res.GetResourceList(IDTF_MATERIAL));
            l->SetType(IDTF_MATERIAL);
            for (const Part& p : parts)
            {
                Material m;
                m.SetName((std::wstring(p.name) + L"Mat").c_str());
                m.m_ambient.SetColor(IFXVector4(f6(p.r * 0.2), f6(p.g * 0.2), f6(p.b * 0.2)));
                m.m_diffuse.SetColor(IFXVector4(f6(p.r), f6(p.g), f6(p.b)));
                m.m_specular.SetColor(IFXVector4(0.072f, 0.072f, 0.072f));
                m.m_emissive.SetColor(IFXVector4(0.0f, 0.0f, 0.0f));
                m.m_reflectivity = 0.1f;
                m.m_opacity = 1.0f;
                l->AddResource(m);
            }
        }
        for (const Part& p : parts)
        {
            ShadingModifier sm;
            sm.SetName(p.name);
            sm.SetType(IDTF_SHADING_MODIFIER);
            sm.SetChainType(IDTF_NODE);
            sm.SetAttributes(ATTRMESH | ATTRLINE | ATTRPOINT | ATTRGLYPH);
            ShaderList sl;
            sl.AddShaderName((std::wstring(p.name) + L"Shader").c_str());
            sm.AddShaderList(sl);
            mods.AddModifier(&sm);
        }

        if (IFXSUCCESS(result)) result = converter.Convert();
        printf("Convert %x\n", (unsigned)result);
        if (IFXSUCCESS(result)) result = sceneUtils.WriteSceneToFile(fopt.outFile, fopt.exportOptions);
        printf("WriteSceneToFile %x\n", (unsigned)result);
    }

    IFXCOMUninitialize();
    IFXDEBUG_SHUTDOWN();
    return IFXSUCCESS(result) ? 0 : 1;
}
