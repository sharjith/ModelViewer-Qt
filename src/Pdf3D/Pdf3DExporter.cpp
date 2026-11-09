#include "Pdf3DExporter.h"

#include "PdfAppender.h"
#include "PartData.h"
#include "SheetRenderer.h"
#include "SheetTemplate.h"
#include "U3dWriter.h"

#include <QBuffer>
#include <QMap>
#include <QCoreApplication>
#include <QDate>
#include <QFileInfo>
#include <QFont>
#include <QStringList>
#include <QHash>
#include <cmath>
#include <QSet>
#include <QVector>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QDir>
#include <QImage>
#include <QSvgRenderer>

namespace Pdf3D
{
namespace
{

constexpr qreal kButtonStripHeight = 20.0;   // the view/animation buttons under the 3D area
constexpr qreal kButtonStripGap = 6.0;

struct Layout
{
    SheetTemplate tpl;    // the sheet template (page, border, title block cells); see SheetTemplate.h
    ResolvedSheet sheet;  // its rectangles for this page size and title block height
    QRectF page;          // full page, painter coordinates (points, origin top left)
    QRectF area3d;        // the 3D area, painter coordinates (sheet.area3d minus the button strip)
    QRectF buttonStrip;   // empty when there are no buttons
    qreal  pageHeightPt = 0.0;
    bool   notesScroll = false;   // the scroll cell's text does not fit: a scrollable form field is added over the cell
    QRectF scrollRect;            // that cell, painter coordinates
    QString scrollText;
    qreal  scrollFontSize = 8.0;
};

QRectF toPdfRect(const QRectF& painterRect, qreal pageHeightPt)
{
    // PDF origin is at the bottom left, y up: QRectF(left, bottom, width, height) with bottom < top is not what Qt's QRectF
    // means, so the rect is returned as (left, pdfBottom) to (right, pdfTop) using top() = pdfBottom, bottom() = pdfTop.
    const qreal pdfBottom = pageHeightPt - painterRect.bottom();
    const qreal pdfTop = pageHeightPt - painterRect.top();
    return QRectF(QPointF(painterRect.left(), pdfBottom), QPointF(painterRect.right(), pdfTop));
}

// The buttons of the strip, left to right: previous and next view (GoTo3DView actions), one button per view when they all fit at
// a readable width, and at the right the animation controls (JavaScript: PDF has no play/pause action). `strip` is in painter
// coordinates; the buttons come back in PDF coordinates.
QList<PageButton> makeButtons(const QStringList& viewNames, bool animation, const QRectF& strip, qreal pageHeightPt)
{
    QList<PageButton> buttons;
    const qreal gap = 4.0, small = 26.0, animW = 40.0, minView = 54.0;
    const auto add = [&](qreal x, qreal w, const QString& caption, const QByteArray& action) {
        PageButton b;
        b.rect = toPdfRect(QRectF(x, strip.top(), w, strip.height()), pageHeightPt);
        b.caption = caption;
        b.action = action;
        buttons.append(b);
    };
    const auto goTo = [](const QByteArray& view) { return "<< /S /GoTo3DView /TA @ANNOT@ /V " + view + " >>"; };
    const auto script = [](const QByteArray& call) {
        // runtime.play()/pause() need the 3D content to be activated; setting activated true is harmless when it already is
        return "<< /S /JavaScript /JS (var a = this.getAnnots3D(0)[0]; if (a) { if (!a.activated) { a.activated = true; } "
               "try { a.context3D.runtime." + call + "(); } catch (e) { } }) >>";
    };

    qreal x = strip.left();
    qreal right = strip.right();
    if (animation)
    {
        right -= animW;
        add(right, animW, QCoreApplication::translate("Pdf3DExporter", "Pause"), script("pause"));
        right -= gap + animW;
        add(right, animW, QCoreApplication::translate("Pdf3DExporter", "Play"), script("play"));
        right -= gap * 3;
    }
    if (viewNames.size() > 1)
    {
        add(x, small, QStringLiteral("<"), goTo("/P"));
        x += small + gap;
        add(x, small, QStringLiteral(">"), goTo("/N"));
        x += small + gap * 3;
        const int n = static_cast<int>(viewNames.size());
        const qreal each = (right - x - gap * (n - 1)) / n;
        if (each >= minView)
        {
            for (int i = 0; i < n; ++i)
            {
                add(x, qMin(each, 90.0), viewNames[i], goTo(QByteArray::number(i)));
                x += qMin(each, 90.0) + gap;
            }
        }
    }
    return buttons;
}

struct PartRow
{
    QString name;
    QString material;
    int     quantity = 0;
};

// One row per distinct (name, mesh) pair, in first-appearance order; instances of the same part add to the quantity.
QVector<PartRow> buildPartRows(const U3dScene& scene, const QString& sourceFiles)
{
    QStringList fileBaseNames;
    for (const QString& file : sourceFiles.split(QStringLiteral(", "), Qt::SkipEmptyParts))
        fileBaseNames << QFileInfo(file).completeBaseName();

    QVector<PartRow> rows;
    QHash<QString, int> index;
    for (const U3dNode& n : scene.nodes)
    {
        if (n.overlay || n.meshIndex < 0 || n.meshIndex >= scene.meshes.size())
            continue;
        const QString key = n.displayName + QLatin1Char('|') + QString::number(n.meshIndex) + QLatin1Char('|') + n.declaredMaterial;
        auto it = index.constFind(key);
        if (it != index.constEnd())
        {
            ++rows[it.value()].quantity;
            continue;
        }
        PartRow r;
        r.name = n.displayName;
        const int mat = scene.meshes[n.meshIndex].materialIndex;
        r.material = (mat >= 0 && mat < scene.materials.size()) ? renderMaterialLabel(scene.materials[mat].name, fileBaseNames) : QString();
        if (!n.declaredMaterial.isEmpty())
            r.material = n.declaredMaterial;   // the designation the source file declares replaces the viewer's render material name
        r.quantity = 1;
        index.insert(key, rows.size());
        rows.push_back(r);
    }
    return rows;
}

constexpr qreal kPartsHeaderHeight = 22.0;
constexpr qreal kPartsRowHeight = 17.0;

int partsRowsPerSheet(const Layout& l)
{
    return qMax(1, static_cast<int>((l.area3d.height() - kPartsHeaderHeight) / kPartsRowHeight));
}

// Border, 3D area frame and title block, identical on every sheet; the drawing is shared with the template preview (SheetRenderer.h).
void drawFrame(QPainter& p, const Layout& l, const Pdf3DOptions& options, int partCount, int sheet, int sheetCount)
{
    drawSheetFrame(p, l.tpl, l.sheet, l.area3d, sheetValues(options, partCount, sheet, sheetCount));
}

// Sheet 1: the frame plus the placeholder under the 3D annotation (the poster when there is one, otherwise a hint for readers
// without 3D support).
void drawModelSheet(QPainter& p, const Layout& l, const Pdf3DOptions& options, int partCount, int sheetCount)
{
    drawFrame(p, l, options, partCount, 1, sheetCount);
    QPen thin(Qt::black, 0.6);
    if (!options.poster.isNull())
    {
        QSizeF s = options.poster.size();
        s.scale(l.area3d.size() - QSizeF(4, 4), Qt::KeepAspectRatio);
        const QRectF target(QPointF(l.area3d.center().x() - s.width() / 2, l.area3d.center().y() - s.height() / 2), s);
        p.drawImage(target, options.poster);
    }
    else
    {
        QFont f = p.font();
        f.setPointSizeF(11);
        p.setFont(f);
        p.setPen(QColor(110, 110, 110));
        p.drawText(l.area3d, Qt::AlignCenter | Qt::TextWordWrap,
                   QCoreApplication::translate("Pdf3DExporter", "3D content: open this file in Adobe Acrobat or Reader and enable 3D."));
        p.setPen(thin);
    }
}

// Parts list sheet `sheetIndex` (0-based among the list sheets).
void drawPartsSheet(QPainter& p, const Layout& l, const Pdf3DOptions& options, const QVector<PartRow>& rows, int partCount,
                    int sheetIndex, int sheetCount)
{
    drawFrame(p, l, options, partCount, 2 + sheetIndex, sheetCount);

    const qreal x0 = l.area3d.left();
    const qreal itemW = 46.0, qtyW = 60.0, matW = 260.0;
    const qreal nameW = l.area3d.width() - itemW - qtyW - matW;
    const qreal xs[5] = {x0, x0 + itemW, x0 + itemW + nameW, x0 + itemW + nameW + matW, x0 + l.area3d.width()};

    QFont f = p.font();
    f.setPointSizeF(9);
    f.setBold(true);
    p.setFont(f);
    p.fillRect(QRectF(x0, l.area3d.top(), l.area3d.width(), kPartsHeaderHeight), QColor(235, 235, 235));
    const QString heads[4] = {QCoreApplication::translate("Pdf3DExporter", "Item"), QCoreApplication::translate("Pdf3DExporter", "Part"),
                              QCoreApplication::translate("Pdf3DExporter", "Material"), QCoreApplication::translate("Pdf3DExporter", "Qty")};
    for (int c = 0; c < 4; ++c)
        p.drawText(QRectF(xs[c] + 6, l.area3d.top(), xs[c + 1] - xs[c] - 12, kPartsHeaderHeight), Qt::AlignLeft | Qt::AlignVCenter, heads[c]);
    p.drawLine(QPointF(x0, l.area3d.top() + kPartsHeaderHeight), QPointF(l.area3d.right(), l.area3d.top() + kPartsHeaderHeight));
    f.setBold(false);
    p.setFont(f);

    const int perSheet = partsRowsPerSheet(l);
    const int first = sheetIndex * perSheet;
    const int last = qMin(first + perSheet, static_cast<int>(rows.size()));
    for (int i = first; i < last; ++i)
    {
        const qreal y = l.area3d.top() + kPartsHeaderHeight + (i - first) * kPartsRowHeight;
        const QString cells[4] = {QString::number(i + 1), rows[i].name, rows[i].material, QString::number(rows[i].quantity)};
        for (int c = 0; c < 4; ++c)
        {
            const QRectF r(xs[c] + 6, y, xs[c + 1] - xs[c] - 12, kPartsRowHeight);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(cells[c], Qt::ElideRight, static_cast<int>(r.width())));
        }
        p.setPen(QPen(QColor(200, 200, 200), 0.4));
        p.drawLine(QPointF(x0, y + kPartsRowHeight), QPointF(l.area3d.right(), y + kPartsRowHeight));
        p.setPen(QPen(Qt::black, 0.6));
    }
    for (int c = 1; c < 4; ++c)
        p.drawLine(QPointF(xs[c], l.area3d.top()), QPointF(xs[c], l.area3d.top() + kPartsHeaderHeight + (last - first) * kPartsRowHeight));
}

} // namespace

