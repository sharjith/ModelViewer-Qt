// Tests for the Save as 3D PDF export (src/Pdf3D). GUI-free except the optional end-to-end test.
//
//   pdf3d_tests          view dictionaries, aiScene conversion, incremental PDF update on a real QPdfWriter page, the
//                        in-process u3d conversion (skipped when the u3d modules are not next to the test executable)
//   pdf3d_tests --full   additionally runs Pdf3DExporter::exportToFile (text layout and fonts) and checks the written file
#include "ClippingState.h"
#include "OverlayToU3d.h"
#include "PdfAppender.h"
#include "Pdf3DExporter.h"
#include "Pdf3DViews.h"
#include "SceneToU3d.h"
#include "U3dWriter.h"

#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

bool nearly(double a, double b, double eps = 1.0e-4) { return std::fabs(a - b) <= eps; }

// A one-page PDF as QPdfWriter writes it (shapes only, so no fonts and no GUI application are needed).
QByteArray makeQtPdf()
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setPdfVersion(QPagedPaintDevice::PdfVersion_1_6);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.drawRect(10, 10, 100, 100);
        painter.end();
    }
    return bytes;
}

QList<double> numbersAfter(const QByteArray& text, const char* key)
{
    const QRegularExpression re(QStringLiteral("%1 \\[([^\\]]*)\\]").arg(QString::fromLatin1(key)));
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(text));
    QList<double> out;
    if (m.hasMatch())
        for (const QString& part : m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            out.append(part.toDouble());
    return out;
}

void testViews()
{
    const float radius = 10.0f;
    const double distance = radius / std::sin(15.0 * 3.14159265358979 / 180.0) * 1.05;

    const QList<QByteArray> views = Pdf3D::standardViews(QVector3D(0, 0, 0), radius, Pdf3D::UpAxis::Y);
    CHECK(views.size() == 7 && views.size() == Pdf3D::standardViewNames().size());
    for (const QByteArray& v : views)
    {
        CHECK(v.startsWith("<< /Type /3DView") && v.contains("/MS /M") && v.contains("/FOV 30"));
        CHECK(numbersAfter(v, "/C2W").size() == 12);
    }

    // Front, Y-up: the eye is on +Z, looking along -Z, up is +Y
    const QList<double> front = numbersAfter(views[1], "/C2W");
    CHECK(front.size() == 12);
    if (front.size() == 12)
    {
        CHECK(nearly(front[3], 0) && nearly(front[4], 1) && nearly(front[5], 0));       // up row
        CHECK(nearly(front[6], 0) && nearly(front[7], 0) && nearly(front[8], -1));      // forward row
        CHECK(nearly(front[9], 0, 1e-3) && nearly(front[10], 0, 1e-3) && nearly(front[11], distance, 1e-3));   // eye
    }
    {
        // compared as numbers: the code computes the distance in float, this test in double
        const QRegularExpressionMatch co = QRegularExpression(QStringLiteral("/CO ([0-9.]+)")).match(QString::fromLatin1(views[1]));
        CHECK(co.hasMatch() && nearly(co.captured(1).toDouble(), distance, 1.0e-2));
    }

    // Top, Y-up: the eye is above (+Y), up is -Z (the model's front edge at the bottom of the screen)
    const QList<double> top = numbersAfter(views[5], "/C2W");
    if (top.size() == 12)
    {
        CHECK(nearly(top[4], 0) && nearly(top[5], -1));
        CHECK(nearly(top[7], -1) && nearly(top[10], distance, 1e-3));
    }

    // Z-up Front looks along +Y from -Y
    const QList<double> zFront = numbersAfter(Pdf3D::standardViews(QVector3D(1, 2, 3), radius, Pdf3D::UpAxis::Z)[1], "/C2W");
    if (zFront.size() == 12)
    {
        CHECK(nearly(zFront[5], 1) && nearly(zFront[7], 1));
        CHECK(nearly(zFront[9], 1, 1e-3) && nearly(zFront[10], 2 - distance, 1e-3) && nearly(zFront[11], 3, 1e-3));
    }

    CHECK(Pdf3D::PdfAppender::pdfNumber(0.5) == "0.5" && Pdf3D::PdfAppender::pdfNumber(-7.1e-15) == "0" && Pdf3D::PdfAppender::pdfNumber(12) == "12");
    CHECK(Pdf3D::PdfAppender::pdfString(QStringLiteral("a(b)\\")) == QByteArray("(a\\(b\\)\\\\)"));
}

aiScene* makeTriangleScene()
{
    aiScene* s = new aiScene;
    s->mRootNode = new aiNode;
    s->mRootNode->mName = "root";
    s->mRootNode->mNumChildren = 1;
    s->mRootNode->mChildren = new aiNode*[1];
    aiNode* child = new aiNode;
    child->mName = "Part A";
    child->mParent = s->mRootNode;
    aiMatrix4x4::Translation(aiVector3D(1, 2, 3), child->mTransformation);
    child->mNumMeshes = 1;
    child->mMeshes = new unsigned[1]{0};
    s->mRootNode->mChildren[0] = child;

    s->mNumMeshes = 1;
    s->mMeshes = new aiMesh*[1];
    aiMesh* mesh = new aiMesh;
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3]{aiVector3D(0, 0, 0), aiVector3D(1, 0, 0), aiVector3D(0, 1, 0)};
    mesh->mNormals = new aiVector3D[3]{aiVector3D(0, 0, 1), aiVector3D(0, 0, 1), aiVector3D(0, 0, 1)};
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned[3]{0, 1, 2};
    mesh->mMaterialIndex = 0;
    s->mMeshes[0] = mesh;

    s->mNumMaterials = 1;
    s->mMaterials = new aiMaterial*[1];
    aiMaterial* mat = new aiMaterial;
    const aiColor4D diffuse(0.1f, 0.2f, 0.3f, 1.0f);
    mat->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
    s->mMaterials[0] = mat;
    return s;
}

