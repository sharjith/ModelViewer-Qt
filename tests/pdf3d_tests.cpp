// Tests for the Save as 3D PDF export (src/Pdf3D). GUI-free except the optional end-to-end test.
//
//   pdf3d_tests          view dictionaries, aiScene conversion, incremental PDF update on a real QPdfWriter page, the
//                        in-process u3d conversion (skipped when the u3d modules are not next to the test executable)
//   pdf3d_tests --full   additionally runs Pdf3DExporter::exportToFile (text layout and fonts) and checks the written file
#include "ClippingState.h"
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

int main(int argc, char** argv)
{
    bool full = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--full") == 0)
            full = true;

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

    testViews();
    testCameraViews();
    testFittedViews();
    testClippingStateJson();
    testSceneToU3d();
    testVertexColors();
    testAppender();
    testU3dWriter();
    if (full)
        testFullExport();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
