// Tests for the Save as 3D PDF export (src/Pdf3D). GUI-free except the optional end-to-end test.
//
//   pdf3d_tests          view dictionaries, aiScene conversion, incremental PDF update on a real QPdfWriter page, the
//                        in-process u3d conversion (skipped when the u3d modules are not next to the test executable)
//   pdf3d_tests --full   additionally runs Pdf3DExporter::exportToFile (text layout and fonts) and checks the written file
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
    const double distance = radius / std::sin(15.0 * 3.14159265358979 / 180.0) * 1.25;

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
    testSceneToU3d();
    testVertexColors();
    testAppender();
    testU3dWriter();
    if (full)
        testFullExport();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