void testClippingStateJson()
{
    // the Clipping Planes snapshot stored with captured views and scene states round-trips through its JSON encoding
    ClippingState state;
    state.planes.append({0, 12.5, false});
    state.planes.append({2, -3.25, true});
    state.showGizmo = false;
    const ClippingState back = clippingStateFromJson(clippingStateToJson(state));
    CHECK(!back.showGizmo && clippingStateFromJson(QJsonObject()).showGizmo);   // the gizmo choice is stored; absent means shown
    CHECK(back.planes.size() == 2 && back.isActive() && !back.boxEnabled);
    CHECK(back.planes.size() == 2 && back.planes[0].axis == 0 && nearly(back.planes[0].position, 12.5) && !back.planes[0].keepPositive);
    CHECK(back.planes.size() == 2 && back.planes[1].axis == 2 && nearly(back.planes[1].position, -3.25) && back.planes[1].keepPositive);

    ClippingState box;
    box.boxEnabled = true;
    box.boxKeepInside = true;
    const double limits[6] = {-1, 2, -3, 4, -5, 6};
    for (int i = 0; i < 6; ++i)
        box.boxLimits[i] = limits[i];
    const ClippingState boxBack = clippingStateFromJson(clippingStateToJson(box));
    CHECK(boxBack.boxEnabled && boxBack.boxKeepInside && boxBack.planes.isEmpty() && boxBack.isActive());
    bool same = true;
    for (int i = 0; i < 6; ++i)
        same = same && nearly(boxBack.boxLimits[i], limits[i]);
    CHECK(same);

    // an empty object is an inactive state; a plane on an invalid axis is dropped
    CHECK(!clippingStateFromJson(QJsonObject()).isActive());
    QJsonObject bad;
    QJsonObject badPlane;
    badPlane.insert(QStringLiteral("axis"), 7);
    bad.insert(QStringLiteral("planes"), QJsonArray{badPlane});
    CHECK(clippingStateFromJson(bad).planes.isEmpty());
}

void testAssemblyFolders()
{
    // root -> "Wheel Axle Assy" (no mesh) -> "Part A" (the mesh): the assembly becomes a folder, the part keeps its world matrix
    aiScene* scene = makeTriangleScene();
    aiNode* part = scene->mRootNode->mChildren[0];
    aiNode* assembly = new aiNode;
    assembly->mName = "Wheel Axle Assy";
    assembly->mParent = scene->mRootNode;
    assembly->mNumChildren = 1;
    assembly->mChildren = new aiNode*[1]{part};
    part->mParent = assembly;
    scene->mRootNode->mChildren[0] = assembly;
    const Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene);
    delete scene;

    CHECK(u.groups.size() == 1 && u.groupParents.size() == 1 && u.groupParents[0].isEmpty());
    CHECK(u.groups.size() == 1 && u.groups[0] == QStringLiteral("G0000_Wheel_Axle_Assy"));
    CHECK(u.nodes.size() == 1 && u.nodes[0].parent == QStringLiteral("G0000_Wheel_Axle_Assy"));
    CHECK(u.nodes.size() == 1 && nearly(u.nodes[0].world.map(QVector3D(0, 0, 0)).x(), 1) && nearly(u.nodes[0].world.map(QVector3D(0, 0, 0)).z(), 3));   // folders carry no transform
}

void testOverlayToU3d()
{
    aiScene* scene = makeTriangleScene();
    Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene);
    delete scene;
    const int nodesBefore = u.nodes.size();
    const int meshesBefore = u.meshes.size();

    // one dark and one bright triangle (a letter with its halo, coplanar) and one yellow segment
    Pdf3D::OverlayGraphicData g;
    g.name = QStringLiteral("Distance 12.5 mm");
    g.folder = QStringLiteral("Measurements");
    g.viewDirection = QVector3D(0, 1, 0);
    g.triangles = {0, 0, 0, 0.06f, 0.06f, 0.08f,  1, 0, 0, 0.06f, 0.06f, 0.08f,  0, 0, 1, 0.06f, 0.06f, 0.08f,
                   0, 0, 0, 1.0f, 0.82f, 0.15f,   1, 0, 0, 1.0f, 0.82f, 0.15f,   0, 0, 1, 1.0f, 0.82f, 0.15f};
    g.lines = {0, 0, 0, 1.0f, 0.82f, 0.15f,  0, 0, 5, 1.0f, 0.82f, 0.15f};
    const int added = Pdf3D::OverlayToU3d::append(u, g, 0.1f);

    // ONE node per item (one entry in the model tree), whatever the number of colours: a single mesh with vertex colours, filed under
    // the group node of its folder
    CHECK(added == 1 && u.nodes.size() == nodesBefore + 1 && u.meshes.size() == meshesBefore + 1);
    CHECK(u.groups == QStringList{QStringLiteral("Measurements")});
    if (u.nodes.size() == nodesBefore + 1)
    {
        const Pdf3D::U3dNode& node = u.nodes[nodesBefore];
        const Pdf3D::U3dMesh& mesh = u.meshes[node.meshIndex];
        CHECK(node.overlay && !u.nodes[0].overlay && node.parent == QStringLiteral("Measurements") && u.nodes[0].parent.isEmpty());
        CHECK(node.name.startsWith(QStringLiteral("O0000_")) && node.overlayItem == 0 && u.overlayItems == 1);
        CHECK(node.displayName == g.name);
        // the halo triangle (3 vertices), the letter triangle (3) and the segment as two crossed ribbons (4 triangles, 12)
        CHECK(mesh.indices.size() == 3 + 3 + 12);
        CHECK(mesh.colors.size() == mesh.indices.size() * 4);
        CHECK(u.materials[mesh.materialIndex].vertexColors);
        // the darker (halo) triangles come first and sit furthest back; the bright ones are nudged towards the viewer (-view)
        CHECK(mesh.positions[1] == 0.0f && mesh.positions[3 * 3 + 1] < mesh.positions[1]);
        CHECK(nearly(mesh.colors[0], 0.06f, 0.005) && nearly(mesh.colors[3 * 4], 1.0f, 0.005) && nearly(mesh.colors[3], 1.0f, 1e-6));
    }

    // a second item reuses the material; several items share it
    const int materialsAfterFirst = u.materials.size();
    Pdf3D::OverlayGraphicData g2 = g;
    g2.name = QStringLiteral("Note");
    g2.triangles.clear();   // only the segment remains
    g2.followsNodes = QStringList{u.nodes[0].name};
    Pdf3D::OverlayToU3d::append(u, g2, 0.1f);
    CHECK(u.materials.size() == materialsAfterFirst);
    CHECK(u.nodes.last().name.startsWith(QStringLiteral("O0001_")) && u.nodes.last().overlayItem == 1 && u.overlayItems == 2);
    CHECK(u.nodes.last().follows == QStringList{u.nodes[0].name});
    CHECK(u.nodes.last().parent == QStringLiteral("Measurements") && u.groups.size() == 1);   // the folder is shared, not repeated

    // an annotation goes to its own folder
    Pdf3D::OverlayGraphicData g3 = g;
    g3.name = QStringLiteral("Wooden board");
    g3.folder = QStringLiteral("Annotations");
    Pdf3D::OverlayToU3d::append(u, g3, 0.1f);
    CHECK(u.groups == (QStringList{QStringLiteral("Measurements"), QStringLiteral("Annotations")}) && u.nodes.last().parent == QStringLiteral("Annotations"));

    // degenerate input adds nothing
    Pdf3D::OverlayGraphicData empty;
    CHECK(Pdf3D::OverlayToU3d::append(u, empty, 0.1f) == 0 && u.overlayItems == 3);
}

