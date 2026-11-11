// Tests for the Save as 3D PDF export (src/Pdf3D). GUI-free except the optional end-to-end test.
//
//   pdf3d_tests          view dictionaries, aiScene conversion, sheet templates (parser, rectangles, placeholders, user folder),
//                        incremental PDF update on a real QPdfWriter page, the
//                        in-process u3d conversion (skipped when the u3d modules are not next to the test executable)
//   pdf3d_tests --full   additionally runs Pdf3DExporter::exportToFile (text layout and fonts) and checks the written file
#include "ClippingState.h"
#include "ViewOverlayVisibility.h"
#include "InPlaneText.h"
#include "OverlayToU3d.h"
#include "PdfAppender.h"
#include "Pdf3DExporter.h"
#include "Pdf3DViews.h"
#include "SceneToU3d.h"
#include "PartData.h"
#include "SheetRenderer.h"
#include "PrintPage.h"
#include "PrintPreset.h"
#include "SheetTemplate.h"
#include "U3dWriter.h"
#include "U3dWorker.h"

#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <functional>
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

void testAnnotationText()
{
    for (const QString& text : {QStringLiteral("N"), QStringLiteral("n"),
                               QStringLiteral("Nescafe"), QStringLiteral("nescafe")})
    {
        const auto& outline = InPlaneText::outline(text);
        float firstTriangle = outline.right;
        for (size_t i = 0; i < outline.uv.size(); i += 2)
            firstTriangle = std::min(firstTriangle, outline.uv[i]);
        CHECK(!outline.uv.empty());
        CHECK(nearly(firstTriangle, outline.left));
    }
}