Pdf3DResult Pdf3DExporter::exportToFile(const U3dScene& scene, const Pdf3DOptions& options, const QString& path)
{
    Pdf3DResult result;

    QVector3D lo, hi;
    if (scene.isEmpty() || !scene.computeBounds(&lo, &hi))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "There is no visible geometry to export.");
        return result;
    }

    // 1. the 3D stream
    // an animation that must not start by itself is written paused (the modifiers' playing flag), whatever the PDF's /AIS says
    U3dScene writeScene = scene;
    writeScene.animation.playing = options.animationAutoStart;
    const U3dWriteResult u3d = U3dWriter::write(writeScene);
    if (!u3d.ok)
    {
        result.error = u3d.error;
        return result;
    }

    // 2. the 2D sheet
    QByteArray pdf;
    Layout layout;
    {
        QBuffer buffer(&pdf);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setPdfVersion(QPagedPaintDevice::PdfVersion_1_6);   // 3D annotations are a PDF 1.6 feature
        layout.tpl = findSheetTemplate(options.templateId);
        const SheetTemplate& tpl = layout.tpl;
        if (tpl.pageSizeId == QPageSize::Custom)
        {
            // a custom size is defined in portrait and turned by the orientation
            const QSizeF mm = tpl.customSizeMm;
            writer.setPageSize(QPageSize(QSizeF(qMin(mm.width(), mm.height()), qMax(mm.width(), mm.height())), QPageSize::Millimeter));
        }
        else
        {
            writer.setPageSize(QPageSize(tpl.pageSizeId));
        }
        writer.setPageOrientation(tpl.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        writer.setResolution(72);   // painter units are PDF points
        writer.setTitle(options.title);
        writer.setCreator(QStringLiteral("ModelViewer"));

        const QSizeF pagePt = writer.pageLayout().fullRect(QPageLayout::Point).size();
        layout.pageHeightPt = pagePt.height();
        layout.page = QRectF(QPointF(0, 0), pagePt);

        QPainter painter(&writer);
        if (!painter.isActive())
        {
            result.error = QCoreApplication::translate("Pdf3DExporter", "The PDF page could not be created.");
            return result;
        }

        int partCount = 0;
        for (const U3dNode& n : scene.nodes)
            partCount += n.overlay ? 0 : 1;
        const SheetLayout sheetLayout = layoutSheet(painter, tpl, pagePt, sheetValues(options, partCount, 1, 1));
        layout.sheet = sheetLayout.sheet;
        layout.area3d = sheetLayout.sheet.area3d;
        layout.notesScroll = sheetLayout.scroll;
        layout.scrollRect = sheetLayout.scrollRect;
        layout.scrollText = sheetLayout.scrollText;
        layout.scrollFontSize = sheetLayout.scrollFontSize;
        // the button strip, when there is something to switch: more than one view, or an animation
        int viewCount = static_cast<int>(options.extraViews.size());
        for (const QString& n : standardViewNames())
            viewCount += options.excludedStandardViews.contains(n) ? 0 : 1;
        if (options.viewButtons && (viewCount > 1 || scene.hasAnimation()))
        {
            layout.buttonStrip = QRectF(layout.area3d.left(), layout.area3d.bottom() - kButtonStripHeight, layout.area3d.width(), kButtonStripHeight);
            layout.area3d.setBottom(layout.buttonStrip.top() - kButtonStripGap);
        }
        const QVector<PartRow> rows = options.includePartsList ? buildPartRows(scene, options.subtitle) : QVector<PartRow>();
        const int perSheet = partsRowsPerSheet(layout);
        const int listSheets = rows.isEmpty() ? 0 : (static_cast<int>(rows.size()) + perSheet - 1) / perSheet;
        const int sheetCount = 1 + listSheets;
        drawModelSheet(painter, layout, options, partCount, sheetCount);
        for (int i = 0; i < listSheets; ++i)
        {
            writer.newPage();
            drawPartsSheet(painter, layout, options, rows, partCount, i, sheetCount);
        }
        painter.end();
    }

    // 3. the annotation, appended as an incremental update
    const QVector3D center = (lo + hi) * 0.5f;
    const float radius = 0.5f * (hi - lo).length();

    Annotation3D annotation;
    annotation.rect = toPdfRect(layout.area3d, layout.pageHeightPt);
    annotation.u3d = u3d.data;
    QStringList viewNames;   // parallel to annotation.viewDictionaries; the captions of the view buttons

    // A view only changes the parts it lists, and Acrobat keeps the previous view's changes (an explosion, hidden parts) for the
    // others. So every view lists every node that ANY view changes, with the default state (visible, assembled transform) where it
    // has no override of its own; nodes nothing changes are not listed. Parts: the overrides the view brings. Measurement and
    // annotation nodes (overlay): shown or hidden per view, and carried along with the part they are attached to when a view moves
    // it (an item attached to several parts that move differently cannot follow all of them and is hidden in that view).
    QHash<QString, QMatrix4x4> worldByName;
    for (const U3dNode& node : scene.nodes)
        worldByName.insert(node.name, node.world);

    const auto nearlyEqual = [](const QMatrix4x4& a, const QMatrix4x4& b) {
        const float* pa = a.constData();
        const float* pb = b.constData();
        for (int i = 0; i < 16; ++i)
        {
            if (std::fabs(pa[i] - pb[i]) > 1.0e-4f)
                return false;
        }
        return true;
    };

    const auto overlayStates = [&](const QVector<NodeVisibility>& partOverrides, bool showOverlays) {
        QHash<QString, int> ownIndex;
        for (int i = 0; i < partOverrides.size(); ++i)
            ownIndex.insert(partOverrides[i].node, i);
        QVector<NodeVisibility> out;
        for (const U3dNode& n : scene.nodes)
        {
            if (!n.overlay)
                continue;
            NodeVisibility s;
            s.node = n.name;
            s.visible = showOverlays;
            s.matrix = n.world;
            if (showOverlays && !n.follows.isEmpty())
            {
                QMatrix4x4 common;
                bool have = false;
                bool consistent = true;
                for (const QString& part : n.follows)
                {
                    QMatrix4x4 pose;   // how this view moves the part from its exported place (identity when it leaves it)
                    const int found = ownIndex.value(part, -1);
                    if (found >= 0 && partOverrides[found].hasMatrix)
                    {
                        bool invertible = false;
                        const QMatrix4x4 worldInverse = worldByName.value(part).inverted(&invertible);
                        if (invertible)
                            pose = partOverrides[found].matrix * worldInverse;
                    }
                    if (!have)
                    {
                        common = pose;
                        have = true;
                    }
                    else if (!nearlyEqual(common, pose))
                    {
                        consistent = false;
                        break;
                    }
                }
                if (!consistent)
                    s.visible = false;
                else if (!nearlyEqual(common, QMatrix4x4()))
                {
                    s.hasMatrix = true;
                    s.matrix = common * n.world;
                }
            }
            out.append(s);
        }
        return out;
    };

    const QStringList standardNames = standardViewNames();
    QVector<NodeVisibility> defaultNodes;   // in scene order
    {
        QSet<QString> touched, withMatrix;
        const auto note = [&](const QVector<NodeVisibility>& states) {
            for (const NodeVisibility& n : states)
            {
                if (!n.visible || n.hasMatrix)
                    touched.insert(n.node);
                if (n.hasMatrix)
                    withMatrix.insert(n.node);
            }
        };
        for (const CameraViewSpec& spec : options.extraViews)
        {
            note(spec.nodes);
            note(overlayStates(spec.nodes, spec.showOverlays));
        }
        for (const QString& name : standardNames)
        {
            if (!options.excludedStandardViews.contains(name))
                note(overlayStates(QVector<NodeVisibility>(), !options.standardViewsWithoutOverlays.contains(name)));
        }
        for (const U3dNode& node : scene.nodes)
        {
            if (!touched.contains(node.name))
                continue;
            NodeVisibility d;
            d.node = node.name;
            d.visible = true;
            d.hasMatrix = withMatrix.contains(node.name);
            d.matrix = node.world;
            defaultNodes.append(d);
        }
    }
    const auto completeNodes = [&](const QVector<NodeVisibility>& ownParts, bool showOverlays) {
        QVector<NodeVisibility> own = ownParts;
        own += overlayStates(ownParts, showOverlays);
        QHash<QString, int> index;
        for (int i = 0; i < own.size(); ++i)
            index.insert(own[i].node, i);
        QVector<NodeVisibility> out;
        out.reserve(defaultNodes.size());
        for (const NodeVisibility& d : defaultNodes)
        {
            const int found = index.value(d.node, -1);
            if (found < 0)
            {
                out.append(d);
                continue;
            }
            NodeVisibility e = own[found];
            if (d.hasMatrix && !e.hasMatrix)
            {
                e.hasMatrix = true;
                e.matrix = d.matrix;
            }
            out.append(e);
        }
        return out;
    };
    {
        // fitted to the bounding box and the shape of the 3D area (falls back to the sphere fit for a degenerate area)
        const float aspect = layout.area3d.height() > 0.0 ? static_cast<float>(layout.area3d.width() / layout.area3d.height()) : 1.0f;
        const auto build = [&](bool showOverlays) {
            const QByteArray nodesBytes = makeNodeOverrides(completeNodes(QVector<NodeVisibility>(), showOverlays));
            QList<QByteArray> views = standardViewsFitted(lo, hi, aspect, options.upAxis, nodesBytes);
            if (views.isEmpty())
                views = standardViews(center, radius, options.upAxis, nodesBytes);
            return views;
        };
        const QList<QByteArray> standardShown = build(true);
        const QList<QByteArray> standardHidden = options.standardViewsWithoutOverlays.isEmpty() ? standardShown : build(false);
        for (int i = 0; i < standardShown.size() && i < standardNames.size(); ++i)
        {
            if (options.excludedStandardViews.contains(standardNames[i]))
                continue;
            annotation.viewDictionaries.append(options.standardViewsWithoutOverlays.contains(standardNames[i]) ? standardHidden[i] : standardShown[i]);
            viewNames.append(standardNames[i]);
        }
    }
    {
        // saved views follow the standard ones; names are made unique ("name (2)") so the view list stays unambiguous
        QSet<QString> used;
        for (const QString& n : standardNames)
            used.insert(n);
        for (CameraViewSpec spec : options.extraViews)
        {
            const QString base = spec.name.isEmpty() ? QCoreApplication::translate("Pdf3DExporter", "View") : spec.name;
            QString unique = base;
            for (int i = 2; used.contains(unique); ++i)
                unique = QStringLiteral("%1 (%2)").arg(base).arg(i);
            used.insert(unique);
            spec.name = unique;
            spec.nodes = completeNodes(spec.nodes, spec.showOverlays);
            annotation.viewDictionaries.append(makeCameraView(spec, center, radius));
            viewNames.append(unique);
        }
    }
    annotation.defaultView = 0;
    annotation.description = QStringLiteral("3D model");
    if (!layout.buttonStrip.isEmpty())
        annotation.buttons = makeButtons(viewNames, scene.hasAnimation(), layout.buttonStrip, layout.pageHeightPt);
    if (scene.hasAnimation())
    {
        const char* style = "Linear";
        int playCount = -1;
        if (options.animationPlayback == AnimationPlayback::BackAndForth)
            style = "Oscillating";
        else if (options.animationPlayback == AnimationPlayback::Once)
            playCount = 1;
        annotation.animationStyle = "<< /Type /3DAnimationStyle /Subtype /" + QByteArray(style) + " /PC " + QByteArray::number(playCount) + " /TM 1 >>";
        annotation.startLive = options.animationAutoStart;
        annotation.activateOnClick = !options.animationAutoStart;   // Acrobat starts an animation as soon as the area is activated
    }
    if (layout.notesScroll)
    {
        annotation.hasNotesField = true;
        annotation.notesField.rect = toPdfRect(layout.scrollRect, layout.pageHeightPt);
        annotation.notesField.text = layout.scrollText;
        annotation.notesField.fontSize = layout.scrollFontSize;
    }

    if (annotation.viewDictionaries.isEmpty())
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "No view is selected: the PDF needs at least one.");
        return result;
    }

    QString error;
    if (!PdfAppender::append3DAnnotation(pdf, annotation, &error))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "The 3D annotation could not be added to the PDF: %1").arg(error);
        return result;
    }

    // 4. write the file
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.write(pdf) != pdf.size() || !file.commit())
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace Pdf3D