void testFittedViews()
{
    // a long flat box 10 x 1 x 0.2 (Z up): the fitted Front view (looking along +Y at the 10 x 0.2 face) is closer than the sphere
    // fit, and every fitted view keeps all corners in front of the eye
    const QVector3D lo(-5, -0.5f, -0.1f), hi(5, 0.5f, 0.1f);
    const QList<QByteArray> fitted = Pdf3D::standardViewsFitted(lo, hi, 1.5f, Pdf3D::UpAxis::Z);
    const QList<QByteArray> sphere = Pdf3D::standardViews((lo + hi) * 0.5f, 0.5f * (hi - lo).length(), Pdf3D::UpAxis::Z);
    CHECK(fitted.size() == 7 && sphere.size() == 7);
    if (fitted.size() == 7 && sphere.size() == 7)
    {
        const QRegularExpression coRe(QStringLiteral("/CO ([0-9.]+)"));
        const double front = coRe.match(QString::fromLatin1(fitted[1])).captured(1).toDouble();
        const double frontSphere = coRe.match(QString::fromLatin1(sphere[1])).captured(1).toDouble();
        CHECK(front > 0.0 && front < frontSphere);
        // Front: the 10 wide box must fit the horizontal field: half width 5 * 1.05 over tanX (tan 15 deg * 1.5), plus the
        // half depth of the box (the near face is 0.5 closer to the eye than the centre): 5.25 / 0.4019 + 0.5
        CHECK(nearly(front, 5.25 / (std::tan(15.0 * 3.14159265358979 / 180.0) * 1.5) + 0.5, 0.05));
    }
}