void testViewOverlayVisibilityJson()
{
    const QUuid measurement = QUuid::createUuid();
    const QUuid annotation = QUuid::createUuid();
    const ViewOverlayVisibility captured{{measurement}, {annotation}};
    const auto restored = viewOverlayVisibilityFromJson(viewOverlayVisibilityToJson(captured));
    CHECK(restored && restored->measurements == captured.measurements && restored->annotations == captured.annotations);

    const auto hidden = viewOverlayVisibilityFromJson(viewOverlayVisibilityToJson({}));
    CHECK(hidden && hidden->measurements.isEmpty() && hidden->annotations.isEmpty());
    CHECK(!viewOverlayVisibilityFromJson(QJsonValue()));
    CHECK(!viewOverlayVisibilityFromJson(QJsonObject{{QStringLiteral("measurements"), QJsonArray()}}));
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

    // page buttons: a caption that does not fit a 90 pt button is cut to its start and ".." in the button's /MK /CA too (Acrobat redraws
    // the face from it, and would clip the full text at both ends), while the tooltip keeps the full name
    Pdf3D::Annotation3D withButtons = a;
    for (const QString& caption : { QStringLiteral("Front"), QStringLiteral("State: Exploded View 1") })
    {
        Pdf3D::PageButton b;
        b.rect = QRectF(QPointF(40, 20), QPointF(130, 40));
        b.caption = caption;
        b.action = "<< /S /GoTo3DView /TA @ANNOT@ /V 0 >>";
        withButtons.buttons.append(b);
    }
    QByteArray buttonPdf = original;
    CHECK(Pdf3D::PdfAppender::append3DAnnotation(buttonPdf, withButtons, &error));
    const QString buttonText = QString::fromLatin1(buttonPdf.mid(original.size()));
    CHECK(buttonText.contains(QStringLiteral("/TU (Front)")) && buttonText.contains(QStringLiteral("/MK << /CA (Front)")));
    CHECK(buttonText.contains(QStringLiteral("/TU (State: Exploded View 1)")));
    CHECK(buttonText.contains(QStringLiteral("/MK << /CA (State: Exploded Vi..)")));
    CHECK(!buttonText.contains(QStringLiteral("/MK << /CA (State: Exploded View 1)")));
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

constexpr char kSlowEncoderMarker[] = "MODELVIEWER_TEST_U3D_CANCEL_MARKER";

void testCancelRunningEncoder(const Pdf3D::U3dScene& scene, const QString& folder)
{
    const QString marker = QDir(folder).filePath(QStringLiteral("encoder-started.txt"));
    const QByteArray oldMarker = qgetenv(kSlowEncoderMarker);
    qputenv(kSlowEncoderMarker, marker.toUtf8().toBase64());

    QElapsedTimer timeout, afterCancel;
    timeout.start();
    const auto result = Pdf3D::encodeU3dInWorker(QCoreApplication::applicationFilePath(), scene, [&] {
        if (QFile::exists(marker))
        {
            if (!afterCancel.isValid())
                afterCancel.start();
            return true;
        }
        return timeout.elapsed() > 10000;
    });

    if (oldMarker.isNull())
        qunsetenv(kSlowEncoderMarker);
    else
        qputenv(kSlowEncoderMarker, oldMarker);
    CHECK(afterCancel.isValid() && afterCancel.elapsed() < 4000);
    CHECK(result.canceled && !result.ok && result.data.isEmpty());
    QFile file(marker);
    CHECK(file.open(QIODevice::ReadOnly));
    const QString workspace = QString::fromUtf8(file.readAll());
    CHECK(!workspace.isEmpty() && !QDir(workspace).exists());
}

void testExportCancellation(const Pdf3D::U3dScene& scene, const QString& folder)
{
    using namespace Pdf3D;
    const QString path = QDir(folder).filePath(QStringLiteral("cancel.pdf"));
    Pdf3DOptions options;
    options.title = QStringLiteral("Background export");
    ExportControl earlyControl;
    earlyControl.proceed = [](ExportStage) { return false; };
    const auto early = Pdf3DExporter::exportToFile(scene, options, path, earlyControl);
    CHECK(early.canceled && !early.ok && early.error.isEmpty());
    CHECK(!QFile::exists(path));

    const QByteArray original("original destination");
    {
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        CHECK(file.write(original) == original.size());
    }

    const auto readDestination = [&] {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };

    // Run the same backend on a worker as the application does. Check that a late cancel
    // discards the temporary output and that a later job can use the library again.
    QVector<ExportStage> stages;
    const auto run = [&](bool cancel) {
        Pdf3DResult result;
        const std::unique_ptr<QThread> worker(QThread::create([&] {
            ExportControl control;
            control.proceed = [&](ExportStage stage) {
                stages.append(stage);
                return !cancel || stage != ExportStage::CommitFile;
            };
            control.encode = [](const U3dScene& model) {
                return encodeU3dInWorker(QCoreApplication::applicationFilePath(), model, {});
            };
            result = Pdf3DExporter::exportToFile(scene, options, path, control);
        }));
        worker->start();
        worker->wait();
        return result;
    };

    const auto late = run(true);
    const QVector<ExportStage> expected = {ExportStage::EncodeModel, ExportStage::ComposeSheets,
        ExportStage::EmbedModel, ExportStage::WriteFile, ExportStage::CommitFile};
    CHECK(stages == expected);
    CHECK(late.canceled && !late.ok && late.error.isEmpty());
    CHECK(readDestination() == original);

    stages.clear();
    const auto retry = run(false);
    CHECK(stages == expected);
    CHECK(retry.ok && !retry.canceled && retry.error.isEmpty());
    const QByteArray pdf = readDestination();
    CHECK(pdf.startsWith("%PDF-1.6") && pdf.contains("/Subtype /3D"));
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
    testCancelRunningEncoder(u, dir.path());
    testExportCancellation(u, dir.path());

    Pdf3D::Pdf3DOptions options;
    options.title = QStringLiteral("Test sheet");
    options.subtitle = QStringLiteral("triangle.glb");
    options.openingView = 2;
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
    CHECK(pdf.contains("/3DV 2 "));
    // view buttons: previous, next and one per view (all seven fit), GoTo3DView actions aimed at the annotation, in an AcroForm;
    // no animation, so no JavaScript
    CHECK(pdf.count("/Subtype /Widget") == 9 && pdf.contains("/AcroForm"));
    CHECK(pdf.count("/S /GoTo3DView") == 9 && pdf.contains("/V /P") && pdf.contains("/V /N") && pdf.contains("/V 6 >>"));
    CHECK(!pdf.contains("/S /JavaScript"));
    CHECK(pdf.endsWith("%%EOF\n"));

    // title block fields + parts list: the list adds a second page (the 3D annotation stays on page 1); without it one page
    const QRegularExpression twoPages(QStringLiteral("/Count\\s+2\\b"));
    const QRegularExpression onePage(QStringLiteral("/Count\\s+1\\b"));
    CHECK(twoPages.match(QString::fromLatin1(pdf)).hasMatch() && !onePage.match(QString::fromLatin1(pdf)).hasMatch());

    Pdf3D::U3dScene annotated = u;
    Pdf3D::OverlayGraphicData overlay;
    overlay.name = QStringLiteral("Dimension");
    overlay.lines = {0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0};
    const int dimensionItem = annotated.overlayItems;
    CHECK(Pdf3D::OverlayToU3d::append(annotated, overlay, 0.01f) == 1);
    const QString dimensionNode = annotated.nodes.last().name;
    overlay.name = QStringLiteral("Note");
    CHECK(Pdf3D::OverlayToU3d::append(annotated, overlay, 0.01f) == 1);
    const QString noteNode = annotated.nodes.last().name;

    Pdf3D::Pdf3DOptions publication = options;
    Pdf3D::CameraViewSpec captured;
    captured.name = QStringLiteral("Captured subset");
    captured.eye = QVector3D(0, 0, 3);
    captured.direction = QVector3D(0, 0, -1);
    captured.visibleOverlayItems = QSet<int>{dimensionItem};
    publication.extraViews.append(captured);
    captured.name = QStringLiteral("Captured empty");
    captured.visibleOverlayItems->clear();
    publication.extraViews.append(captured);
    captured.name = QStringLiteral("Legacy view");
    captured.visibleOverlayItems.reset();
    publication.extraViews.append(captured);
    captured.name = QStringLiteral("Overlays disabled");
    captured.visibleOverlayItems = QSet<int>{dimensionItem};
    captured.showOverlays = false;
    publication.extraViews.append(captured);
    QImage detail(80, 50, QImage::Format_RGB32);
    detail.fill(Qt::red);
    QImage section(50, 80, QImage::Format_RGB32);
    section.fill(Qt::green);
    publication.companionSheets = {{QStringLiteral("Detail"), detail}, {QStringLiteral("Section"), section}};
    const QString publishedPath = dir.filePath(QStringLiteral("published-3d.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(annotated, publication, publishedPath).ok);
    QFile published(publishedPath);
    CHECK(published.open(QIODevice::ReadOnly));
    const QByteArray publishedPdf = published.readAll();
    CHECK(QRegularExpression(QStringLiteral("/Count\\s+4\\b")).match(QString::fromLatin1(publishedPdf)).hasMatch());
    CHECK(publishedPdf.count("/Subtype /3D") == 1);
    CHECK(publishedPdf.count("/Subtype /Image") >= 2);
    const auto view = [&](const QByteArray& name) {
        const qsizetype start = publishedPdf.indexOf("/XN (" + name + ")");
        if (start < 0)
            return QByteArray();
        const qsizetype end = publishedPdf.indexOf("/Type /3DView", start);
        return end < 0 ? publishedPdf.mid(start) : publishedPdf.mid(start, end - start);
    };
    const auto nodeState = [](const QString& name, bool visible) {
        return "/Type /3DNode /N " + Pdf3D::PdfAppender::pdfTextString(name) + (visible ? " /V true" : " /V false");
    };
    CHECK(view("Captured subset").contains(nodeState(dimensionNode, true)));
    CHECK(view("Captured subset").contains(nodeState(noteNode, false)));
    CHECK(view("Captured empty").contains(nodeState(dimensionNode, false)));
    CHECK(view("Captured empty").contains(nodeState(noteNode, false)));
    CHECK(view("Legacy view").contains(nodeState(dimensionNode, true)));
    CHECK(view("Legacy view").contains(nodeState(noteNode, true)));
    CHECK(view("Overlays disabled").contains(nodeState(dimensionNode, false)));
    CHECK(view("Overlays disabled").contains(nodeState(noteNode, false)));

    options.viewButtons = false;
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

    // sheet templates: the template decides the page size (Default is A4 landscape, 842 x 595 pt; A3 landscape 1191 x 842 pt), an
    // unknown template id falls back to Default, and a long note still becomes the scrollable notes field of the Default block
    const QRegularExpression a4Box(QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+84[12](\\.\\d+)?\\s+59[45](\\.\\d+)?\\s*\\]"));
    const QRegularExpression a3Box(QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+119[01](\\.\\d+)?\\s+84[12](\\.\\d+)?\\s*\\]"));
    CHECK(a4Box.match(QString::fromLatin1(pdf2)).hasMatch());
    Pdf3D::Pdf3DOptions a3 = options;
    a3.templateId = QStringLiteral("builtin:a3-landscape");
    const QString pathA3 = dir.filePath(QStringLiteral("outA3.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, a3, pathA3).ok);
    QFile fA3(pathA3);
    CHECK(fA3.open(QIODevice::ReadOnly));
    const QString pdfA3 = QString::fromLatin1(fA3.readAll());
    CHECK(a3Box.match(pdfA3).hasMatch() && !a4Box.match(pdfA3).hasMatch());
    Pdf3D::Pdf3DOptions unknownTemplate = options;
    unknownTemplate.templateId = QStringLiteral("user:does-not-exist");
    const QString pathUnknown = dir.filePath(QStringLiteral("outUnknown.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, unknownTemplate, pathUnknown).ok);
    QFile fUnknown(pathUnknown);
    CHECK(fUnknown.open(QIODevice::ReadOnly));
    CHECK(a4Box.match(QString::fromLatin1(fUnknown.readAll())).hasMatch());
    // the sample template: a logo, custom fields, a different grid; the exporter finds it by id in the user folder (test mode, so the
    // folder is not the real one)
    QString sampleError;
    CHECK(Pdf3D::writeSampleSheetTemplate(Pdf3D::userSheetTemplateFolder(), &sampleError) == QLatin1String("user:sample-company"));
    Pdf3D::Pdf3DOptions withSample = options;
    withSample.templateId = QStringLiteral("user:sample-company");
    withSample.fieldValues.insert(QStringLiteral("approvedBy"), QStringLiteral("A. Approver"));
    withSample.fieldValues.insert(QStringLiteral("material"), QStringLiteral("Steel"));
    const QString pathSample = dir.filePath(QStringLiteral("outSample.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, withSample, pathSample).ok);
    QFile fSample(pathSample);
    CHECK(fSample.open(QIODevice::ReadOnly));
    CHECK(a4Box.match(QString::fromLatin1(fSample.readAll())).hasMatch());
    Pdf3D::Pdf3DOptions longNotes = options;
    longNotes.notes = QString(2000, QLatin1Char('n'));
    for (int i = 20; i < longNotes.notes.size(); i += 20)
        longNotes.notes[i] = QLatin1Char(' ');   // words, so the text wraps over many lines
    const QString pathLong = dir.filePath(QStringLiteral("outLongNotes.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(u, longNotes, pathLong).ok);
    QFile fLong(pathLong);
    CHECK(fLong.open(QIODevice::ReadOnly));
    CHECK(fLong.readAll().contains("/FT /Tx"));

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
    Pdf3D::CameraViewSpec hiddenPart = hidden;
    hiddenPart.name = QStringLiteral("Hidden part");
    hiddenPart.showOverlays = true;
    hiddenPart.nodes.append({u.nodes[0].name, false});
    followOptions.extraViews.append(hiddenPart);
    const QString pathFollow = dir.filePath(QStringLiteral("follow.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(withOverlay, followOptions, pathFollow).ok);
    QFile fFollow(pathFollow);
    CHECK(fFollow.open(QIODevice::ReadOnly));
    const QByteArray pdfFollow = fFollow.readAll();
    // Ten views (seven standard, moved, overlays off and part hidden) each list the part and its overlay.
    CHECK(pdfFollow.count("/Type /3DNode") == 20);
    CHECK(pdfFollow.count("/N (O0000_m1) /V true /M [1 0 0 0 1 0 0 0 1 5 0 0]") == 1);   // follows the part in the moved view only
    CHECK(pdfFollow.count("/N (O0000_m1) /V false") == 2);   // hidden with overlays off or with its part
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
    animOptions.viewButtons = true;
    animOptions.animationPlayback = Pdf3D::AnimationPlayback::BackAndForth;
    const QString pathAnim = dir.filePath(QStringLiteral("anim.pdf"));
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(animatedScene, animOptions, pathAnim).ok);
    QFile fAnim(pathAnim);
    CHECK(fAnim.open(QIODevice::ReadOnly));
    const QByteArray pdfAnim = fAnim.readAll();
    CHECK(pdfAnim.contains("/AN << /Type /3DAnimationStyle /Subtype /Oscillating /PC -1 /TM 1 >>") && pdfAnim.contains("/AIS /L /DIS /I"));
    CHECK(pdfAnim.contains("/S /JavaScript") && pdfAnim.contains("runtime.play()") && pdfAnim.contains("runtime.pause()"));   // Play, Pause
    animOptions.animationPlayback = Pdf3D::AnimationPlayback::Once;
    animOptions.animationAutoStart = false;
    const QString pathAnim2 = dir.filePath(QStringLiteral("anim_once.pdf"));   // not the first file: it is still open (Windows cannot replace it)
    CHECK(Pdf3D::Pdf3DExporter::exportToFile(animatedScene, animOptions, pathAnim2).ok);
    QFile fAnim2(pathAnim2);
    CHECK(fAnim2.open(QIODevice::ReadOnly));
    const QByteArray pdfAnim2 = fAnim2.readAll();
    CHECK(pdfAnim2.contains("/Subtype /Linear /PC 1 /TM 1 >>") && !pdfAnim2.contains("/AIS /L"));
    CHECK(pdfAnim2.contains("/3DA << /A /XA") && pdfAnim.contains("/3DA << /A /PO"));   // autostart off: the area is activated by a click
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
    CHECK(pdfTop.contains("/3DV 0 "));
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

// ---- Sheet templates (SheetTemplate.h) ----------------------------------------------------------------------------------------------

bool almostEqual(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool sameRect(const QRectF& a, const QRectF& b) { return almostEqual(a.left(), b.left()) && almostEqual(a.top(), b.top()) && almostEqual(a.width(), b.width()) && almostEqual(a.height(), b.height()); }

// A valid minimal template (millimetres, A4 landscape, one cell), changed by `mutate` for the rejection cases.
QByteArray sheetJson(const std::function<void(QJsonObject&)>& mutate = nullptr)
{
    QJsonObject cell;
    cell.insert(QStringLiteral("id"), QStringLiteral("a"));
    cell.insert(QStringLiteral("left"), 2);
    cell.insert(QStringLiteral("right"), -2);
    cell.insert(QStringLiteral("top"), 2);
    cell.insert(QStringLiteral("bottom"), -2);
    cell.insert(QStringLiteral("text"), QStringLiteral("{title}"));
    QJsonObject block;
    block.insert(QStringLiteral("anchor"), QStringLiteral("bottom"));
    block.insert(QStringLiteral("height"), 30);
    block.insert(QStringLiteral("cells"), QJsonArray{ cell });
    QJsonObject page;
    page.insert(QStringLiteral("size"), QStringLiteral("A4"));
    page.insert(QStringLiteral("orientation"), QStringLiteral("landscape"));
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), QStringLiteral("Test"));
    root.insert(QStringLiteral("units"), QStringLiteral("mm"));
    root.insert(QStringLiteral("page"), page);
    root.insert(QStringLiteral("titleBlock"), block);
    if (mutate)
        mutate(root);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Mutators reach into the title block: root["titleBlock"] is a copy, so write it back.
void editBlock(QJsonObject& root, const std::function<void(QJsonObject&)>& edit)
{
    QJsonObject block = root.value(QStringLiteral("titleBlock")).toObject();
    edit(block);
    root.insert(QStringLiteral("titleBlock"), block);
}

void editFirstCell(QJsonObject& root, const std::function<void(QJsonObject&)>& edit)
{
    editBlock(root, [&](QJsonObject& block) {
        QJsonArray cells = block.value(QStringLiteral("cells")).toArray();
        QJsonObject cell = cells.at(0).toObject();
        edit(cell);
        cells.replace(0, cell);
        block.insert(QStringLiteral("cells"), cells);
    });
}

bool rejects(const QByteArray& json, const QString& expectedInMessage)
{
    Pdf3D::SheetTemplate t;
    QString error;
    const bool ok = Pdf3D::parseSheetTemplate(json, QStringLiteral("test"), &t, &error);
    if (ok || !error.contains(expectedInMessage))
        std::fprintf(stderr, "  expected rejection containing \"%s\", got ok=%d error=\"%s\"\n", qPrintable(expectedInMessage), ok ? 1 : 0, qPrintable(error));
    return !ok && error.contains(expectedInMessage);
}

// Two templates describe the same sheet: page, every rectangle and line for one block height, the cell texts and styles, the fields.
bool sameSheet(const Pdf3D::SheetTemplate& a, const Pdf3D::SheetTemplate& b, double blockHeight)
{
    using namespace Pdf3D;
    const QSizeF pa = sheetPageSizePt(a), pb = sheetPageSizePt(b);
    if (!almostEqual(pa.width(), pb.width()) || !almostEqual(pa.height(), pb.height()))
        return false;
    const ResolvedSheet ra = resolveSheet(a, pa, blockHeight), rb = resolveSheet(b, pb, blockHeight);
    if (ra.cells.size() != rb.cells.size() || ra.lines.size() != rb.lines.size() || a.cells.size() != b.cells.size() || a.fields.size() != b.fields.size())
        return false;
    if (!sameRect(ra.border, rb.border) || !sameRect(ra.titleBlock, rb.titleBlock) || !sameRect(ra.area3d, rb.area3d) || !sameRect(ra.logo, rb.logo))
        return false;
    for (int i = 0; i < ra.cells.size(); ++i)
    {
        const SheetCell &ca = a.cells[i], &cb = b.cells[i];
        if (!sameRect(ra.cells[i], rb.cells[i]) || ca.id != cb.id || ca.text != cb.text || ca.label != cb.label || !almostEqual(ca.fontSize, cb.fontSize) ||
            ca.bold != cb.bold || ca.color != cb.color || (ca.align & Qt::AlignHorizontal_Mask) != (cb.align & Qt::AlignHorizontal_Mask) || ca.wrap != cb.wrap ||
            ca.scrollWhenOverflow != cb.scrollWhenOverflow)
            return false;
    }
    for (int i = 0; i < ra.lines.size(); ++i)
        if (!almostEqual(ra.lines[i].x1(), rb.lines[i].x1()) || !almostEqual(ra.lines[i].y1(), rb.lines[i].y1()) || !almostEqual(ra.lines[i].x2(), rb.lines[i].x2()) ||
            !almostEqual(ra.lines[i].y2(), rb.lines[i].y2()))
            return false;
    for (int i = 0; i < a.fields.size(); ++i)
        if (a.fields[i].id != b.fields[i].id || a.fields[i].label != b.fields[i].label || a.fields[i].type != b.fields[i].type ||
            a.fields[i].defaultValue != b.fields[i].defaultValue || a.fields[i].options != b.fields[i].options)
            return false;
    return a.name == b.name && a.growWithCell == b.growWithCell && almostEqual(a.blockHeight, b.blockHeight) && almostEqual(a.blockMaxHeight, b.blockMaxHeight) &&
           a.landscape == b.landscape && a.hasLogo == b.hasLogo && almostEqual(a.lineWidth, b.lineWidth);
}

void testSheetTemplates()
{
    using namespace Pdf3D;

    // built-in templates parse
    const QVector<SheetTemplate> builtIn = builtInSheetTemplates();
    CHECK(builtIn.size() == 2);
    CHECK(builtIn.size() == 2 && builtIn[0].id == QLatin1String("builtin:default") && builtIn[1].id == QLatin1String("builtin:a3-landscape"));
    const SheetTemplate& def = builtIn.value(0);

    // Default reproduces today's hard-coded sheet (the numbers of Pdf3DExporter.cpp: margin 28, border 20, 12 gap, 360 wide grid,
    // notes 52 below the top, 3 x 2 cells), for the minimum and the maximum title block height
    const QSizeF page = sheetPageSizePt(def);
    CHECK(almostEqual(page.width(), 841.8898, 0.01) && almostEqual(page.height(), 595.2756, 0.01));
    for (const double bh : { 90.0, 100.0, 120.0 })
    {
        const ResolvedSheet r = resolveSheet(def, page, bh);
        const QRectF tb(28.0, page.height() - 28.0 - bh, page.width() - 56.0, bh);
        const double splitX = tb.right() - 360.0, rowH = bh / 3.0;
        CHECK(sameRect(r.page, QRectF(QPointF(0, 0), page)));
        CHECK(sameRect(r.border, QRectF(20, 20, page.width() - 40, page.height() - 40)));
        CHECK(sameRect(r.titleBlock, tb));
        CHECK(sameRect(r.area3d, QRectF(QPointF(28, 28), QPointF(page.width() - 28, tb.top() - 12))));
        CHECK(r.cells.size() == 9);
        if (r.cells.size() == 9)
        {
            CHECK(sameRect(r.cells[0], QRectF(tb.left() + 10, tb.top() + 6, splitX - tb.left() - 20, 28)));           // title
            CHECK(sameRect(r.cells[1], QRectF(tb.left() + 10, tb.top() + 36, splitX - tb.left() - 20, 14)));          // subtitle
            CHECK(sameRect(r.cells[2], QRectF(tb.left() + 10, tb.top() + 52, tb.width() - 360.0 - 20.0, bh - 52 - 4))); // notes
            const int order[6][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 }, { 1, 2 } };   // column, row of cells 3..8
            for (int k = 0; k < 6; ++k)
                CHECK(sameRect(r.cells[3 + k], QRectF(splitX + order[k][0] * 180.0, tb.top() + order[k][1] * rowH, 180.0, rowH)));
        }
        CHECK(r.lines.size() == 4);
        if (r.lines.size() == 4)
        {
            const auto sameLine = [](const QLineF& a, const QLineF& b) { return almostEqual(a.x1(), b.x1()) && almostEqual(a.y1(), b.y1()) && almostEqual(a.x2(), b.x2()) && almostEqual(a.y2(), b.y2()); };
            CHECK(sameLine(r.lines[0], QLineF(splitX, tb.top(), splitX, tb.bottom())));
            CHECK(sameLine(r.lines[1], QLineF(splitX + 180.0, tb.top(), splitX + 180.0, tb.bottom())));
            CHECK(sameLine(r.lines[2], QLineF(splitX, tb.top() + rowH, tb.right(), tb.top() + rowH)));
            CHECK(sameLine(r.lines[3], QLineF(splitX, tb.top() + 2 * rowH, tb.right(), tb.top() + 2 * rowH)));
        }
        CHECK(r.logo.isEmpty());
    }

    // the title block grows with the notes like the exporter did: 52 + text + 6, within 90..120
    bool overflows = true;
    CHECK(almostEqual(titleBlockHeightFor(def, page, 0.0, &overflows), 90.0) && !overflows);
    CHECK(almostEqual(titleBlockHeightFor(def, page, 20.0, &overflows), 90.0) && !overflows);
    CHECK(almostEqual(titleBlockHeightFor(def, page, 50.0, &overflows), 108.0) && !overflows);
    CHECK(almostEqual(titleBlockHeightFor(def, page, 100.0, &overflows), 120.0) && overflows);

    // exactly one scrollable cell (the notes) and the built-in label is the one the exporter always used
    int scroll = 0;
    for (const SheetCell& c : def.cells)
        scroll += c.scrollWhenOverflow ? 1 : 0;
    CHECK(scroll == 1 && def.cells.value(2).id == QLatin1String("notes") && def.growWithCell == QLatin1String("notes"));
    CHECK(sheetLabelText(def, QStringLiteral("Drawing no.")) == QLatin1String("Drawing no."));

    // A3 landscape
    const SheetTemplate a3 = builtIn.value(1);
    const QSizeF a3Page = sheetPageSizePt(a3);
    CHECK(almostEqual(a3Page.width(), 1190.55, 0.01) && almostEqual(a3Page.height(), 841.89, 0.01));

    // the placeholders used decide which standard fields the dialog asks for
    const QSet<QString> used = usedSheetPlaceholders(def);
    for (const QString& name : { QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("notes"), QStringLiteral("drawingNumber"), QStringLiteral("revision"),
                                  QStringLiteral("author"), QStringLiteral("company"), QStringLiteral("scale"), QStringLiteral("date"), QStringLiteral("parts"),
                                  QStringLiteral("sheet"), QStringLiteral("sheets") })
        CHECK(used.contains(name));
    CHECK(standardSheetPlaceholders().size() == 12);

    // placeholder substitution, fallback and unknown names
    QMap<QString, QString> values;
    values.insert(QStringLiteral("scale"), QString());
    values.insert(QStringLiteral("parts"), QStringLiteral("12"));
    values.insert(QStringLiteral("sheet"), QStringLiteral("1"));
    values.insert(QStringLiteral("sheets"), QStringLiteral("3"));
    values.insert(QStringLiteral("approvedBy"), QStringLiteral("A. B."));
    QStringList unknown;
    CHECK(substituteSheetText(QStringLiteral("{scale|-}  |  {parts}  |  {sheet}/{sheets}"), values, &unknown) == QLatin1String("-  |  12  |  1/3") && unknown.isEmpty());
    CHECK(substituteSheetText(QStringLiteral("by {approvedBy}{missing} {missing}!"), values, &unknown) == QLatin1String("by A. B. !") && unknown == QStringList{ QStringLiteral("missing") });
    CHECK(substituteSheetText(QStringLiteral("no placeholders { } {1x}"), values) == QLatin1String("no placeholders { } {1x}"));

    // a valid minimal template, a custom field and a corner block
    SheetTemplate t;
    QString error;
    QStringList warnings;
    CHECK(parseSheetTemplate(sheetJson(), QStringLiteral("test"), &t, &error, &warnings) && t.cells.size() == 1 && warnings.isEmpty());
    CHECK(parseSheetTemplate(sheetJson([](QJsonObject& o) {
        QJsonObject field;
        field.insert(QStringLiteral("id"), QStringLiteral("approvedBy"));
        field.insert(QStringLiteral("label"), QStringLiteral("Approved by"));
        field.insert(QStringLiteral("type"), QStringLiteral("choice"));
        field.insert(QStringLiteral("options"), QJsonArray{ QStringLiteral("A"), QStringLiteral("B") });
        o.insert(QStringLiteral("fields"), QJsonArray{ field });
        editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("text"), QStringLiteral("{approvedBy} {nonsense}")); });
    }), QStringLiteral("test"), &t, &error, &warnings) && t.fields.size() == 1 && t.fields[0].options.size() == 2 && warnings.size() == 1 && warnings[0].contains(QLatin1String("nonsense")));
    CHECK(parseSheetTemplate(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("anchor"), QStringLiteral("bottom-right")); b.insert(QStringLiteral("width"), 100); });
    }), QStringLiteral("test"), &t, &error));
    if (t.anchor == TitleBlockAnchor::BottomRight)
    {
        const QSizeF p = sheetPageSizePt(t);
        const ResolvedSheet r = resolveSheet(t, p, t.blockHeight);
        CHECK(almostEqual(r.titleBlock.width(), 100.0 * 72.0 / 25.4) && almostEqual(r.titleBlock.right(), p.width() - t.margin));
        // the cell is 2 mm in from each edge (millimetres converted to points; the right and bottom edges count from the opposite side)
        CHECK(sameRect(r.cells[0], r.titleBlock.adjusted(2.0 * 72.0 / 25.4, 2.0 * 72.0 / 25.4, -2.0 * 72.0 / 25.4, -2.0 * 72.0 / 25.4)));
    }
    else
        CHECK(false);

    // coordinate rules: positive from the top-left, negative from the opposite edge, fractions and percentages of the block size
    CHECK(almostEqual(resolveSheetEdge(SheetEdge{ 10.0, false }, 200.0), 10.0));
    CHECK(almostEqual(resolveSheetEdge(SheetEdge{ -10.0, false }, 200.0), 190.0));
    CHECK(almostEqual(resolveSheetEdge(SheetEdge{ 0.25, true }, 200.0), 50.0));
    CHECK(almostEqual(resolveSheetEdge(SheetEdge{ -0.25, true }, 200.0), 150.0));
    CHECK(almostEqual(resolveSheetEdge(SheetEdge{ 1.0, true }, 200.0), 200.0));

    // rejected templates, each with a message that names the problem
    CHECK(rejects(QByteArray("not json"), QStringLiteral("JSON")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { o.insert(QStringLiteral("version"), 2); }), QStringLiteral("version")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { o.insert(QStringLiteral("name"), QStringLiteral("")); }), QStringLiteral("name")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { o.insert(QStringLiteral("units"), QStringLiteral("inch")); }), QStringLiteral("units")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { QJsonObject p = o.value(QStringLiteral("page")).toObject(); p.insert(QStringLiteral("size"), QStringLiteral("B7")); o.insert(QStringLiteral("page"), p); }), QStringLiteral("page size")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { QJsonObject p = o.value(QStringLiteral("page")).toObject(); p.insert(QStringLiteral("size"), QJsonArray{ 50, 50 }); o.insert(QStringLiteral("page"), p); }), QStringLiteral("custom page size")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { QJsonObject p = o.value(QStringLiteral("page")).toObject(); p.insert(QStringLiteral("orientation"), QStringLiteral("diagonal")); o.insert(QStringLiteral("page"), p); }), QStringLiteral("orientation")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("anchor"), QStringLiteral("top")); }); }), QStringLiteral("anchor")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("anchor"), QStringLiteral("bottom-left")); }); }), QStringLiteral("width")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("maxHeight"), 10); }); }), QStringLiteral("maxHeight")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("cells"), QJsonArray()); }); }), QStringLiteral("cells")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("right"), 500); }); }), QStringLiteral("outside the title block")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("left"), 50); c.insert(QStringLiteral("right"), 40); }); }), QStringLiteral("no area")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.remove(QStringLiteral("top")); }); }), QStringLiteral("top")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("top"), QStringLiteral("2/0")); }); }), QStringLiteral("fraction")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("bottom"), QStringLiteral("150%")); }); }), QStringLiteral("percentage")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("fontSize"), 1); }); }), QStringLiteral("fontSize")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("color"), QStringLiteral("nocolour")); }); }), QStringLiteral("color")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("align"), QStringLiteral("middle")); }); }), QStringLiteral("align")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("text"), QString(501, QLatin1Char('x'))); }); }), QStringLiteral("longer than")));
    CHECK(rejects(sheetJson([](QJsonObject& o) { editBlock(o, [](QJsonObject& b) { b.insert(QStringLiteral("growWithCell"), QStringLiteral("nothing")); }); }), QStringLiteral("growWithCell")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) {
            QJsonArray cells = b.value(QStringLiteral("cells")).toArray();
            cells.append(cells.at(0));   // the same id twice
            b.insert(QStringLiteral("cells"), cells);
        });
    }), QStringLiteral("used twice")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) {
            QJsonArray cells = b.value(QStringLiteral("cells")).toArray();
            for (int i = 0; i < 2; ++i)
            {
                QJsonObject c = cells.at(0).toObject();
                c.insert(QStringLiteral("id"), QStringLiteral("s%1").arg(i));
                c.insert(QStringLiteral("scrollWhenOverflow"), true);
                cells.append(c);
            }
            b.insert(QStringLiteral("cells"), cells);
        });
    }), QStringLiteral("scrollWhenOverflow")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) {
            QJsonArray cells;
            for (int i = 0; i < 65; ++i)
            {
                QJsonObject c = b.value(QStringLiteral("cells")).toArray().at(0).toObject();
                c.insert(QStringLiteral("id"), QStringLiteral("c%1").arg(i));
                cells.append(c);
            }
            b.insert(QStringLiteral("cells"), cells);
        });
    }), QStringLiteral("cells")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        QJsonObject field;
        field.insert(QStringLiteral("id"), QStringLiteral("title"));   // a standard placeholder name
        field.insert(QStringLiteral("label"), QStringLiteral("X"));
        o.insert(QStringLiteral("fields"), QJsonArray{ field });
    }), QStringLiteral("already a placeholder")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        QJsonObject field;
        field.insert(QStringLiteral("id"), QStringLiteral("pick"));
        field.insert(QStringLiteral("label"), QStringLiteral("Pick"));
        field.insert(QStringLiteral("type"), QStringLiteral("choice"));   // a choice without options
        o.insert(QStringLiteral("fields"), QJsonArray{ field });
    }), QStringLiteral("options")));
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) {
            QJsonObject logo;
            logo.insert(QStringLiteral("file"), QStringLiteral("logo.png"));
            logo.insert(QStringLiteral("left"), 2);
            logo.insert(QStringLiteral("right"), 400);   // outside the block
            logo.insert(QStringLiteral("top"), 2);
            logo.insert(QStringLiteral("bottom"), 10);
            b.insert(QStringLiteral("logo"), logo);
        });
    }), QStringLiteral("logo")));
    CHECK(rejects(QByteArray(300 * 1024, ' '), QStringLiteral("larger")));

    // a logo is parsed with its rectangle
    CHECK(parseSheetTemplate(sheetJson([](QJsonObject& o) {
        editBlock(o, [](QJsonObject& b) {
            QJsonObject logo;
            logo.insert(QStringLiteral("file"), QStringLiteral("logo.svg"));
            logo.insert(QStringLiteral("left"), -30);
            logo.insert(QStringLiteral("right"), -2);
            logo.insert(QStringLiteral("top"), 2);
            logo.insert(QStringLiteral("bottom"), -2);
            logo.insert(QStringLiteral("fit"), QStringLiteral("stretch"));
            b.insert(QStringLiteral("logo"), logo);
        });
    }), QStringLiteral("test"), &t, &error) && t.hasLogo && !t.logo.contain && !resolveSheet(t, sheetPageSizePt(t), t.blockHeight).logo.isEmpty());

    // user templates from a folder: a good one, a broken one, one with a missing logo, and files that are ignored
    QTemporaryDir folder;
    CHECK(folder.isValid());
    if (folder.isValid())
    {
        const auto write = [&](const QString& name, const QByteArray& data) { QFile f(QDir(folder.path()).filePath(name)); CHECK(f.open(QIODevice::WriteOnly)); f.write(data); };
        write(QStringLiteral("b_good.json"), sheetJson());
        write(QStringLiteral("a_broken.json"), QByteArray("{ nope"));
        write(QStringLiteral("c_logo.json"), sheetJson([](QJsonObject& o) {
            editBlock(o, [](QJsonObject& b) {
                QJsonObject logo;
                logo.insert(QStringLiteral("file"), QStringLiteral("missing.png"));
                logo.insert(QStringLiteral("left"), -30);
                logo.insert(QStringLiteral("right"), -2);
                logo.insert(QStringLiteral("top"), 2);
                logo.insert(QStringLiteral("bottom"), -2);
                b.insert(QStringLiteral("logo"), logo);
            });
        }));
        write(QStringLiteral("notes.txt"), QByteArray("ignored"));
        QStringList problems;
        const QVector<SheetTemplate> user = loadUserSheetTemplates(folder.path(), &problems);
        CHECK(user.size() == 2);
        CHECK(user.size() == 2 && user[0].id == QLatin1String("user:b_good") && user[1].id == QLatin1String("user:c_logo"));
        CHECK(user.size() == 2 && user[0].folder == QDir(folder.path()).absolutePath() && sheetLabelText(user[0], QStringLiteral("Drawing no.")) == QLatin1String("Drawing no."));
        CHECK(problems.size() == 2);
        CHECK(problems.size() == 2 && problems[0].startsWith(QLatin1String("a_broken.json")) && problems[1].contains(QLatin1String("logo")) && problems[1].contains(QLatin1String("missing.png")));
        CHECK(loadUserSheetTemplates(QDir(folder.path()).filePath(QStringLiteral("nowhere"))).isEmpty());
    }

    // the sample the dialog's "Create sample" button writes: valid, with its logo and fields, and never over the user's own edits
    QTemporaryDir sampleRoot;
    CHECK(sampleRoot.isValid());
    if (sampleRoot.isValid())
    {
        const QString sampleFolder = QDir(sampleRoot.path()).filePath(QStringLiteral("pdf3d-templates"));
        QString writeError;
        CHECK(writeSampleSheetTemplate(sampleFolder, &writeError) == QLatin1String("user:sample-company") && writeError.isEmpty());
        CHECK(QFile::exists(QDir(sampleFolder).filePath(QStringLiteral("sample-logo.svg"))));
        QStringList sampleProblems;
        const QVector<SheetTemplate> sample = loadUserSheetTemplates(sampleFolder, &sampleProblems);
        CHECK(sample.size() == 1 && sampleProblems.isEmpty());
        if (sample.size() == 1)
        {
            const SheetTemplate& s = sample[0];
            CHECK(s.id == QLatin1String("user:sample-company") && s.hasLogo && s.cells.size() == 11 && s.fields.size() == 2);
            CHECK(s.fields.size() == 2 && s.fields[0].id == QLatin1String("approvedBy") && s.fields[1].type == QLatin1String("choice") && s.fields[1].options.size() == 4);
            const QSet<QString> sampleUsed = usedSheetPlaceholders(s);
            CHECK(sampleUsed.contains(QStringLiteral("approvedBy")) && sampleUsed.contains(QStringLiteral("material")) && !sampleUsed.contains(QStringLiteral("company")));
            const ResolvedSheet r = resolveSheet(s, sheetPageSizePt(s), s.blockHeight);
            CHECK(!r.logo.isEmpty() && r.titleBlock.contains(r.logo) && r.logo.right() < r.cells[0].left());   // the logo sits left of the title
            CHECK(r.titleBlock.contains(r.cells[3]) && r.cells[3].left() > r.cells[0].right());                // the grid sits right of the title
        }
        // a second call keeps an edited file
        {
            QFile edited(QDir(sampleFolder).filePath(QStringLiteral("sample-company.json")));
            CHECK(edited.open(QIODevice::WriteOnly | QIODevice::Truncate));
            edited.write("edited");
        }
        CHECK(writeSampleSheetTemplate(sampleFolder, &writeError) == QLatin1String("user:sample-company"));
        QFile again(QDir(sampleFolder).filePath(QStringLiteral("sample-company.json")));
        CHECK(again.open(QIODevice::ReadOnly) && again.readAll() == "edited");
        CHECK(writeSampleSheetTemplate(QString(), &writeError).isEmpty() && !writeError.isEmpty());
    }

    // saving: a template written as JSON and read back describes the same sheet (the built-ins, the sample, a template with a logo)
    for (const SheetTemplate& original : builtIn)
    {
        SheetTemplate back;
        QString backError;
        CHECK(parseSheetTemplate(sheetTemplateToJson(original), original.id, &back, &backError));
        CHECK(sameSheet(original, back, original.blockHeight) && sameSheet(original, back, original.blockMaxHeight));
    }

    QTemporaryDir saveRoot;
    CHECK(saveRoot.isValid());
    if (saveRoot.isValid())
    {
        const QString folder = QDir(saveRoot.path()).filePath(QStringLiteral("pdf3d-templates"));
        QString saveError;
        QStringList loadProblems;

        // Save as: a copy under a new name in the user folder, built-in names refused, an existing name selects, bad names rejected
        const QString id = saveSheetTemplateAs(def, QStringLiteral("My Company"), folder, &saveError);
        CHECK(id == QLatin1String("user:My Company") && saveError.isEmpty());
        CHECK(QFile::exists(QDir(folder).filePath(QStringLiteral("My Company.json"))));
        QVector<SheetTemplate> saved = loadUserSheetTemplates(folder, &loadProblems);
        CHECK(saved.size() == 1 && loadProblems.isEmpty());
        if (saved.size() == 1)
        {
            CHECK(saved[0].name == QLatin1String("My Company") && saved[0].id == id);
            SheetTemplate expected = def;
            expected.name = QStringLiteral("My Company");
            CHECK(sameSheet(expected, saved[0], def.blockHeight) && sameSheet(expected, saved[0], def.blockMaxHeight));
        }
        CHECK(saveSheetTemplateAs(def, QStringLiteral("my company"), folder, &saveError) == id);   // the same name (any case) selects it
        CHECK(QDir(folder).entryList(QStringList() << QStringLiteral("*.json")).size() == 1);
        CHECK(saveSheetTemplateAs(def, QStringLiteral("default"), folder, &saveError).isEmpty() && saveError.contains(QLatin1String("built-in")));
        CHECK(saveSheetTemplateAs(def, QStringLiteral("  "), folder, &saveError).isEmpty() && !saveError.isEmpty());
        CHECK(saveSheetTemplateAs(def, QString(81, QLatin1Char('x')), folder, &saveError).isEmpty());
        CHECK(sanitizedTemplateFileName(QStringLiteral("A/B:C*?")) == QLatin1String("A_B_C__") && sanitizedTemplateFileName(QStringLiteral("///")) == QLatin1String("___"));
        const QString second = saveSheetTemplateAs(def, QStringLiteral("My/Company"), folder, &saveError);   // another name, same sanitised base as nothing yet
        CHECK(second == QLatin1String("user:My_Company"));

        // Logo: a template without one gets a cell at the left of the title block; the text moves right, the grid stays; saved and read back
        const QString logoFolder = QDir(saveRoot.path()).filePath(QStringLiteral("logos"));
        const QString sampleFolder2 = QDir(saveRoot.path()).filePath(QStringLiteral("sample"));
        CHECK(writeSampleSheetTemplate(sampleFolder2, &saveError) == QLatin1String("user:sample-company"));
        const QString logoFile = QDir(sampleFolder2).filePath(QStringLiteral("sample-logo.svg"));
        SheetTemplate withLogo = saved.value(0);
        const ResolvedSheet before = resolveSheet(withLogo, sheetPageSizePt(withLogo), withLogo.blockHeight);
        CHECK(!withLogo.hasLogo && withLogo.lines.size() == 4);
        CHECK(setSheetTemplateLogo(&withLogo, logoFile, folder, &saveError) && withLogo.hasLogo && withLogo.logo.file == QLatin1String("sample-logo.svg"));
        CHECK(QFile::exists(QDir(folder).filePath(QStringLiteral("sample-logo.svg"))) && withLogo.lines.size() == 5);
        const ResolvedSheet after = resolveSheet(withLogo, sheetPageSizePt(withLogo), withLogo.blockHeight);
        CHECK(!after.logo.isEmpty() && after.titleBlock.contains(after.logo));
        CHECK(almostEqual(after.cells[0].left() - before.cells[0].left(), 116.0));   // 4 + max(60, 1.2 x 90) + 4 points: the title moved right
        CHECK(sameRect(after.cells[3], before.cells[3]));                              // the field grid did not move
        CHECK(after.logo.right() < after.cells[0].left());
        // a narrow cell at the left whose right edge stays put (a fraction here) is moved clear of the logo as a whole, not squeezed to nothing
        SheetTemplate narrowCase = saved.value(0);
        SheetCell narrowCell;
        narrowCell.text = QStringLiteral("{date}");
        narrowCell.rect.left = SheetEdge{ 10.0, false };
        narrowCell.rect.right = SheetEdge{ 0.04, true };
        narrowCell.rect.top = SheetEdge{ 4.0, false };
        narrowCell.rect.bottom = SheetEdge{ 18.0, false };
        narrowCase.cells.append(narrowCell);
        CHECK(validateSheetTemplate(narrowCase, &saveError));
        const ResolvedSheet narrowBefore = resolveSheet(narrowCase, sheetPageSizePt(narrowCase), narrowCase.blockHeight);
        CHECK(setSheetTemplateLogo(&narrowCase, logoFile, folder, &saveError) && validateSheetTemplate(narrowCase, &saveError));
        const ResolvedSheet narrowAfter = resolveSheet(narrowCase, sheetPageSizePt(narrowCase), narrowCase.blockHeight);
        CHECK(narrowAfter.cells.last().left() >= narrowAfter.logo.right() && narrowAfter.cells.last().width() >= narrowBefore.cells.last().width() - 0.5);
        CHECK(writeUserSheetTemplate(withLogo, folder, &saveError) == withLogo.id);
        const QVector<SheetTemplate> reloaded = loadUserSheetTemplates(folder, &loadProblems);
        SheetTemplate found;
        for (const SheetTemplate& r : reloaded)
            if (r.id == withLogo.id)
                found = r;
        CHECK(found.hasLogo && QFileInfo::exists(sheetLogoPath(found)) && sameSheet(withLogo, found, withLogo.blockHeight));
        // a second logo only replaces the file: the cell stays where it is and nothing moves again
        CHECK(setSheetTemplateLogo(&withLogo, logoFile, folder, &saveError));
        CHECK(sameRect(resolveSheet(withLogo, sheetPageSizePt(withLogo), withLogo.blockHeight).cells[0], after.cells[0]) && withLogo.lines.size() == 5);
        CHECK(!setSheetTemplateLogo(&withLogo, QDir(folder).filePath(QStringLiteral("My Company.json")), folder, &saveError) && saveError.contains(QLatin1String("PNG")));
        // a copy of a template that has a logo takes the logo file along
        const QString copyId = saveSheetTemplateAs(found, QStringLiteral("Copy with logo"), logoFolder, &saveError);
        CHECK(copyId == QLatin1String("user:Copy with logo") && QFile::exists(QDir(logoFolder).filePath(QStringLiteral("sample-logo.svg"))));
        // what would not load again is never written
        SheetTemplate broken = withLogo;
        broken.id = QStringLiteral("user:broken");
        broken.cells[0].rect.left = SheetEdge{ 5000.0, false };
        CHECK(writeUserSheetTemplate(broken, folder, &saveError).isEmpty() && !saveError.isEmpty() && !QFile::exists(QDir(folder).filePath(QStringLiteral("broken.json"))));
        SheetTemplate builtInId = def;
        CHECK(writeUserSheetTemplate(builtInId, folder, &saveError).isEmpty());
    }
}

