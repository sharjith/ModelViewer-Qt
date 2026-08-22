#include "ReportExportDialog.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPushButton>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    // Same first-line/60-char-truncation/"(empty note)" convention
    // AnnotationDialog::refreshResultsList() uses for its own row labels -
    // duplicated rather than shared, matching this codebase's established
    // per-dialog-local-list convention (MeasurementResultsList/
    // AnnotationResultsList are separate copies too).
    QString annotationRowLabel(const QString& text)
    {
        QString firstLine = text.section(QChar('\n'), 0, 0);
        constexpr int kMaxLabelChars = 60;
        if (firstLine.size() > kMaxLabelChars)
            firstLine = firstLine.left(kMaxLabelChars) + QStringLiteral("...");
        if (firstLine.isEmpty())
            firstLine = QObject::tr("(empty note)");
        return firstLine;
    }
}

ReportExportDialog::ReportExportDialog(ModelViewer* modelViewer, QWidget* parent)
    : QDialog(parent)
    , _modelViewer(modelViewer)
{
    setWindowTitle(tr("Export Report"));

    _viewsList = new QListWidget(this);
    SceneGraph* sceneGraph = _modelViewer ? _modelViewer->sceneGraph() : nullptr;
    const QVector<GltfCameraEntry> capturedViews = sceneGraph
        ? sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras
        : QVector<GltfCameraEntry>();
    for (const GltfCameraEntry& entry : capturedViews)
    {
        QListWidgetItem* item = new QListWidgetItem(entry.name, _viewsList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(tr("Double-click to customize which measurements/annotations this view shows"));
    }

    _noViewsLabel = new QLabel(tr("No captured views yet - use the Cameras tab's \"Capture View\" first."), this);
    _noViewsLabel->setWordWrap(true);
    _noViewsLabel->setVisible(capturedViews.isEmpty());
    _viewsList->setVisible(!capturedViews.isEmpty());

    // Persistent hint, not just a per-row tooltip - the tooltip alone
    // requires already knowing to hover, which defeats the point of a
    // discoverability hint.
    QLabel* viewsHintLabel = new QLabel(
        tr("Double-click a view to customize which measurements/annotations it shows"), this);
    viewsHintLabel->setWordWrap(true);
    viewsHintLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    viewsHintLabel->setVisible(!capturedViews.isEmpty());

    _includeTableCheck = new QCheckBox(tr("Include measurement/annotation table"), this);
    _includeTableCheck->setChecked(true);

    const QString defaultTitle = sceneGraph && _modelViewer
        ? QFileInfo(_modelViewer->currentFile()).completeBaseName()
        : QString();
    _titleEdit = new QLineEdit(defaultTitle.isEmpty() ? tr("Report") : defaultTitle, this);

    _exportButton = new QPushButton(tr("Export..."), this);
    _cancelButton = new QPushButton(tr("Cancel"), this);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(_cancelButton);
    buttonRow->addWidget(_exportButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Title:"), this));
    layout->addWidget(_titleEdit);
    layout->addWidget(new QLabel(tr("Captured views to include:"), this));
    layout->addWidget(viewsHintLabel);
    layout->addWidget(_noViewsLabel);
    layout->addWidget(_viewsList, 1);
    layout->addWidget(_includeTableCheck);
    layout->addLayout(buttonRow);
    resize(320, 400);

    connect(_viewsList, &QListWidget::itemChanged, this, &ReportExportDialog::updateExportEnabled);
    connect(_viewsList, &QListWidget::itemDoubleClicked, this, &ReportExportDialog::editViewVisibility);
    connect(_includeTableCheck, &QCheckBox::toggled, this, &ReportExportDialog::updateExportEnabled);
    connect(_exportButton, &QPushButton::clicked, this, &ReportExportDialog::onExportClicked);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    updateExportEnabled();
}

void ReportExportDialog::updateExportEnabled()
{
    bool anyViewChecked = false;
    for (int i = 0; i < _viewsList->count(); ++i)
    {
        if (_viewsList->item(i)->checkState() == Qt::Checked)
        {
            anyViewChecked = true;
            break;
        }
    }

    SceneGraph* sceneGraph = _modelViewer ? _modelViewer->sceneGraph() : nullptr;
    const bool hasTableContent = sceneGraph
        && (!sceneGraph->measurements().isEmpty() || !sceneGraph->annotations().isEmpty());

    // Nothing silently produces an empty PDF - either at least one view is
    // going in, or the table is on AND has something in it.
    _exportButton->setEnabled(anyViewChecked || (_includeTableCheck->isChecked() && hasTableContent));
}

void ReportExportDialog::editViewVisibility(QListWidgetItem* item)
{
    if (!_modelViewer || !item)
        return;

    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph || !viewport)
        return;

    const int index = _viewsList->row(item);
    if (index < 0)
        return;

    const bool hadOverride = _perViewOverrides.contains(index);
    const ViewVisibilityOverride existing = _perViewOverrides.value(index);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("View Visibility"));

    // Two checklists (not a shared combined one) - simplest layout for two
    // unrelated id spaces, same "one dialog, two plain QListWidgets" weight
    // as the rest of this dialog's own controls.
    QListWidget* measurementsList = new QListWidget(&dialog);
    QVector<QUuid> measurementIds;
    for (const Measurement& m : sceneGraph->measurements())
    {
        QListWidgetItem* row = new QListWidgetItem(viewport->measurementSummaryText(m), measurementsList);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        const bool checked = hadOverride ? existing.visibleMeasurementIds.contains(m.id) : m.visible;
        row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        measurementIds.append(m.id);
    }

    QListWidget* annotationsList = new QListWidget(&dialog);
    QVector<QUuid> annotationIds;
    for (const Annotation& a : sceneGraph->annotations())
    {
        QListWidgetItem* row = new QListWidgetItem(annotationRowLabel(a.text), annotationsList);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        const bool checked = hadOverride ? existing.visibleAnnotationIds.contains(a.id) : a.visible;
        row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        annotationIds.append(a.id);
    }

    QPushButton* useCurrentButton = new QPushButton(tr("Use Current Visibility"), &dialog);
    QPushButton* okButton = new QPushButton(tr("OK"), &dialog);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), &dialog);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(useCurrentButton);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(okButton);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Measurements shown in this view:"), &dialog));
    layout->addWidget(measurementsList, 1);
    layout->addWidget(new QLabel(tr("Annotations shown in this view:"), &dialog));
    layout->addWidget(annotationsList, 1);
    layout->addLayout(buttonRow);
    dialog.resize(320, 420);

    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    // "Use Current Visibility" both clears any existing override AND closes
    // the dialog as an accept - a deliberate, immediate action rather than
    // "check the boxes to match current state yourself then hit OK".
    bool useCurrentClicked = false;
    connect(useCurrentButton, &QPushButton::clicked, &dialog, [&dialog, &useCurrentClicked]() {
        useCurrentClicked = true;
        dialog.accept();
        });

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (useCurrentClicked)
    {
        _perViewOverrides.remove(index);
    }
    else
    {
        ViewVisibilityOverride ov;
        for (int i = 0; i < measurementsList->count(); ++i)
        {
            if (measurementsList->item(i)->checkState() == Qt::Checked)
                ov.visibleMeasurementIds.insert(measurementIds.at(i));
        }
        for (int i = 0; i < annotationsList->count(); ++i)
        {
            if (annotationsList->item(i)->checkState() == Qt::Checked)
                ov.visibleAnnotationIds.insert(annotationIds.at(i));
        }
        _perViewOverrides.insert(index, ov);
    }

    const QVector<GltfCameraEntry> capturedViews =
        sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras;
    const QString baseName = index < capturedViews.size() ? capturedViews.at(index).name : item->text();
    item->setText(_perViewOverrides.contains(index) ? baseName + tr(" (custom visibility)") : baseName);
}