void testCameraViews()
{
    Pdf3D::CameraViewSpec persp;
    persp.name = QStringLiteral("Saved");
    persp.eye = QVector3D(0, -10, 0);
    persp.direction = QVector3D(0, 2, 0);   // not normalised on purpose
    persp.up = QVector3D(0, 0, 1);
    persp.fovDegrees = 45.0f;
    const QByteArray pv = Pdf3D::makeCameraView(persp, QVector3D(0, 0, 0), 5.0f);
    CHECK(pv.contains("/XN (Saved)") && pv.contains("/Subtype /P") && pv.contains("/FOV 45") && !pv.contains("/NA"));
    CHECK(pv.contains("/SA []"));   // a view without a section clears the previous view's section
    CHECK(pv.contains("/RM << /Type /3DRenderMode /Subtype /Solid >>"));
    Pdf3D::CameraViewSpec wire = persp;
    wire.renderMode = "Wireframe";
    CHECK(Pdf3D::makeCameraView(wire, QVector3D(0, 0, 0), 5.0f).contains("/RM << /Type /3DRenderMode /Subtype /Wireframe >>"));
    // the orbit centre is the scene centre projected on the view axis: 10 units ahead of the eye
    const QRegularExpressionMatch co = QRegularExpression(QStringLiteral("/CO ([0-9.]+)")).match(QString::fromLatin1(pv));
    CHECK(co.hasMatch() && nearly(co.captured(1).toDouble(), 10.0, 1.0e-3));

    Pdf3D::CameraViewSpec ortho = persp;
    ortho.name = QStringLiteral("Ortho ") + QChar(0x00e4);   // non-ASCII: written as a UTF-16 text string
    ortho.orthographic = true;
    ortho.orthoRange = 2.5f;
    ortho.nodes = {{QStringLiteral("N0000_a"), true}, {QStringLiteral("N0001_b"), false}};
    const QByteArray ov = Pdf3D::makeCameraView(ortho, QVector3D(0, 0, 0), 5.0f);
    // orthographic views are narrow-field perspective views: 2.5 world units across the smaller dimension at a 2 degree field
    CHECK(ov.contains("/Subtype /P") && ov.contains("/FOV 2") && ov.contains("/XN <FEFF"));
    {
        const QRegularExpressionMatch oco = QRegularExpression(QStringLiteral("/CO ([0-9.]+)")).match(QString::fromLatin1(ov));
        const double expected = 0.5 * 2.5 / std::tan(1.0 * 3.14159265358979 / 180.0);
        CHECK(oco.hasMatch() && nearly(oco.captured(1).toDouble(), expected, 1.0e-2));
    }
    CHECK(ov.contains("/Type /3DNode /N (N0000_a) /V true") && ov.contains("/N (N0001_b) /V false"));

    // per-node transform: translation by (1, 2, 3) is written as the 3x3 rows followed by the translation
    Pdf3D::CameraViewSpec moved = persp;
    Pdf3D::NodeVisibility nv;
    nv.node = QStringLiteral("N0000_a");
    nv.hasMatrix = true;
    nv.matrix.translate(1.0f, 2.0f, 3.0f);
    moved.nodes.append(nv);
    const QByteArray mv = Pdf3D::makeCameraView(moved, QVector3D(0, 0, 0), 5.0f);
    CHECK(mv.contains("/N (N0000_a) /V true /M [1 0 0 0 1 0 0 0 1 1 2 3]"));

    // cross sections: the centre is relative to the scene centre; angle pairs verified in Acrobat (see makeCrossSection)
    Pdf3D::SectionPlane plane;
    plane.axis = 2;
    plane.position = 30.0;
    plane.keepPositive = false;
    const QByteArray sz = Pdf3D::makeCrossSection(plane, QVector3D(0, 0, 20));
    CHECK(sz.contains("/C [0 0 10]") && sz.contains("/O [0 0 null]") && sz.contains("/SC true") && sz.contains("/PV false") && sz.contains("/IV true"));
    plane.keepPositive = true;
    CHECK(Pdf3D::makeCrossSection(plane, QVector3D(0, 0, 20)).contains("/O [180 0 null]"));
    plane.axis = 1;
    plane.position = 7.0;
    CHECK(Pdf3D::makeCrossSection(plane, QVector3D(1, 2, 3)).contains("/C [0 5 0]") && Pdf3D::makeCrossSection(plane, QVector3D(1, 2, 3)).contains("/O [90 0 null]"));
    plane.keepPositive = false;
    CHECK(Pdf3D::makeCrossSection(plane, QVector3D(1, 2, 3)).contains("/O [-90 0 null]"));
    plane.axis = 0;
    plane.position = 12.0;
    CHECK(Pdf3D::makeCrossSection(plane, QVector3D(2, 0, 0)).contains("/C [10 0 0]") && Pdf3D::makeCrossSection(plane, QVector3D(2, 0, 0)).contains("/O [0 90 null]"));
    plane.keepPositive = true;
    CHECK(Pdf3D::makeCrossSection(plane, QVector3D(2, 0, 0)).contains("/O [0 -90 null]"));

    Pdf3D::CameraViewSpec sectioned = persp;
    sectioned.sections.append(plane);
    CHECK(Pdf3D::makeCameraView(sectioned, QVector3D(0, 0, 0), 5.0f).contains("/SA [<< /Type /3DCrossSection"));
    Pdf3D::SectionPlane second = plane;
    second.axis = 2;
    sectioned.sections.append(second);   // several planes in one view are all written
    CHECK(Pdf3D::makeCameraView(sectioned, QVector3D(0, 0, 0), 5.0f).count("/Type /3DCrossSection") == 2);
    CHECK(Pdf3D::makeCameraView(persp, QVector3D(0, 0, 0), 5.0f).contains("/SA []"));

    // a scene centre behind the eye still gives a positive orbit distance
    Pdf3D::CameraViewSpec behind = persp;
    behind.direction = QVector3D(0, -1, 0);
    const QByteArray bv = Pdf3D::makeCameraView(behind, QVector3D(0, 0, 0), 5.0f);
    const QRegularExpressionMatch bco = QRegularExpression(QStringLiteral("/CO ([0-9.]+)")).match(QString::fromLatin1(bv));
    CHECK(bco.hasMatch() && bco.captured(1).toDouble() > 0.0);
}

void testSceneToU3d()
{
    aiScene* scene = makeTriangleScene();
    QVector<QMatrix4x4> pre;
    QMatrix4x4 shift;
    shift.translate(10, 0, 0);
    pre.append(shift);

    const Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene, pre);
    CHECK(u.nodes.size() == 1 && u.meshes.size() == 1 && u.materials.size() == 1);
    if (u.nodes.size() == 1 && u.meshes.size() == 1 && u.materials.size() == 1)
    {
        CHECK(u.nodes[0].displayName == QLatin1String("Part A"));
        CHECK(!u.nodes[0].name.contains(QLatin1Char(' ')));   // ASCII-only identifier
        const QVector3D origin = u.nodes[0].world.map(QVector3D(0, 0, 0));
        CHECK(nearly(origin.x(), 11) && nearly(origin.y(), 2) && nearly(origin.z(), 3));
        CHECK(u.meshes[0].positions.size() == 9 && u.meshes[0].normals.size() == 9 && u.meshes[0].indices.size() == 3);
        CHECK(nearly(u.materials[0].diffuse.x(), 0.1) && nearly(u.materials[0].diffuse.z(), 0.3));
    }
    QVector3D lo, hi;
    CHECK(u.computeBounds(&lo, &hi) && nearly(lo.x(), 11) && nearly(hi.y(), 3));

    const Pdf3D::U3dScene withoutPre = Pdf3D::SceneToU3d::build(scene);
    CHECK(withoutPre.nodes.size() == 1 && nearly(withoutPre.nodes[0].world.map(QVector3D(0, 0, 0)).x(), 1));
    delete scene;

    CHECK(Pdf3D::SceneToU3d::build(nullptr).isEmpty());
}