// The shared sheet layout and drawing (SheetRenderer.h): the title block grows with the notes like the exporter's did, and the preview
// picture is the page with the 3D area greyed out, the frame and the text.
void testSheetRenderer()
{
    using namespace Pdf3D;
    const SheetTemplate def = builtInSheetTemplates().value(0);
    const QSizeF page = sheetPageSizePt(def);

    // a 72 dpi painter measures text like the PDF writer does (font sizes in points are pixels)
    QImage measureImage(16, 16, QImage::Format_ARGB32);
    measureImage.setDotsPerMeterX(2835);
    measureImage.setDotsPerMeterY(2835);
    QPainter measure(&measureImage);

    QMap<QString, QString> values;
    values.insert(QStringLiteral("title"), QStringLiteral("Skate-Board-Assy"));
    values.insert(QStringLiteral("subtitle"), QStringLiteral("model.stp"));
    values.insert(QStringLiteral("notes"), QString());
    values.insert(QStringLiteral("drawingNumber"), QStringLiteral("DWG-1"));
    values.insert(QStringLiteral("revision"), QStringLiteral("A"));
    values.insert(QStringLiteral("author"), QStringLiteral("N. N."));
    values.insert(QStringLiteral("company"), QStringLiteral("ACME"));
    values.insert(QStringLiteral("scale"), QString());
    values.insert(QStringLiteral("date"), QStringLiteral("2026-11-07"));
    values.insert(QStringLiteral("parts"), QStringLiteral("19"));
    values.insert(QStringLiteral("sheet"), QStringLiteral("1"));
    values.insert(QStringLiteral("sheets"), QStringLiteral("2"));

    const SheetLayout none = layoutSheet(measure, def, page, values);
    CHECK(almostEqual(none.sheet.titleBlock.height(), 90.0) && !none.scroll);
    CHECK(sameRect(none.sheet.titleBlock, resolveSheet(def, page, 90.0).titleBlock) && sameRect(none.sheet.area3d, resolveSheet(def, page, 90.0).area3d));

    values.insert(QStringLiteral("notes"), QStringLiteral("one line"));
    CHECK(almostEqual(layoutSheet(measure, def, page, values).sheet.titleBlock.height(), 90.0));   // 52 + one line + 6 is still within the minimum block

    values.insert(QStringLiteral("notes"), QStringLiteral("first\nsecond\nthird\nfourth"));
    const SheetLayout few = layoutSheet(measure, def, page, values);
    CHECK(few.sheet.titleBlock.height() > 90.0 && few.sheet.titleBlock.height() < 120.0 && !few.scroll);   // grows with the text

    QString longNotes(2000, QLatin1Char('n'));
    for (int i = 20; i < longNotes.size(); i += 20)
        longNotes[i] = QLatin1Char(' ');
    values.insert(QStringLiteral("notes"), longNotes);
    const SheetLayout many = layoutSheet(measure, def, page, values);
    CHECK(almostEqual(many.sheet.titleBlock.height(), 120.0) && many.scroll && many.scrollText == longNotes && many.scrollRect.height() > 40.0);
    CHECK(many.sheet.area3d.height() < none.sheet.area3d.height());   // the taller block leaves less room for the 3D area
    measure.end();

    // the preview picture: size, background, the greyed 3D area, dark text in the title block, the selection frame, the logo
    values.insert(QStringLiteral("notes"), QString());
    SheetLayout shown;
    const QImage image = renderSheetPreview(def, values, 420, -1, &shown);
    CHECK(image.width() == 420 && std::abs(image.height() - 297) <= 1);
    CHECK(QColor(image.pixelColor(2, 2)) == QColor(Qt::white));
    const double zoom = 420.0 / page.width();
    const QColor greyArea = image.pixelColor(static_cast<int>((shown.sheet.area3d.left() + 20.0) * zoom), static_cast<int>((shown.sheet.area3d.top() + 20.0) * zoom));
    CHECK(greyArea == QColor(238, 241, 245));
    int dark = 0;
    const QRectF block = shown.sheet.titleBlock;
    for (int y = static_cast<int>(block.top() * zoom); y < static_cast<int>(block.bottom() * zoom); ++y)
        for (int x = static_cast<int>(block.left() * zoom); x < static_cast<int>(block.right() * zoom); ++x)
            dark += image.pixelColor(x, y).value() < 100 ? 1 : 0;
    CHECK(dark > 10);
    CHECK(renderSheetPreview(def, values, 420) == image);                                  // deterministic
    CHECK(renderSheetPreview(def, values, 420, 0) != image);                               // a selected cell is framed
    CHECK(renderSheetPreview(def, values, 420, 99) == image && renderSheetPreview(def, values, 420, -5) == image);   // out of range: none
    CHECK(renderSheetPreview(def, values, 840).width() == 840);

    // a template with a logo draws something in the logo cell that the same template without it does not
    QTemporaryDir root;
    CHECK(root.isValid());
    if (root.isValid())
    {
        const QString folder = QDir(root.path()).filePath(QStringLiteral("t"));
        CHECK(writeSampleSheetTemplate(folder) == QLatin1String("user:sample-company"));
        const QVector<SheetTemplate> user = loadUserSheetTemplates(folder);
        CHECK(user.size() == 1);
        if (user.size() == 1)
        {
            SheetTemplate withLogo = user[0];
            SheetTemplate withoutLogo = withLogo;
            withoutLogo.hasLogo = false;
            values.insert(QStringLiteral("approvedBy"), QStringLiteral("A. B."));
            values.insert(QStringLiteral("material"), QStringLiteral("Steel"));
            const QImage a = renderSheetPreview(withLogo, values, 900), b = renderSheetPreview(withoutLogo, values, 900);
            CHECK(a != b && a.size() == b.size());
        }
    }
}