void ReportExportDialog::onExportClicked()
{
    if (!_modelViewer)
        return;

    const QString defaultName = _titleEdit->text().isEmpty() ? tr("Report") : _titleEdit->text();
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Report"),
        defaultName + QStringLiteral(".pdf"), tr("PDF Files (*.pdf)"));
    if (path.isEmpty())
        return;  // cancelled - no partial file written

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = generateReport(path);
    QGuiApplication::restoreOverrideCursor();

    if (!ok)
    {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write the report PDF. See the log for details."));
        return;
    }

    QMessageBox::information(this, tr("Export Report"), tr("Report exported to:\n%1").arg(path));
    accept();
}

bool ReportExportDialog::generateReport(const QString& path)
{
    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph || !viewport)
        return false;

    // ---- Capture each checked view -----------------------------------------
    struct CapturedView { QString name; QImage image; };
    QVector<CapturedView> capturedImages;

    // Snapshot the document's real visibility state once, before any
    // per-view override (see editViewVisibility()) is applied below -
    // restored unconditionally once every view has been captured, so
    // generating a report never leaves this attribute changed from what it
    // was before Export was clicked.
    QHash<QUuid, bool> originalMeasurementVisibility;
    for (const Measurement& m : sceneGraph->measurements())
        originalMeasurementVisibility.insert(m.id, m.visible);
    QHash<QUuid, bool> originalAnnotationVisibility;
    for (const Annotation& a : sceneGraph->annotations())
        originalAnnotationVisibility.insert(a.id, a.visible);

    const QVector<GltfCameraEntry> capturedViews =
        sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras;
    for (int i = 0; i < _viewsList->count() && i < capturedViews.size(); ++i)
    {
        if (_viewsList->item(i)->checkState() != Qt::Checked)
            continue;

        // A view with a custom override shows exactly its chosen subset for
        // this one capture; a view with none is left exactly as-is, so it
        // captures whatever's currently globally visible - unchanged
        // behavior for anyone who never opens the per-view editor.
        const auto overrideIt = _perViewOverrides.constFind(i);
        if (overrideIt != _perViewOverrides.constEnd())
        {
            for (auto vis = originalMeasurementVisibility.constBegin(); vis != originalMeasurementVisibility.constEnd(); ++vis)
                sceneGraph->setMeasurementVisible(vis.key(), overrideIt->visibleMeasurementIds.contains(vis.key()));
            for (auto vis = originalAnnotationVisibility.constBegin(); vis != originalAnnotationVisibility.constEnd(); ++vis)
                sceneGraph->setAnnotationVisible(vis.key(), overrideIt->visibleAnnotationIds.contains(vis.key()));
        }

        // Synchronous - activateGltfCamera() repositions the camera and
        // requests a repaint immediately for the captured-views bucket (no
        // animation clip is ever active for it), and captureCleanRayTracedImage()
        // forces its own fresh paint while grabbing, so no extra event-loop
        // spin is needed between the two calls. Measurement/annotation
        // overlays render into the same framebuffer during the normal paint
        // path, so they come along in the screenshot for free.
        viewport->activateGltfCamera(capturedViewsSourceFileKey(), i);
        capturedImages.append({ capturedViews.at(i).name, viewport->captureCleanRayTracedImage() });
    }

    // Restore the document's real visibility state, whether or not any
    // per-view override was applied above.
    for (auto vis = originalMeasurementVisibility.constBegin(); vis != originalMeasurementVisibility.constEnd(); ++vis)
        sceneGraph->setMeasurementVisible(vis.key(), vis.value());
    for (auto vis = originalAnnotationVisibility.constBegin(); vis != originalAnnotationVisibility.constEnd(); ++vis)
        sceneGraph->setAnnotationVisible(vis.key(), vis.value());

    // Restore whatever the user was actually looking at before generating
    // the report - don't leave them parked on the last exported view.
    viewport->resetToSystemCamera();

    // ---- Assemble the flowing HTML document --------------------------------
    QTextDocument doc;
    QString html = QStringLiteral("<h1>%1</h1>").arg(_titleEdit->text().toHtmlEscaped());

    for (int i = 0; i < capturedImages.size(); ++i)
    {
        const CapturedView& cv = capturedImages.at(i);
        const QUrl imageUrl(QStringLiteral("view%1").arg(i));
        doc.addResource(QTextDocument::ImageResource, imageUrl, cv.image);
        // page-break-inside: avoid keeps the image and its own caption as
        // one atomic block during QTextDocument's automatic pagination -
        // without it, a page break can land between an image and its
        // caption, which visually reads as the caption belonging to the
        // NEXT image instead (confirmed - that's exactly what happened
        // before this wrapper was added).
        // Trailing <br> after the block (not inside the avoid-break div -
        // a blank line is fine to split onto the next page even when the
        // image+caption above it isn't) leaves a one-line gap before the
        // next view, so consecutive views aren't visually run together.
        html += QStringLiteral("<div style=\"page-break-inside: avoid;\"><img src=\"%1\" width=\"500\"><br><h3>%2</h3></div><br>")
            .arg(imageUrl.toString(), cv.name.toHtmlEscaped());
    }

    if (_includeTableCheck->isChecked())
    {
        const QVector<Measurement>& measurements = sceneGraph->measurements();
        if (!measurements.isEmpty())
        {
            html += QStringLiteral("<h2>Measurements</h2><table border=\"1\" cellpadding=\"4\" width=\"100%\">");
            for (const Measurement& m : measurements)
                html += QStringLiteral("<tr><td>%1</td></tr>").arg(viewport->measurementSummaryText(m).toHtmlEscaped());
            html += QStringLiteral("</table>");
        }

        const QVector<Annotation>& annotations = sceneGraph->annotations();
        if (!annotations.isEmpty())
        {
            html += QStringLiteral("<h2>Annotations</h2><table border=\"1\" cellpadding=\"4\" width=\"100%\">");
            for (const Annotation& a : annotations)
            {
                QString text = a.text.toHtmlEscaped();
                text.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
                html += QStringLiteral("<tr><td>%1</td></tr>").arg(text);
            }
            html += QStringLiteral("</table>");
        }
    }

    doc.setHtml(html);

    // ---- Paginate onto the PDF ----------------------------------------------
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(150);
    doc.setPageSize(QSizeF(writer.width(), writer.height()));
    doc.print(&writer);

    return true;
}