void testVertexColors()
{
    // uniform vertex colour: becomes a tinted material (vertex colour times base colour), no per-vertex data
    aiScene* scene = makeTriangleScene();
    scene->mMeshes[0]->mColors[0] = new aiColor4D[3]{aiColor4D(1, 0, 0, 1), aiColor4D(1, 0, 0, 1), aiColor4D(1, 0, 0, 1)};
    Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene);
    delete scene;
    CHECK(u.meshes.size() == 1 && u.meshes[0].colors.isEmpty());
    if (u.meshes.size() == 1 && u.meshes[0].materialIndex >= 0 && u.meshes[0].materialIndex < u.materials.size())
    {
        const Pdf3D::U3dMaterial& m = u.materials[u.meshes[0].materialIndex];
        CHECK(!m.vertexColors && nearly(m.diffuse.x(), 0.1) && nearly(m.diffuse.y(), 0.0) && nearly(m.diffuse.z(), 0.0));
    }

    // varying vertex colours: kept per vertex, material switched to vertex colours
    scene = makeTriangleScene();
    scene->mMeshes[0]->mColors[0] = new aiColor4D[3]{aiColor4D(1, 0, 0, 1), aiColor4D(0, 1, 0, 1), aiColor4D(0, 0, 1, 1)};
    u = Pdf3D::SceneToU3d::build(scene);
    delete scene;
    CHECK(u.meshes.size() == 1 && u.meshes[0].colors.size() == 12);
    if (u.meshes.size() == 1 && u.meshes[0].materialIndex >= 0 && u.meshes[0].materialIndex < u.materials.size())
        CHECK(u.materials[u.meshes[0].materialIndex].vertexColors);

    // all white: the base material is used unchanged
    scene = makeTriangleScene();
    scene->mMeshes[0]->mColors[0] = new aiColor4D[3]{aiColor4D(1, 1, 1, 1), aiColor4D(1, 1, 1, 1), aiColor4D(1, 1, 1, 1)};
    u = Pdf3D::SceneToU3d::build(scene);
    delete scene;
    CHECK(u.materials.size() == 1 && u.meshes.size() == 1 && u.meshes[0].colors.isEmpty());

    // the writer accepts a coloured mesh (per-vertex path) when the modules are present
    QStringList missing;
    if (Pdf3D::U3dWriter::modulesAvailable(&missing))
    {
        scene = makeTriangleScene();
        scene->mMeshes[0]->mColors[0] = new aiColor4D[3]{aiColor4D(1, 0, 0, 1), aiColor4D(0, 1, 0, 1), aiColor4D(0, 0, 1, 1)};
        u = Pdf3D::SceneToU3d::build(scene);
        delete scene;
        const Pdf3D::U3dWriteResult r = Pdf3D::U3dWriter::write(u);
        CHECK(r.ok && r.data.startsWith("U3D"));
    }
}

void testAppender()
{
    const QByteArray original = makeQtPdf();
    CHECK(original.startsWith("%PDF-1.6"));
    // the structure assumptions of PdfAppender (research doc 9.1)
    CHECK(original.contains("\nxref\n") && original.contains("/Annots") && !original.contains("/ObjStm") && !original.contains("/XRef"));

    Pdf3D::Annotation3D a;
    a.rect = QRectF(QPointF(36, 100), QPointF(800, 500));
    a.u3d = QByteArray("U3D-test-data-") + QByteArray(2000, 'x');
    a.viewDictionaries = Pdf3D::standardViews(QVector3D(0, 0, 0), 5.0f, Pdf3D::UpAxis::Y);
    a.description = QStringLiteral("3D model");

    QByteArray out = original;
    QString error;
    CHECK(Pdf3D::PdfAppender::append3DAnnotation(out, a, &error));
    CHECK(error.isEmpty());
    CHECK(out.size() > original.size() && out.startsWith(original));   // append only, original bytes untouched
    CHECK(out.endsWith("%%EOF\n"));

    const QByteArray update = out.mid(original.size());
    const QString updateText = QString::fromLatin1(update);
    CHECK(updateText.contains(QStringLiteral("/Subtype /3D")) && updateText.contains(QStringLiteral("/Subtype /U3D")));
    CHECK(updateText.contains(QStringLiteral("/Type /3DView")) && updateText.contains(QStringLiteral("/3DD ")) && updateText.contains(QStringLiteral("/3DV 0")));
    CHECK(updateText.contains(QStringLiteral("/FlateDecode")));

    // new startxref points at a classic xref section, whose /Prev is the original startxref
    const int sx = out.lastIndexOf("startxref");
    const qint64 newXref = out.mid(sx + 9, 24).trimmed().split('\n').first().toLongLong();
    CHECK(newXref >= original.size() && out.mid(static_cast<int>(newXref), 5) == "xref\n");
    const int origSx = original.lastIndexOf("startxref");
    const qint64 origXref = original.mid(origSx + 9, 24).trimmed().split('\n').first().toLongLong();
    CHECK(updateText.contains(QStringLiteral("/Prev %1").arg(origXref)));

    // every entry of the new xref section points at "N 0 obj"
    {
        int pos = static_cast<int>(newXref) + 5;
        int entries = 0;
        for (;;)
        {
            const int eol = out.indexOf('\n', pos);
            const QByteArray line = out.mid(pos, eol - pos).trimmed();
            if (line.startsWith("trailer"))
                break;
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() != 2)
            {
                CHECK(false);
                break;
            }
            const int objNum = parts[0].toInt();
            pos = eol + 1;
            const qint64 off = out.mid(pos, 10).toLongLong();
            CHECK(out.mid(pos + 10, 10) == " 00000 n \n");
            CHECK(out.mid(static_cast<int>(off), QByteArray::number(objNum).size() + 6) == QByteArray::number(objNum) + " 0 obj");
            pos += 20;
            ++entries;
        }
        CHECK(entries == 4);   // 3D stream, appearance, annotation, rewritten /Annots array
    }

    // the page's indirect /Annots array object was rewritten with the new annotation appended
    const QRegularExpressionMatch annots = QRegularExpression(QStringLiteral("/Annots (\\d+) 0 R")).match(QString::fromLatin1(original));
    CHECK(annots.hasMatch());
    if (annots.hasMatch())
    {
        const QRegularExpression rewritten(QStringLiteral("(?:^|\\n)%1 0 obj\\n\\[ ?([^\\]]*)\\]").arg(annots.captured(1)));
        const QRegularExpressionMatch r = rewritten.match(updateText);
        CHECK(r.hasMatch() && r.captured(1).contains(QStringLiteral(" 0 R")));
    }

    // error paths: nothing to append to, and a cross-reference stream file
    QByteArray garbage("not a pdf");
    CHECK(!Pdf3D::PdfAppender::append3DAnnotation(garbage, a, &error) && !error.isEmpty());
    QByteArray xrefStream("%PDF-1.5\n1 0 obj\n<< >>\nendobj\nstartxref\n9\n%%EOF\n");
    CHECK(!Pdf3D::PdfAppender::append3DAnnotation(xrefStream, a, &error));
    Pdf3D::Annotation3D empty;
    QByteArray copy = original;
    CHECK(!Pdf3D::PdfAppender::append3DAnnotation(copy, empty, &error) && copy == original);
}