void testPrintPageReflow()
{
    QImage view(32, 32, QImage::Format_RGB32);
    const QRgb modelColor = qRgb(220, 30, 95);
    view.fill(modelColor);
    Pdf3D::Pdf3DOptions options;
    options.templateId = QStringLiteral("builtin:a3-landscape");
    options.drawingNumber = QStringLiteral("DWG-0123");

    const auto render = [&](const QSize& paper, int dpi, const Pdf3D::Pdf3DOptions& pageOptions) {
        QImage page(paper * (dpi / 72), QImage::Format_RGB32);
        page.setDotsPerMeterX(qRound(dpi / 0.0254));
        page.setDotsPerMeterY(qRound(dpi / 0.0254));
        QPainter painter(&page);
        const bool drawn = PrintPage::paint(painter, QSizeF(paper), dpi, true, pageOptions, 1, view);
        painter.end();
        return drawn ? page : QImage();
    };

    // Reuse the landscape template on portrait, narrower and unusually short sheets.
    for (const QSize paper : {QSize(842, 595), QSize(595, 842), QSize(420, 595), QSize(420, 160)})
    {
        const QImage page = render(paper, 72, options);
        CHECK(!page.isNull() && page.size() == paper);
        if (page.isNull())
            continue;

        int left = paper.width(), right = -1, top = paper.height(), bottom = -1;
        for (int y = 0; y < paper.height(); ++y)
        {
            const auto* pixels = reinterpret_cast<const QRgb*>(page.constScanLine(y));
            for (int x = 0; x < paper.width(); ++x)
            {
                if (pixels[x] != modelColor)
                    continue;
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
        CHECK(right > left && bottom > top && qAbs((right - left) - (bottom - top)) <= 2);

        Pdf3D::Pdf3DOptions blank = options;
        blank.drawingNumber.clear();
        CHECK(page != render(paper, 72, blank));
    }

    // Device DPI should change the number of pixels a label occupies, while its physical size stays the same.
    Pdf3D::Pdf3DOptions blank = options;
    blank.drawingNumber.clear();
    const auto labelBounds = [&](int dpi) {
        const QSize paper(595, 842);
        const QImage withLabel = render(paper, dpi, options);
        const QImage withoutLabel = render(paper, dpi, blank);
        QRect bounds;
        for (int y = 0; y < withLabel.height(); ++y)
        {
            const auto* with = reinterpret_cast<const QRgb*>(withLabel.constScanLine(y));
            const auto* without = reinterpret_cast<const QRgb*>(withoutLabel.constScanLine(y));
            for (int x = 0; x < withLabel.width(); ++x)
                if (with[x] != without[x])
                    bounds |= QRect(x, y, 1, 1);
        }
        return bounds;
    };
    const QRect at72 = labelBounds(72);
    const QRect at144 = labelBounds(144);
    CHECK(at72.isValid() && at144.isValid());
    CHECK(qAbs(at72.width() * 2 - at144.width()) <= 8);
    CHECK(qAbs(at72.height() * 2 - at144.height()) <= 4);
}

void testPrintTemplateCompanion()
{
    using namespace Pdf3D;
    SheetTemplate landscape = builtInSheetTemplates().first();
    landscape.id = QStringLiteral("user:landscape");
    landscape.name = QStringLiteral("Landscape primary");

    SheetTemplate portrait = landscape;
    portrait.id = QStringLiteral("user:portrait");
    portrait.name = QStringLiteral("Portrait companion");
    portrait.landscape = false;
    portrait = scaleSheetTemplateWidth(portrait, sheetBlockWidthPt(portrait) / sheetBlockWidthPt(landscape));
    portrait.printCompanionId = landscape.id;
    landscape.printCompanionId = portrait.id;

    SheetTemplate restored;
    QString error;
    CHECK(parseSheetTemplate(sheetTemplateToJson(landscape), landscape.id, &restored, &error));
    CHECK(restored.printCompanionId == portrait.id);
    CHECK(validateSheetTemplate(portrait, &error));

    const QVector<SheetTemplate> available{restored, portrait};
    CHECK(printTemplateForOrientation(restored, true, available).id == restored.id);
    CHECK(printTemplateForOrientation(restored, false, available).id == portrait.id);
    CHECK(printTemplateForOrientation(portrait, true, available).id == restored.id);
    CHECK(printTemplateForOrientation(restored, false, {}).id == restored.id);

    SheetTemplate wrongOrientation = portrait;
    wrongOrientation.landscape = true;
    CHECK(printTemplateForOrientation(restored, false, {wrongOrientation}).id == restored.id);
    landscape.printCompanionId = QStringLiteral("portrait.json");
    CHECK(!validateSheetTemplate(landscape, &error));
}

void testPrintPresets()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid())
        return;

    const QMarginsF margins(12.75, 8, 4.5, 9);
    const QPageSize custom(QSizeF(175.25, 280.5), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    const PrintPreset landscape{QPageLayout(custom, QPageLayout::Landscape, margins, QPageLayout::Millimeter),
                                QStringLiteral("user:company"), true, 600, 0.2};
    const PrintPreset portrait{QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, margins, QPageLayout::Millimeter),
                               QStringLiteral("builtin:default"), false, 150};

    const QString path = dir.filePath(QStringLiteral("print.ini"));
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("presets"), QVariantMap{
            {QStringLiteral("Custom / A"), landscape.toSettings()},
            {QStringLiteral("A4"), portrait.toSettings()}
        });
        settings.sync();
        CHECK(settings.status() == QSettings::NoError);
    }

    const QSettings settings(path, QSettings::IniFormat);
    const QVariantMap stored = settings.value(QStringLiteral("presets")).toMap();
    const auto verify = [&](const QString& name, const PrintPreset& expected) {
        const auto restored = PrintPreset::fromSettings(stored.value(name).toMap());
        CHECK(restored.has_value());
        if (!restored)
            return;
        CHECK(restored->paper.isEquivalentTo(expected.paper));
        CHECK(restored->paper.pageSize().id() == expected.paper.pageSize().id());
        CHECK(restored->paper.margins(QPageLayout::Millimeter) == margins);
        CHECK(restored->templateId == expected.templateId && restored->useTemplate == expected.useTemplate);
        CHECK(restored->imageDpi == expected.imageDpi && restored->scaleRatio == expected.scaleRatio);
    };
    verify(QStringLiteral("Custom / A"), landscape);
    verify(QStringLiteral("A4"), portrait);

    QVariantMap bad = landscape.toSettings();
    bad.insert(QStringLiteral("marginsMm"), QVariantList{500, 10, 500, 10});
    CHECK(!PrintPreset::fromSettings(bad));
    CHECK(!PrintPreset::fromSettings({}));
}

void testCalibratedPrintScale()
{
    const QSizeF pagePt(432, 360);
    const QSizeF modelMm(100, 50);
    Pdf3D::Pdf3DOptions options;
    QImage view(400, 200, QImage::Format_RGB32);
    view.fill(Qt::red);

    for (const int dpi : {254, 508})
    {
        QImage page((pagePt * (dpi / 72.0)).toSize(), QImage::Format_RGB32);
        QPainter painter(&page);
        bool tooLarge = true;
        CHECK(PrintPage::paint(painter, pagePt, dpi, false, options, 1, view, modelMm, &tooLarge));
        CHECK(!tooLarge);
        painter.end();

        const auto* row = reinterpret_cast<const QRgb*>(page.constScanLine(page.height() / 2));
        const int redPixels = std::count(row, row + page.width(), qRgb(255, 0, 0));
        const int expectedWidth = dpi * 100 / 25.4;
        CHECK(qAbs(redPixels - expectedWidth) <= 2);
        const QSize capture = PrintPage::rasterSize(pagePt, dpi, false, options, 1, view.size(), modelMm);
        CHECK(qAbs(capture.width() - expectedWidth) <= 1);
    }

    const QSizeF reduced = modelMm * 0.5;
    CHECK(qAbs(PrintPage::rasterSize(pagePt, 254, false, options, 1, view.size(), reduced).width() - 500) <= 1);

    QImage small(216, 216, QImage::Format_RGB32);
    QPainter painter(&small);
    bool tooLarge = false;
    CHECK(!PrintPage::paint(painter, QSizeF(small.size()), 72, false, options, 1, view, modelMm, &tooLarge));
    CHECK(tooLarge);
    CHECK(PrintPage::rasterSize(QSizeF(small.size()), 254, false, options, 1, view.size(), modelMm).isEmpty());
}