void testU3dWriter()
{
    QStringList missing;
    if (!Pdf3D::U3dWriter::modulesAvailable(&missing))
    {
        std::printf("SKIP u3d writer test: modules missing next to the test executable: %s\n", qPrintable(missing.join(QStringLiteral(", "))));
        return;
    }
    aiScene* scene = makeTriangleScene();
    const Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene);
    delete scene;

    const Pdf3D::U3dWriteResult r = Pdf3D::U3dWriter::write(u);
    CHECK(r.ok);
    if (!r.ok)
        std::fprintf(stderr, "u3d writer error: %s\n", qPrintable(r.error));
    CHECK(r.data.size() > 100 && r.data.startsWith("U3D"));   // ECMA-363 file header block type 0x00443355

    // an animation (key frames of the part node) makes the stream larger: motion resource + animation modifier
    {
        Pdf3D::U3dScene animated = u;
        Pdf3D::U3dMotionTrack track;
        track.node = animated.nodes[0].name;
        track.times = {0.0f, 1.0f, 2.0f};
        QMatrix4x4 lifted;
        lifted.translate(0, 0, 5);
        QMatrix4x4 turned;
        turned.rotate(90.0f, 0, 0, 1);
        track.keys = {QMatrix4x4(), lifted, turned};
        animated.animation.name = QStringLiteral("Lift");
        animated.animation.tracks.append(track);
        CHECK(animated.hasAnimation() && !u.hasAnimation());
        const Pdf3D::U3dWriteResult ra = Pdf3D::U3dWriter::write(animated);
        CHECK(ra.ok && ra.data.size() > r.data.size());
        if (!ra.ok)
            std::fprintf(stderr, "animated u3d writer error: %s\n", qPrintable(ra.error));
    }

    // a second export in the same process works (library initialised and shut down per call)
    CHECK(Pdf3D::U3dWriter::write(u).ok);

    CHECK(!Pdf3D::U3dWriter::write(Pdf3D::U3dScene()).ok);   // empty scene is refused, not crashed on
}