void testPrintCaptureSize()
{
    const Pdf3D::Pdf3DOptions options;
    const QSize view(1600, 800);
    const QSizeF paper(720, 432);
    const QSize draft = PrintPage::rasterSize(paper, 150, false, options, 1, view);
    const QSize normal = PrintPage::rasterSize(paper, 300, false, options, 1, view);
    CHECK(draft == QSize(1500, 750));
    CHECK(normal == QSize(3000, 1500));

    const QSize framed = PrintPage::rasterSize(paper, 300, true, options, 1, view);
    CHECK(!framed.isEmpty() && framed.width() < normal.width());
    CHECK(qAbs(framed.width() - 2 * framed.height()) <= 2);

    const QSize bounded = PrintPage::rasterSize(QSizeF(3500, 3000), 600, false, options, 1, QSize(1, 1));
    CHECK(!bounded.isEmpty() && bounded.width() == bounded.height());
    CHECK(qint64(bounded.width()) * bounded.height() <= 12000000);
    CHECK(PrintPage::rasterSize({}, 300, false, options, 1, view).isEmpty());
}

void testStaticPrintPdf()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid())
        return;

    const QString path = dir.filePath(QStringLiteral("sheet.pdf"));
    const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                             QMarginsF(15, 7, 9, 5), QPageLayout::Millimeter);
    Pdf3D::Pdf3DOptions options;
    options.title = QStringLiteral("Static sheet");
    options.drawingNumber = QStringLiteral("DRAWING-1");
    QImage view(32, 24, QImage::Format_RGB32);
    view.fill(Qt::red);

    QString error;
    CHECK(PrintPage::saveFile(path, PrintPage::FileFormat::Pdf, layout, true, options, 1, view, &error));
    const auto readPdf = [&path] {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QByteArray pdf = readPdf();
    CHECK(pdf.startsWith("%PDF-"));
    CHECK(pdf.contains("/Subtype /Image"));
    CHECK(pdf.contains("/Type /Font"));
    CHECK(!pdf.contains("/Subtype /3D"));

    // Failed export must keep an existing file intact.
    CHECK(!PrintPage::saveFile(path, PrintPage::FileFormat::Pdf, layout, true, options, 1, QImage(), &error));
    CHECK(!error.isEmpty());
    CHECK(readPdf() == pdf);
}

void testPublishedSheets()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid())
        return;
    const QString path = dir.filePath(QStringLiteral("published.pdf"));
    const QPageLayout paper(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                            QMarginsF(7, 11, 15, 8), QPageLayout::Millimeter);
    Pdf3D::Pdf3DOptions options;
    options.title = QStringLiteral("Publish sheets");
    QImage view(64, 32, QImage::Format_RGB32);
    view.fill(Qt::blue);
    QVector<Pdf3D::StaticSheet> sheets{{QStringLiteral("Assembly"), view},
                                     {QStringLiteral("Detail"), view},
                                     {QStringLiteral("Section"), view}};
    QString error;
    CHECK(PrintPage::saveSheets(path, paper, true, options, 1, sheets, &error));
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    file.close();
    CHECK(QRegularExpression(QStringLiteral("/Count\\s+3\\b")).match(QString::fromLatin1(original)).hasMatch());

    sheets[1].image = QImage();
    CHECK(!PrintPage::saveSheets(path, paper, true, options, 1, sheets, &error));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == original);
}

void testStaticPrintSvg()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid())
        return;

    const QString path = dir.filePath(QStringLiteral("sheet.svg"));
    const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                             QMarginsF(15, 7, 9, 5), QPageLayout::Millimeter);
    Pdf3D::Pdf3DOptions options;
    options.title = QStringLiteral("SVG sheet");
    options.drawingNumber = QStringLiteral("SVG-0123");
    QImage view(32, 24, QImage::Format_RGB32);
    view.fill(Qt::red);

    QString error;
    CHECK(PrintPage::saveFile(path, PrintPage::FileFormat::Svg, layout, true, options, 1, view, &error));
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray svg = file.readAll();
    file.close();
    QSvgRenderer renderer(svg);
    CHECK(renderer.isValid());
    CHECK(renderer.viewBoxF() == layout.fullRect(QPageLayout::Point));
    CHECK(svg.contains("<text") && svg.contains("SVG-0123"));
    CHECK(svg.contains("data:image/png;base64,"));
    CHECK(svg.contains("<rect"));

    // In plain mode the image fills the printable width while leaving the chosen physical margins.
    CHECK(PrintPage::saveFile(path, PrintPage::FileFormat::Svg, layout, false, options, 1, view, &error));
    CHECK(renderer.load(path));
    const QRectF printable = layout.paintRect(QPageLayout::Point);
    QImage paper(layout.fullRect(QPageLayout::Point).size().toSize(), QImage::Format_RGB32);
    paper.fill(Qt::black);
    QPainter painter(&paper);
    renderer.render(&painter, QRectF(paper.rect()));
    painter.end();
    const int y = qRound(printable.center().y());
    CHECK(paper.pixelColor(qRound(printable.left()) - 2, y) == QColor(Qt::white));
    CHECK(paper.pixelColor(qRound(printable.left()) + 2, y) == QColor(Qt::red));
    CHECK(paper.pixelColor(qRound(printable.right()) - 2, y) == QColor(Qt::red));
    CHECK(paper.pixelColor(qRound(printable.right()) + 2, y) == QColor(Qt::white));
}

// What the exported parts declare (PartData.h) and how it reaches the sheet: {material} and {mass} come from the parts, the user's own entry in
// a custom field of the same id wins, and a mass is counted once per part however many meshes the part became.
void testPartData()
{
    using namespace Pdf3D;
    U3dScene scene;
    scene.meshes.resize(1);
    const auto addNode = [&](const char* name, double kg, const char* group, const char* material, bool overlay = false) {
        U3dNode n;
        n.name = QString::fromLatin1(name);
        n.displayName = n.name;
        n.meshIndex = 0;
        n.overlay = overlay;
        n.declaredMassKg = kg;
        n.declaredMassGroup = QString::fromLatin1(group);
        n.declaredMaterial = QString::fromLatin1(material);
        scene.nodes.push_back(n);
    };

    addNode("a", -1.0, "", "");
    PartSummary s = summarisePartData(scene);
    CHECK(s.parts == 1 && s.withMass == 0 && s.withMaterial == 0 && s.mass.isEmpty() && s.material.isEmpty());   // nothing declared: nothing shown

    scene.nodes.clear();
    addNode("a", 2.5, "g1", "Steel");
    addNode("b", 0.5, "g2", "Steel");
    s = summarisePartData(scene);
    CHECK(s.parts == 2 && s.withMass == 2 && almostEqual(s.massKg, 3.0) && s.mass == QLatin1String("3 kg") && s.material == QLatin1String("Steel"));

    addNode("c", 4.0, "g3", "Brass");
    addNode("d", 4.0, "g3", "Brass");   // a second mesh of the same part: its mass is counted once
    s = summarisePartData(scene);
    CHECK(s.parts == 4 && s.withMass == 4 && almostEqual(s.massKg, 7.0) && s.mass == QLatin1String("7 kg"));
    CHECK(s.materials.size() == 2 && s.material == QLatin1String("2 materials"));

    addNode("e", -1.0, "", "");   // a part that declares nothing: the totals say how many parts they cover
    s = summarisePartData(scene);
    CHECK(s.parts == 5 && s.withMass == 4 && s.mass == QLatin1String("7 kg (4 of 5 parts)") && s.material == QLatin1String("2 materials (4 of 5 parts)"));

    addNode("ov", 100.0, "gx", "Gold", true);   // an overlay node is not a part
    s = summarisePartData(scene);
    CHECK(s.parts == 5 && almostEqual(s.massKg, 7.0));

    scene.nodes.clear();
    addNode("a", 1.0, "", "");
    addNode("b", 1.0, "", "");   // no group: each node counts on its own
    s = summarisePartData(scene);
    CHECK(almostEqual(s.massKg, 2.0) && s.mass == QLatin1String("2 kg") && s.material.isEmpty());

    scene.nodes.clear();
    addNode("a", std::nan(""), "", "Steel");   // an unusable number is not a mass
    s = summarisePartData(scene);
    CHECK(s.withMass == 0 && s.mass.isEmpty() && s.material == QLatin1String("Steel"));

    CHECK(formatMass(418.0762388) == QLatin1String("418.1 kg"));
    CHECK(formatMass(0.00346) == QLatin1String("0.00346 kg"));
    CHECK(formatMass(0.0004748) == QLatin1String("0.4748 g"));
    CHECK(formatMass(12345.6) == QLatin1String("12346 kg"));
    CHECK(formatMass(0.0) == QLatin1String("0 kg") && formatMass(-1.0).isEmpty());

    // the parts list's Material entry from the viewer's render material name: the importers' part-named materials are not materials
    const QStringList bases{ QStringLiteral("GX200 engine") };
    CHECK(renderMaterialLabel(QStringLiteral("GX200 engine (SOLID)"), bases).isEmpty());
    CHECK(renderMaterialLabel(QStringLiteral("GX200 engine (SOLID) (vertex colours)"), bases).isEmpty());
    CHECK(renderMaterialLabel(QStringLiteral("Material3"), bases).isEmpty());
    CHECK(renderMaterialLabel(QStringLiteral("Steel"), bases) == QLatin1String("Steel"));
    CHECK(renderMaterialLabel(QStringLiteral("Steel (vertex colours)"), bases) == QLatin1String("Steel"));
    CHECK(renderMaterialLabel(QStringLiteral("Brushed (alloy)"), bases) == QLatin1String("Brushed (alloy)"));
    CHECK(renderMaterialLabel(QStringLiteral("Materials"), bases) == QLatin1String("Materials") && renderMaterialLabel(QStringLiteral("Material"), bases) == QLatin1String("Material"));
    CHECK(renderMaterialLabel(QStringLiteral("GX200 engine (SOLID)"), QStringList()) == QLatin1String("GX200 engine (SOLID)"));   // another file's name is not recognised

    // the sheet values: from the parts, overridden by a non-empty custom field of the same id, never blanked by an empty one
    Pdf3DOptions options;
    options.sceneMaterial = QStringLiteral("Steel");
    options.sceneMass = QStringLiteral("3 kg");
    QMap<QString, QString> v = sheetValues(options, 1, 1, 1);
    CHECK(v.value(QStringLiteral("material")) == QLatin1String("Steel") && v.value(QStringLiteral("mass")) == QLatin1String("3 kg"));
    options.fieldValues.insert(QStringLiteral("material"), QStringLiteral("   "));
    v = sheetValues(options, 1, 1, 1);
    CHECK(v.value(QStringLiteral("material")) == QLatin1String("Steel"));
    options.fieldValues.insert(QStringLiteral("material"), QStringLiteral("Titanium"));
    options.fieldValues.insert(QStringLiteral("approvedBy"), QStringLiteral("A. B."));
    v = sheetValues(options, 1, 1, 1);
    CHECK(v.value(QStringLiteral("material")) == QLatin1String("Titanium") && v.value(QStringLiteral("approvedBy")) == QLatin1String("A. B."));
    options.fieldValues.insert(QStringLiteral("title"), QStringLiteral("not allowed to replace"));
    options.title = QStringLiteral("Real title");
    v = sheetValues(options, 1, 1, 1);
    CHECK(v.value(QStringLiteral("title")) == QLatin1String("Real title"));   // the standard values keep their precedence

    // templates: {material} and {mass} are known placeholders; a custom field with the id "material" (the sample template has one) still loads
    CHECK(sceneSheetPlaceholders() == QStringList({ QStringLiteral("material"), QStringLiteral("mass") }));
    SheetTemplate tpl;
    QString error;
    QStringList warnings;
    CHECK(parseSheetTemplate(sheetJson([](QJsonObject& o) {
        editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("text"), QStringLiteral("{material} {mass|-}")); });
    }), QStringLiteral("test"), &tpl, &error, &warnings) && warnings.isEmpty());
    CHECK(parseSheetTemplate(sheetJson([](QJsonObject& o) {
        QJsonObject field;
        field.insert(QStringLiteral("id"), QStringLiteral("material"));
        field.insert(QStringLiteral("label"), QStringLiteral("Material"));
        field.insert(QStringLiteral("type"), QStringLiteral("choice"));
        field.insert(QStringLiteral("options"), QJsonArray{ QStringLiteral("Steel"), QStringLiteral("Other") });
        o.insert(QStringLiteral("fields"), QJsonArray{ field });
        editFirstCell(o, [](QJsonObject& c) { c.insert(QStringLiteral("text"), QStringLiteral("{material}")); });
    }), QStringLiteral("test"), &tpl, &error, &warnings) && tpl.fields.size() == 1);
    CHECK(rejects(sheetJson([](QJsonObject& o) {
        QJsonObject field;
        field.insert(QStringLiteral("id"), QStringLiteral("title"));
        field.insert(QStringLiteral("label"), QStringLiteral("T"));
        o.insert(QStringLiteral("fields"), QJsonArray{ field });
    }), QStringLiteral("already a placeholder name")));   // the standard names stay reserved
}