void testFullExport()
{
    QStringList missing;
    if (!Pdf3D::U3dWriter::modulesAvailable(&missing))
    {
        std::printf("SKIP full export test: u3d modules missing\n");
        return;
    }
    QTemporaryDir dir;
    CHECK(dir.isValid());
    aiScene* scene = makeTriangleScene();
    const Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(scene);
    delete scene;

    Pdf3D::Pdf3DOptions options;
    options.title = QStringLiteral("Test sheet");
    options.subtitle = QStringLiteral("triangle.glb");
    const QString path = dir.filePath(QStringLiteral("out.pdf"));
    const Pdf3D::Pdf3DResult r = Pdf3D::Pdf3DExporter::exportToFile(u, options, path);
    CHECK(r.ok);
    if (!r.ok)
        std::fprintf(stderr, "export error: %s\n", qPrintable(r.error));

    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    const QByteArray pdf = f.readAll();
    CHECK(pdf.startsWith("%PDF-1.6") && pdf.size() > 1500);
    CHECK(pdf.contains("/Subtype /3D") && pdf.contains("/Type /3DView") && pdf.endsWith("%%EOF\n"));
    CHECK(pdf.count("/Type /3DView") == 7);

    // title block fields + parts list: the list adds a second page (the 3D annotation stays on page 1); without it one page
    const QRegularExpression twoPages(QStringLiteral("/Count\\s+2\\b"));
    const QRegularExpression onePage(QStringLiteral("/Count\\s+1\\b"));
    CHECK(twoPages.match(QString::fromLatin1(pdf)).hasMatch() && !onePage.match(QString::fromLatin1(pdf)).hasMatch());

    options.author = QStringLiteral("A. Author");
    options.company = QStringLiteral("ACME");
    options.drawingNumber = QStringLiteral("DWG-001");
    options.revision = QStringLiteral("B");
    options.scale = QStringLiteral("1:2");
    options.notes = QStringLiteral("note");
    options.includePartsList = false;
    const QString path2 = dir.filePath(QStringLiteral("out2.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, options, path2).ok);
    QFile f2(path2);
    CHECK(f2.open(QIODevice::ReadOnly));
    const QByteArray pdf2 = f2.readAll();
    CHECK(onePage.match(QString::fromLatin1(pdf2)).hasMatch() && pdf2.contains("/Subtype /3D"));
    CHECK(!pdf2.contains("/Subtype /Widget"));   // short notes stay plain text in the title block

    // a view that moves a part: every view (the seven standard ones too) lists that part, so the explosion does not carry over
    Pdf3D::Pdf3DOptions moved = options;
    moved.notes.clear();
    Pdf3D::CameraViewSpec exploded;
    exploded.name = QStringLiteral("Exploded");
    exploded.eye = QVector3D(0, -10, 0);
    exploded.direction = QVector3D(0, 1, 0);
    exploded.up = QVector3D(0, 0, 1);
    Pdf3D::NodeVisibility moveNode;
    moveNode.node = u.nodes[0].name;
    moveNode.hasMatrix = true;
    moveNode.matrix.translate(5.0f, 0.0f, 0.0f);
    exploded.nodes.append(moveNode);
    moved.extraViews.append(exploded);
    const QString pathMoved = dir.filePath(QStringLiteral("moved.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, moved, pathMoved).ok);
    QFile fMoved(pathMoved);
    CHECK(fMoved.open(QIODevice::ReadOnly));
    const QByteArray pdfMoved = fMoved.readAll();
    CHECK(pdfMoved.count("/Type /3DNode") == 8 && pdfMoved.count("/M [") >= 8);   // 7 standard views + the exploded one

    // an item attached to the moved part follows it in the view that moves it, and sits at its place in every other view; another
    // view can hide it
    Pdf3D::U3dScene withOverlay = u;
    {
        Pdf3D::OverlayGraphicData og;
        og.name = QStringLiteral("m1");
        og.viewDirection = QVector3D(0, 1, 0);
        og.followsNodes = QStringList{u.nodes[0].name};
        og.triangles = {0, 0, 0, 1, 1, 1,  1, 0, 0, 1, 1, 1,  0, 0, 1, 1, 1, 1};
        Pdf3D::OverlayToU3d::append(withOverlay, og, 0.1f);
    }
    Pdf3D::Pdf3DOptions followOptions = moved;
    {
        // as the app builds an exploded view: the part's matrix is the explosion times its exported world matrix
        QMatrix4x4 explosion;
        explosion.translate(5.0f, 0.0f, 0.0f);
        followOptions.extraViews[0].nodes[0].matrix = explosion * u.nodes[0].world;
    }
    Pdf3D::CameraViewSpec hidden = exploded;
    hidden.name = QStringLiteral("No measurements");
    hidden.nodes.clear();
    hidden.showOverlays = false;
    followOptions.extraViews.append(hidden);
    const QString pathFollow = dir.filePath(QStringLiteral("follow.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(withOverlay, followOptions, pathFollow).ok);
    QFile fFollow(pathFollow);
    CHECK(fFollow.open(QIODevice::ReadOnly));
    const QByteArray pdfFollow = fFollow.readAll();
    // 9 views (7 standard + moved + hidden) list the part and the overlay node: 18 node entries
    CHECK(pdfFollow.count("/Type /3DNode") == 18);
    CHECK(pdfFollow.count("/N (O0000_m1) /V true /M [1 0 0 0 1 0 0 0 1 5 0 0]") == 1);   // follows the part in the moved view only
    CHECK(pdfFollow.count("/N (O0000_m1) /V false") == 1);                               // hidden in the view that asks for it
    CHECK(pdfFollow.count("/N (O0000_m1) /V true /M [1 0 0 0 1 0 0 0 1 0 0 0]") == 7);   // default place elsewhere

    // an animation: the style (/AN) in the 3D stream dictionary, the annotation live on opening; none without an animation
    Pdf3D::U3dScene animatedScene = u;
    {
        Pdf3D::U3dMotionTrack track;
        track.node = u.nodes[0].name;
        track.times = {0.0f, 1.0f};
        QMatrix4x4 lifted;
        lifted.translate(0, 0, 5);
        track.keys = {QMatrix4x4(), lifted};
        animatedScene.animation.tracks.append(track);
    }
    Pdf3D::Pdf3DOptions animOptions = options;
    animOptions.notes.clear();
    animOptions.animationPlayback = Pdf3D::AnimationPlayback::BackAndForth;
    const QString pathAnim = dir.filePath(QStringLiteral("anim.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(animatedScene, animOptions, pathAnim).ok);
    QFile fAnim(pathAnim);
    CHECK(fAnim.open(QIODevice::ReadOnly));
    const QByteArray pdfAnim = fAnim.readAll();
    CHECK(pdfAnim.contains("/AN << /Type /3DAnimationStyle /Subtype /Oscillating /PC -1 /TM 1 >>") && pdfAnim.contains("/AIS /L /DIS /I"));
    animOptions.animationPlayback = Pdf3D::AnimationPlayback::Once;
    animOptions.animationAutoStart = false;
    const QString pathAnim2 = dir.filePath(QStringLiteral("anim_once.pdf"));   // not the first file: it is still open (Windows cannot replace it)
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(animatedScene, animOptions, pathAnim2).ok);
    QFile fAnim2(pathAnim2);
    CHECK(fAnim2.open(QIODevice::ReadOnly));
    const QByteArray pdfAnim2 = fAnim2.readAll();
    CHECK(pdfAnim2.contains("/Subtype /Linear /PC 1 /TM 1 >>") && !pdfAnim2.contains("/AIS /L"));
    CHECK(!pdf.contains("/3DAnimationStyle") && !pdf.contains("/AIS /L"));   // no animation, no style

    // standard views can be left out: only "Top" remains, and with none left the export is refused
    Pdf3D::Pdf3DOptions onlyTop = options;
    onlyTop.notes.clear();
    for (const QString& name : Pdf3D::standardViewNames())
    {
        if (name != QStringLiteral("Top"))
            onlyTop.excludedStandardViews.insert(name);
    }
    const QString pathTop = dir.filePath(QStringLiteral("top.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, onlyTop, pathTop).ok);
    QFile fTop(pathTop);
    CHECK(fTop.open(QIODevice::ReadOnly));
    const QByteArray pdfTop = fTop.readAll();
    CHECK(pdfTop.count("/Type /3DView") == 1 && pdfTop.contains("/XN (Top)"));
    onlyTop.excludedStandardViews.insert(QStringLiteral("Top"));
    CHECK(!Pdf3D::Pdf3DExporter::exportToFile(u, onlyTop, dir.filePath(QStringLiteral("none.pdf"))).ok);

    // long notes do not fit the title block: they become a read-only multi-line form field (scrollable in Acrobat)
    options.includePartsList = true;   // two sheets: the notes field must appear on both
    options.notes = QString();
    for (int i = 0; i < 40; ++i)
        options.notes += QStringLiteral("Note line %1 with some words to fill the cell. ").arg(i);
    const QString path3 = dir.filePath(QStringLiteral("out3.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, options, path3).ok);
    QFile f3(path3);
    CHECK(f3.open(QIODevice::ReadOnly));
    const QByteArray pdf3 = f3.readAll();
    CHECK(pdf3.count("/Subtype /Widget") == 2 && pdf3.contains("/AcroForm") && pdf3.contains("/Ff 4198400") && pdf3.contains("/Subtype /3D"));
    CHECK(pdf3.endsWith("%%EOF\n"));
}
}

// ---- Acrobat spikes through the real exporter (run with --spikes <folder>): animated PDFs that differ in one thing each, to open in
// Acrobat and see which ones get an active play button. Not part of the checks.
Pdf3D::U3dScene spikeScene(int parts, bool grouped)
{
    aiScene* sc = makeTriangleScene();
    Pdf3D::U3dScene u = Pdf3D::SceneToU3d::build(sc);
    delete sc;
    const Pdf3D::U3dNode base = u.nodes[0];
    u.nodes.clear();
    if (grouped)
    {
        u.groups = QStringList{QStringLiteral("Folder")};
        u.groupParents = QStringList{QString()};
    }
    for (int i = 0; i < parts; ++i)
    {
        Pdf3D::U3dNode n = base;
        n.name = QStringLiteral("N%1_Part").arg(i, 4, 10, QLatin1Char('0'));
        n.displayName = QStringLiteral("Part %1").arg(i);
        n.world = QMatrix4x4();
        n.world.translate(40.0f * i, 0.0f, 0.0f);
        n.world.scale(30.0f);
        n.parent = grouped ? QStringLiteral("Folder") : QString();
        u.nodes.push_back(n);
    }
    return u;
}

void addSpikeTrack(Pdf3D::U3dScene& u, int part, int keys, double duration)
{
    Pdf3D::U3dMotionTrack track;
    track.node = u.nodes[part].name;
    for (int k = 0; k < keys; ++k)
    {
        const double f = static_cast<double>(k) / static_cast<double>(keys - 1);
        QMatrix4x4 key;                                   // in the part's own frame (the part is scaled by 30): a lift and a turn
        key.translate(0.0f, 0.0f, static_cast<float>(1.0 * std::sin(3.14159265 * f)));
        key.rotate(static_cast<float>(90.0 * f), 0.0f, 0.0f, 1.0f);
        track.times.push_back(static_cast<float>(duration * f));
        track.keys.push_back(key);
    }
    u.animation.name = QStringLiteral("Spike");
    u.animation.tracks.push_back(track);
}

void writeSpike(const QString& folder, const QString& name, const Pdf3D::U3dScene& u)
{
    Pdf3D::Pdf3DOptions o;
    o.title = name;
    o.includePartsList = false;
    const Pdf3D::Pdf3DResult r = Pdf3D::Pdf3DExporter::exportToFile(u, o, QDir(folder).filePath(name + QStringLiteral(".pdf")));
    std::printf("%s: %s\n", qPrintable(name), r.ok ? "written" : qPrintable(r.error));
}

void writeAnimationSpikes(const QString& folder)
{
    QDir().mkpath(folder);
    { Pdf3D::U3dScene u = spikeScene(1, false); addSpikeTrack(u, 0, 3, 3.0);   writeSpike(folder, QStringLiteral("anim_A_one_part_root_3keys"), u); }
    { Pdf3D::U3dScene u = spikeScene(1, true);  addSpikeTrack(u, 0, 3, 3.0);   writeSpike(folder, QStringLiteral("anim_B_one_part_in_folder_3keys"), u); }
    { Pdf3D::U3dScene u = spikeScene(1, false); addSpikeTrack(u, 0, 121, 6.0); writeSpike(folder, QStringLiteral("anim_C_one_part_root_121keys"), u); }
    { Pdf3D::U3dScene u = spikeScene(6, true);  for (int i = 0; i < 6; ++i) addSpikeTrack(u, i, 3, 3.0); writeSpike(folder, QStringLiteral("anim_D_six_parts_in_folder_3keys"), u); }
}

int main(int argc, char** argv)
{
    bool full = false;
    QString spikeFolder;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--full") == 0)
            full = true;
        else if (std::strcmp(argv[i], "--spikes") == 0 && i + 1 < argc)
            spikeFolder = QString::fromLocal8Bit(argv[++i]);
    }

    // QPdfWriter/QPainter want a GUI application object. vcpkg's Qt has no "offscreen" platform plugin on Windows, so use the
    // regular "windows" platform there (a desktop session is enough, no window is shown); "offscreen" elsewhere. Set
    // QT_QPA_PLATFORM yourself to override.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    QGuiApplication app(argc, argv);

    if (!spikeFolder.isEmpty())
    {
        writeAnimationSpikes(spikeFolder);
        return 0;
    }

    testViews();
    testCameraViews();
    testFittedViews();
    testClippingStateJson();
    testAssemblyFolders();
    testOverlayToU3d();
    testSceneToU3d();
    testVertexColors();
    testAppender();
    testU3dWriter();
    if (full)
        testFullExport();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