// What the template editor relies on: coordinates written and read as text, and the validity check of an edited template.
void testSheetEditing()
{
    using namespace Pdf3D;
    const double pt = 72.0 / 25.4;
    SheetEdge e;
    QString error;
    CHECK(parseSheetEdgeText(QStringLiteral("12.5"), &e, &error) && !e.fraction && almostEqual(e.value, 12.5 * pt));
    CHECK(parseSheetEdgeText(QStringLiteral(" -99 "), &e) && !e.fraction && almostEqual(e.value, -99.0 * pt));
    CHECK(parseSheetEdgeText(QStringLiteral("12,5"), &e) && almostEqual(e.value, 12.5 * pt));   // a decimal comma
    CHECK(parseSheetEdgeText(QStringLiteral("1/4"), &e) && e.fraction && almostEqual(e.value, 0.25));
    CHECK(parseSheetEdgeText(QStringLiteral("-1/3"), &e) && e.fraction && almostEqual(e.value, -1.0 / 3.0));
    CHECK(parseSheetEdgeText(QStringLiteral("37.5%"), &e) && e.fraction && almostEqual(e.value, 0.375));
    CHECK(parseSheetEdgeText(QStringLiteral("100 %"), &e) && e.fraction && almostEqual(e.value, 1.0));
    for (const char* bad : { "", "abc", "12mm", "1e3", "1/0", "1//2", "--5", "%", "1000000", "5%%" })
        CHECK(!parseSheetEdgeText(QString::fromLatin1(bad), &e, &error) && !error.isEmpty());

    // written text reads back to the same coordinate
    for (const char* text : { "0", "12.5", "-99", "3.25", "1/4", "-1/3", "3/10", "1/2", "100%", "-100%", "37.51%" })
    {
        SheetEdge a, b;
        CHECK(parseSheetEdgeText(QString::fromLatin1(text), &a));
        const QString written = sheetEdgeToText(a);
        CHECK(parseSheetEdgeText(written, &b) && b.fraction == a.fraction && almostEqual(a.value, b.value));
    }
    CHECK(sheetEdgeToText(SheetEdge{ 12.5 * pt, false }) == QLatin1String("12.5"));
    CHECK(sheetEdgeToText(SheetEdge{ 0.25, true }) == QLatin1String("1/4"));
    CHECK(sheetEdgeToText(SheetEdge{ 0.5, true }) == QLatin1String("1/2"));
    CHECK(sheetEdgeToText(SheetEdge{ -0.25, true }) == QLatin1String("-1/4"));
    CHECK(sheetEdgeToText(SheetEdge{ 1.0, true }) == QLatin1String("100%"));
    CHECK(sheetEdgeToText(SheetEdge{ 0.375, true }) == QLatin1String("3/8"));
    CHECK(sheetEdgeToText(SheetEdge{ 0.3751, true }) == QLatin1String("37.51%"));

    // the validity check an edit goes through
    const QVector<SheetTemplate> builtIns = builtInSheetTemplates();
    CHECK(!builtIns.isEmpty());
    for (const SheetTemplate& b : builtIns)
        CHECK(validateSheetTemplate(b, &error));   // what is edited must be valid to begin with
    SheetTemplate tpl = builtIns.value(0);
    tpl.name = QString();
    CHECK(!validateSheetTemplate(tpl, &error) && error.contains(QLatin1String("name")));
    tpl = builtIns.value(0);
    tpl.cells[0].rect.right = SheetEdge{ 5000.0, false };   // reaches outside the title block
    CHECK(!validateSheetTemplate(tpl, &error) && error.contains(QLatin1String("outside the title block")));
    tpl = builtIns.value(0);
    tpl.cells[0].rect.left = SheetEdge{ 0.9, true };
    tpl.cells[0].rect.right = SheetEdge{ 0.1, true };   // right of the left edge is required
    CHECK(!validateSheetTemplate(tpl, &error) && error.contains(QLatin1String("no area")));
    tpl = builtIns.value(0);
    SheetCell added;   // the cell the editor's Add creates: valid inside the Default block
    added.text = QStringLiteral("{title}");
    added.rect.left = SheetEdge{ 10.0, false };
    added.rect.top = SheetEdge{ 10.0, false };
    added.rect.right = SheetEdge{ 0.3, true };
    added.rect.bottom = SheetEdge{ 0.5, true };
    tpl.cells.append(added);
    CHECK(validateSheetTemplate(tpl, &error));
    // an edited template survives writing and reading
    tpl.cells.last().text = QStringLiteral("{material|-} / {mass|-}");
    tpl.cells.last().color = QColor(200, 30, 30);
    tpl.cells.last().align = Qt::AlignRight;
    tpl.id = QStringLiteral("user:edited");
    SheetTemplate back;
    QStringList warnings;
    CHECK(parseSheetTemplate(sheetTemplateToJson(tpl), tpl.id, &back, &error, &warnings) && back.cells.size() == tpl.cells.size());
    if (back.cells.size() == tpl.cells.size())
    {
        const SheetCell& c = back.cells.last();
        CHECK(c.text == tpl.cells.last().text && c.color == QColor(200, 30, 30) && (c.align & Qt::AlignHorizontal_Mask) == Qt::AlignRight);
    }

    // what the Page tab changes: every named page size, a custom size, orientation, the block anchor and heights
    const QVector<QPair<QString, QPageSize::PageSizeId>> sizes = sheetPageSizeChoices();
    CHECK(sizes.size() == 9 && sizes.first().first == QLatin1String("A0") && sizes.first().second == QPageSize::A0);
    bool hasA4 = false, hasLetter = false;
    for (const auto& s : sizes)
    {
        hasA4 = hasA4 || (s.first == QLatin1String("A4") && s.second == QPageSize::A4);
        hasLetter = hasLetter || (s.first == QLatin1String("Letter") && s.second == QPageSize::Letter);
        SheetTemplate sized = builtIns.value(0);
        sized.pageSizeId = s.second;
        SheetTemplate readBack;
        CHECK(parseSheetTemplate(sheetTemplateToJson(sized), QStringLiteral("user:s"), &readBack, &error) && readBack.pageSizeId == s.second);
    }
    CHECK(hasA4 && hasLetter);
    SheetTemplate paged = builtIns.value(0);
    paged.pageSizeId = QPageSize::Custom;
    paged.customSizeMm = QSizeF(300.0, 200.0);
    paged.landscape = false;
    SheetTemplate pagedBack;
    CHECK(parseSheetTemplate(sheetTemplateToJson(paged), QStringLiteral("user:p"), &pagedBack, &error) && pagedBack.pageSizeId == QPageSize::Custom
          && almostEqual(pagedBack.customSizeMm.width(), 300.0) && almostEqual(pagedBack.customSizeMm.height(), 200.0) && !pagedBack.landscape);
    // the limits themselves are allowed (100 mm must survive the trip through points); the Default cells need not fit such a page,
    // so only the page size check is looked at
    paged.customSizeMm = QSizeF(100.0, 100.0);
    CHECK(validateSheetTemplate(paged, &error) || !error.contains(QLatin1String("custom page size")));
    paged.customSizeMm = QSizeF(5000.0, 5000.0);
    CHECK(validateSheetTemplate(paged, &error) || !error.contains(QLatin1String("custom page size")));
    paged.customSizeMm = QSizeF(99.0, 200.0);
    CHECK(!validateSheetTemplate(paged, &error) && error.contains(QLatin1String("custom page size")));
    paged.customSizeMm = QSizeF(50.0, 200.0);
    CHECK(!validateSheetTemplate(paged, &error) && error.contains(QLatin1String("custom page size")));
    paged.customSizeMm = QSizeF(5001.0, 200.0);
    CHECK(!validateSheetTemplate(paged, &error) && error.contains(QLatin1String("custom page size")));
    SheetTemplate corner = builtIns.value(0);
    corner.anchor = TitleBlockAnchor::BottomRight;
    corner.blockWidth = 300.0;
    CHECK(validateSheetTemplate(corner, &error) || !error.isEmpty());   // either way the editor gets an answer it can show
    corner.blockWidth = 5.0;
    CHECK(!validateSheetTemplate(corner, &error) && error.contains(QLatin1String("width")));
    SheetTemplate tall = builtIns.value(0);
    tall.blockHeight = 600.0;
    tall.blockMaxHeight = 600.0;
    CHECK(!validateSheetTemplate(tall, &error) && error.contains(QLatin1String("height")));

    // what the Fields tab changes: a field of each type, a choice list, a taken name, too many fields
    SheetTemplate withFields = builtIns.value(0);
    withFields.id = QStringLiteral("user:f");
    SheetField text;
    text.id = QStringLiteral("projectNo");
    text.label = QStringLiteral("Project");
    SheetField choice;
    choice.id = QStringLiteral("grade");
    choice.label = QStringLiteral("Grade");
    choice.type = QStringLiteral("choice");
    choice.options = QStringList{ QStringLiteral("S235"), QStringLiteral("S355") };
    choice.defaultValue = QStringLiteral("S235");
    SheetField date;
    date.id = QStringLiteral("issued");
    date.label = QStringLiteral("Issued");
    date.type = QStringLiteral("date");
    SheetField multi;
    multi.id = QStringLiteral("remarks");
    multi.label = QStringLiteral("Remarks");
    multi.type = QStringLiteral("multiline");
    withFields.fields = { text, choice, date, multi };
    SheetTemplate fieldsBack;
    CHECK(parseSheetTemplate(sheetTemplateToJson(withFields), withFields.id, &fieldsBack, &error) && fieldsBack.fields.size() == 4
          && fieldsBack.fields[1].options == choice.options && fieldsBack.fields[1].defaultValue == QLatin1String("S235")
          && fieldsBack.fields[2].type == QLatin1String("date") && fieldsBack.fields[3].type == QLatin1String("multiline"));
    SheetTemplate bad = withFields;
    bad.fields[0].id = QStringLiteral("author");   // a standard placeholder name
    CHECK(!validateSheetTemplate(bad, &error) && error.contains(QLatin1String("already a placeholder name")));
    bad = withFields;
    bad.fields[1].options.clear();   // a choice needs its list
    CHECK(!validateSheetTemplate(bad, &error) && error.contains(QLatin1String("choice")));
    bad = withFields;
    bad.fields[0].id = QStringLiteral("1st");
    CHECK(!validateSheetTemplate(bad, &error) && error.contains(QLatin1String("id")));
    bad = withFields;
    bad.fields[2].id = bad.fields[0].id;   // two fields with one id
    CHECK(!validateSheetTemplate(bad, &error));
    bad = withFields;
    bad.fields.clear();
    for (int i = 0; i < 17; ++i)
    {
        SheetField f;
        f.id = QStringLiteral("f%1").arg(i);
        f.label = QStringLiteral("F");
        bad.fields.append(f);
    }
    CHECK(!validateSheetTemplate(bad, &error) && error.contains(QLatin1String("custom fields")));
    withFields.fields[0].id = QStringLiteral("material");   // a field that takes over a scene value is allowed
    CHECK(validateSheetTemplate(withFields, &error));

    // what the Logo tab changes: a logo and its position round trip, and a template without one has none
    SheetTemplate logoed = builtIns.value(0);
    logoed.id = QStringLiteral("user:l");
    logoed.hasLogo = true;
    logoed.logo.file = QStringLiteral("mark.svg");
    logoed.logo.rect.left = SheetEdge{ 4.0, false };
    logoed.logo.rect.right = SheetEdge{ 0.25, true };
    logoed.logo.rect.top = SheetEdge{ 4.0, false };
    logoed.logo.rect.bottom = SheetEdge{ -4.0, false };
    logoed.logo.contain = false;
    SheetTemplate logoBack;
    CHECK(parseSheetTemplate(sheetTemplateToJson(logoed), logoed.id, &logoBack, &error) && logoBack.hasLogo && logoBack.logo.file == QLatin1String("mark.svg")
          && logoBack.logo.rect.right.fraction && almostEqual(logoBack.logo.rect.right.value, 0.25) && !logoBack.logo.contain);
    logoed.hasLogo = false;
    CHECK(parseSheetTemplate(sheetTemplateToJson(logoed), logoed.id, &logoBack, &error) && !logoBack.hasLogo);

    // changing the page carries the layout over: the cells scaled to the new block width stay valid for every named size and orientation
    for (const SheetTemplate& base : builtIns)
    {
        for (const auto& s : sizes)
        {
            for (const bool landscape : { true, false })
            {
                SheetTemplate moved = base;
                moved.pageSizeId = s.second;
                moved.landscape = landscape;
                const double ratio = sheetBlockWidthPt(moved) / sheetBlockWidthPt(base);
                CHECK(sheetBlockWidthPt(moved) > 0.0 && ratio > 0.0);
                const SheetTemplate scaled = scaleSheetTemplateWidth(moved, ratio);
                CHECK(validateSheetTemplate(scaled, &error));
                if (!error.isEmpty() && !validateSheetTemplate(scaled))
                    std::printf("  scaled %s %s %s: %s\n", qPrintable(base.name), qPrintable(s.first), landscape ? "landscape" : "portrait", qPrintable(error));
            }
        }
    }
    // the scaling itself: lengths scale, fractions and heights do not, and scaling by the inverse gives the template back
    SheetTemplate def = builtIns.value(0);
    const SheetTemplate twice = scaleSheetTemplateWidth(def, 2.0);
    CHECK(almostEqual(twice.cells[3].rect.left.value, def.cells[3].rect.left.value * 2.0) && almostEqual(twice.cells[3].rect.top.value, def.cells[3].rect.top.value));
    const SheetTemplate roundTrip = scaleSheetTemplateWidth(twice, 0.5);
    bool same = roundTrip.cells.size() == def.cells.size();
    for (int i = 0; same && i < def.cells.size(); ++i)
        same = almostEqual(roundTrip.cells[i].rect.left.value, def.cells[i].rect.left.value) && almostEqual(roundTrip.cells[i].rect.right.value, def.cells[i].rect.right.value)
               && roundTrip.cells[i].rect.left.fraction == def.cells[i].rect.left.fraction;
    CHECK(same);
    SheetTemplate fractional = def;
    fractional.cells[0].rect.right = SheetEdge{ 0.4, true };
    CHECK(almostEqual(scaleSheetTemplateWidth(fractional, 3.0).cells[0].rect.right.value, 0.4));
    // the width of the block: between the margins for a full width block, its own width (at most that) for a corner block
    SheetTemplate widthTpl = def;
    CHECK(almostEqual(sheetBlockWidthPt(widthTpl), sheetPageSizePt(widthTpl).width() - 2.0 * widthTpl.margin));
    widthTpl.anchor = TitleBlockAnchor::BottomLeft;
    widthTpl.blockWidth = 200.0;
    CHECK(almostEqual(sheetBlockWidthPt(widthTpl), 200.0));
    widthTpl.blockWidth = 5000.0;
    CHECK(almostEqual(sheetBlockWidthPt(widthTpl), sheetPageSizePt(widthTpl).width() - 2.0 * widthTpl.margin));

    // number fields: the type, its settings and limits, and reading and printing in the style of a language
    SheetField number;
    number.id = QStringLiteral("massKg");
    number.label = QStringLiteral("Mass");
    number.type = QStringLiteral("number");
    number.decimals = 2;
    number.unit = QStringLiteral("kg");
    number.minimum = 0.0;
    number.maximum = 100000.0;
    number.defaultValue = QStringLiteral("12.5");
    SheetTemplate withNumber = builtIns.value(0);
    withNumber.id = QStringLiteral("user:n");
    withNumber.fields = { number };
    SheetTemplate numberBack;
    CHECK(parseSheetTemplate(sheetTemplateToJson(withNumber), withNumber.id, &numberBack, &error) && numberBack.fields.size() == 1);
    if (numberBack.fields.size() == 1)
    {
        const SheetField& n = numberBack.fields[0];
        CHECK(n.type == QLatin1String("number") && n.decimals == 2 && n.unit == QLatin1String("kg") && almostEqual(n.minimum, 0.0) && almostEqual(n.maximum, 100000.0)
              && n.defaultValue == QLatin1String("12.5"));
    }
    SheetTemplate plainNumber = withNumber;
    plainNumber.fields[0].minimum = -1e9;
    plainNumber.fields[0].maximum = 1e9;
    plainNumber.fields[0].unit.clear();
    plainNumber.fields[0].defaultValue.clear();
    const QString plainJson = QString::fromUtf8(sheetTemplateToJson(plainNumber));
    CHECK(!plainJson.contains(QLatin1String("\"min\"")) && !plainJson.contains(QLatin1String("\"max\"")) && !plainJson.contains(QLatin1String("\"unit\""))
          && !plainJson.contains(QLatin1String("\"decimals\"")));   // the defaults are not written
    CHECK(parseSheetTemplate(sheetTemplateToJson(plainNumber), withNumber.id, &numberBack, &error) && numberBack.fields[0].minimum == -1e9 && numberBack.fields[0].maximum == 1e9);
    SheetTemplate badNumber = withNumber;
    badNumber.fields[0].decimals = 7;
    CHECK(!validateSheetTemplate(badNumber, &error) && error.contains(QLatin1String("decimals")));
    badNumber = withNumber;
    badNumber.fields[0].minimum = 10.0;
    badNumber.fields[0].maximum = 5.0;
    CHECK(!validateSheetTemplate(badNumber, &error) && error.contains(QLatin1String("minimum")));
    badNumber = withNumber;
    badNumber.fields[0].defaultValue = QStringLiteral("abc");
    CHECK(!validateSheetTemplate(badNumber, &error) && error.contains(QLatin1String("default")));
    badNumber.fields[0].defaultValue = QStringLiteral("-5");   // below the minimum
    CHECK(!validateSheetTemplate(badNumber, &error) && error.contains(QLatin1String("default")));
    badNumber = withNumber;
    badNumber.fields[0].unit = QString(30, QLatin1Char('u'));
    CHECK(!validateSheetTemplate(badNumber, &error));
    badNumber = withNumber;
    badNumber.fields[0].type = QStringLiteral("integer");
    CHECK(!validateSheetTemplate(badNumber, &error) && error.contains(QLatin1String("number")));

    const QLocale german(QLocale::German, QLocale::Germany), english(QLocale::English, QLocale::UnitedStates);
    double parsed = 0.0;
    CHECK(parseSheetNumber(QStringLiteral("12,5"), german, &parsed) && almostEqual(parsed, 12.5));
    CHECK(parseSheetNumber(QStringLiteral("12.5"), german, &parsed) && almostEqual(parsed, 12.5));      // a plain point is accepted in every language
    CHECK(parseSheetNumber(QStringLiteral("1.234,5"), german, &parsed) && almostEqual(parsed, 1234.5));
    CHECK(parseSheetNumber(QStringLiteral("12.5"), english, &parsed) && almostEqual(parsed, 12.5));
    CHECK(parseSheetNumber(QStringLiteral(" 7 "), english, &parsed) && almostEqual(parsed, 7.0));
    CHECK(!parseSheetNumber(QString(), english, &parsed) && !parseSheetNumber(QStringLiteral("abc"), english, &parsed) && !parseSheetNumber(QStringLiteral("1 2"), english, &parsed));
    CHECK(formatSheetNumber(number, 12.5, german) == QLatin1String("12,50 kg"));
    CHECK(formatSheetNumber(number, 12.5, english) == QLatin1String("12.50 kg"));
    CHECK(formatSheetNumber(number, 1234567.891, english) == QLatin1String("1234567.89 kg"));        // no digit grouping on a sheet
    CHECK(formatSheetNumber(number, 1234567.891, german) == QLatin1String("1234567,89 kg"));
    number.unit.clear();
    number.decimals = 0;
    CHECK(formatSheetNumber(number, 2.4, english) == QLatin1String("2") && formatSheetNumber(number, 2.6, english) == QLatin1String("3"));
    number.decimals = 3;
    CHECK(formatSheetNumber(number, -0.5, german) == QLatin1String("-0,500"));

    // placing a cell with the mouse: a rectangle goes back to edges written like the cell's own, and resolves to the same rectangle
    const QSizeF dragBlock(400.0, 100.0);
    SheetRect like;
    like.left = SheetEdge{ 10.0, false };      // from the left
    like.right = SheetEdge{ -50.0, false };    // from the right
    like.top = SheetEdge{ 0.1, true };         // a fraction from the top
    like.bottom = SheetEdge{ -0.2, true };     // a fraction from the bottom
    const QRectF dragged(30.0, 20.0, 200.0, 40.0);
    const SheetRect edges = sheetRectFromBlockRect(dragged, like, dragBlock);
    CHECK(!edges.left.fraction && edges.left.value > 0.0 && !edges.right.fraction && edges.right.value < 0.0
          && edges.top.fraction && edges.top.value > 0.0 && edges.bottom.fraction && edges.bottom.value < 0.0);
    CHECK(almostEqual(resolveSheetEdge(edges.left, dragBlock.width()), 30.0) && almostEqual(resolveSheetEdge(edges.right, dragBlock.width()), 230.0)
          && almostEqual(resolveSheetEdge(edges.top, dragBlock.height()), 20.0) && almostEqual(resolveSheetEdge(edges.bottom, dragBlock.height()), 60.0));
    const SheetRect fromNear = sheetRectFromBlockRect(dragged, SheetRect(), dragBlock);   // a default rectangle: lengths from the left and top
    CHECK(!fromNear.right.fraction && fromNear.right.value > 0.0 && almostEqual(resolveSheetEdge(fromNear.right, dragBlock.width()), 230.0)
          && almostEqual(resolveSheetEdge(fromNear.bottom, dragBlock.height()), 60.0));
    // the edited template stays valid after a drag
    SheetTemplate moved = builtIns.value(0);
    const QSizeF movedBlock(sheetBlockWidthPt(moved), moved.blockHeight);
    moved.cells[0].rect = sheetRectFromBlockRect(QRectF(20.0, 12.0, 150.0, 22.0), moved.cells[0].rect, movedBlock);
    CHECK(validateSheetTemplate(moved, &error));
    // positions are written in millimetres with at most two decimals
    CHECK(sheetEdgeToText(SheetEdge{ 10.0, false }) == QLatin1String("3.53"));
    CHECK(sheetEdgeToText(SheetEdge{ -10.0, false }) == QLatin1String("-3.53"));
    CHECK(sheetEdgeToText(SheetEdge{ 28.3464567, false }) == QLatin1String("10"));
    CHECK(sheetEdgeToText(SheetEdge{ 0.0, false }) == QLatin1String("0") && sheetEdgeToText(SheetEdge{ -0.001, false }) == QLatin1String("0"));
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], Pdf3D::kU3dWorkerArgument) == 0)
    {
        QCoreApplication worker(argc, argv);
        const QString workspace = worker.arguments().value(2);
        const QByteArray marker = QByteArray::fromBase64(qgetenv(kSlowEncoderMarker));
        if (!marker.isEmpty())
        {
            QSaveFile started(QString::fromUtf8(marker));
            if (!started.open(QIODevice::WriteOnly))
                return 1;
            const QByteArray folder = workspace.toUtf8();
            if (started.write(folder) != folder.size() || !started.commit())
                return 1;
            // The parent has to stop this process, which deliberately never runs the real encoder.
            QThread::sleep(15);
            return 1;
        }
        return Pdf3D::runU3dWorker(workspace);
    }

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
    QStandardPaths::setTestModeEnabled(true);   // the user's template folder (AppData) is a test location, not the real one
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
    testViewOverlayVisibilityJson();
    testAnnotationText();
    testAssemblyFolders();
    testOverlayToU3d();
    testSceneToU3d();
    testVertexColors();
    testAppender();
    testU3dWriter();
    testSheetTemplates();
    testSheetRenderer();
    testPrintPageReflow();
    testPrintCaptureSize();
    testCalibratedPrintScale();
    testPrintPresets();
    testPrintTemplateCompanion();
    testStaticPrintPdf();
    testStaticPrintSvg();
    testPublishedSheets();
    testPartData();
    testSheetEditing();
    if (full)
        testFullExport();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
