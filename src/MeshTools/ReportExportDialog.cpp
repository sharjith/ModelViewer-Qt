#include "ReportExportDialog.h"
#include "DialogLayoutHelpers.h"
#include "ui_ReportExportDialog.h"

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
    , ui(std::make_unique<Ui::ReportExportDialog>())
{
    ui->setupUi(this);
    DialogLayout::keepNaturalHeight(ui->viewsHintLabel);
    DialogLayout::makeCentredHint(ui->noViewsLabel);
    ui->verticalLayout->setStretchFactor(ui->noViewsLabel, 1);

    SceneGraph* sceneGraph = _modelViewer ? _modelViewer->sceneGraph() : nullptr;
    const QVector<GltfCameraEntry> capturedViews = sceneGraph
        ? sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras
        : QVector<GltfCameraEntry>();
    for (const GltfCameraEntry& entry : capturedViews)
    {
        QListWidgetItem* item = new QListWidgetItem(entry.name, ui->viewsList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(tr("Double-click to customize which measurements/annotations this view shows"));
    }

    ui->noViewsLabel->setVisible(capturedViews.isEmpty());
    ui->viewsList->setVisible(!capturedViews.isEmpty());
    ui->viewsHintLabel->setVisible(!capturedViews.isEmpty());

    const QString defaultTitle = sceneGraph && _modelViewer
        ? QFileInfo(_modelViewer->currentFile()).completeBaseName()
        : QString();
    ui->titleEdit->setText(defaultTitle.isEmpty() ? tr("Report") : defaultTitle);

    connect(ui->viewsList, &QListWidget::itemChanged, this, &ReportExportDialog::updateExportEnabled);
    connect(ui->viewsList, &QListWidget::itemDoubleClicked, this, &ReportExportDialog::editViewVisibility);
    connect(ui->includeTableCheck, &QCheckBox::toggled, this, &ReportExportDialog::updateExportEnabled);
    connect(ui->exportButton, &QPushButton::clicked, this, &ReportExportDialog::onExportClicked);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    updateExportEnabled();
}

ReportExportDialog::~ReportExportDialog()
{
}

void ReportExportDialog::updateExportEnabled()
{
    bool anyViewChecked = false;
    for (int i = 0; i < ui->viewsList->count(); ++i)
    {
        if (ui->viewsList->item(i)->checkState() == Qt::Checked)
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
    ui->exportButton->setEnabled(anyViewChecked || (ui->includeTableCheck->isChecked() && hasTableContent));
}

void ReportExportDialog::editViewVisibility(QListWidgetItem* item)
{
    if (!_modelViewer || !item)
        return;

    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph || !viewport)
        return;

    const int index = ui->viewsList->row(item);
    const auto capturedViews = sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras;
    if (index < 0 || index >= capturedViews.size())
        return;

    std::optional<ViewOverlayVisibility> overrideVisibility;
    const auto existing = _perViewOverrides.constFind(index);
    if (existing != _perViewOverrides.constEnd())
        overrideVisibility = *existing;
    if (!editOverlayVisibility(*_modelViewer, this, capturedViews.at(index).overlayVisibility, overrideVisibility))
        return;
    if (overrideVisibility)
        _perViewOverrides.insert(index, *overrideVisibility);
    else
        _perViewOverrides.remove(index);

    const QString baseName = capturedViews.at(index).name;
    item->setText(overrideVisibility ? baseName + tr(" (custom visibility)") : baseName);
}

bool ReportExportDialog::editOverlayVisibility(ModelViewer& viewer, QWidget* parent,
    const std::optional<ViewOverlayVisibility>& captured,
    std::optional<ViewOverlayVisibility>& overrideVisibility)
{
    const SceneGraph* sceneGraph = viewer.sceneGraph();
    auto* viewport = viewer.getViewportWidget();
    if (!sceneGraph || !viewport)
        return false;

    const auto& visibility = overrideVisibility ? overrideVisibility : captured;
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Measurement and Annotation Visibility"));

    QListWidget* measurementsList = new QListWidget(&dialog);
    QVector<QUuid> measurementIds;
    for (const Measurement& m : sceneGraph->measurements())
    {
        QListWidgetItem* row = new QListWidgetItem(viewport->measurementSummaryText(m), measurementsList);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        const bool checked = visibility ? visibility->measurements.contains(m.id) : m.visible;
        row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        measurementIds.append(m.id);
    }

    QListWidget* annotationsList = new QListWidget(&dialog);
    QVector<QUuid> annotationIds;
    for (const Annotation& a : sceneGraph->annotations())
    {
        QListWidgetItem* row = new QListWidgetItem(annotationRowLabel(a.text), annotationsList);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        const bool checked = visibility ? visibility->annotations.contains(a.id) : a.visible;
        row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        annotationIds.append(a.id);
    }

    QPushButton* useCurrentButton = new QPushButton(captured ? tr("Use Captured Visibility")
                                                                   : tr("Use Current Visibility"), &dialog);
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

    // Clear the report override and return to the view's default visibility.
    bool useCurrentClicked = false;
    connect(useCurrentButton, &QPushButton::clicked, &dialog, [&dialog, &useCurrentClicked]() {
        useCurrentClicked = true;
        dialog.accept();
        });

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (useCurrentClicked)
    {
        overrideVisibility.reset();
        return true;
    }

    ViewOverlayVisibility selected;
    for (int i = 0; i < measurementsList->count(); ++i)
    {
        if (measurementsList->item(i)->checkState() == Qt::Checked)
            selected.measurements.insert(measurementIds.at(i));
    }
    for (int i = 0; i < annotationsList->count(); ++i)
    {
        if (annotationsList->item(i)->checkState() == Qt::Checked)
            selected.annotations.insert(annotationIds.at(i));
    }
    overrideVisibility = std::move(selected);
    return true;
}

void ReportExportDialog::onExportClicked()
{
    if (!_modelViewer)
        return;

    const QString defaultName = ui->titleEdit->text().isEmpty() ? tr("Report") : ui->titleEdit->text();
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

    const auto originalOverlays = viewport->captureCurrentCameraEntry(QString()).overlayVisibility;

    const QVector<GltfCameraEntry> capturedViews =
        sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras;
    for (int i = 0; i < ui->viewsList->count() && i < capturedViews.size(); ++i)
    {
        if (ui->viewsList->item(i)->checkState() != Qt::Checked)
            continue;

        const auto& view = capturedViews.at(i);
        viewport->activateGltfCamera(capturedViewsSourceFileKey(), i);

        // Report-specific overrides take precedence over the saved view.
        const auto overrideIt = _perViewOverrides.constFind(i);
        if (overrideIt != _perViewOverrides.constEnd())
            sceneGraph->applyViewOverlayVisibility(*overrideIt);
        else if (!view.overlayVisibility && originalOverlays)
            sceneGraph->applyViewOverlayVisibility(*originalOverlays);

        capturedImages.append({ capturedViews.at(i).name, viewport->captureCleanRayTracedImage() });
    }

    if (originalOverlays)
        sceneGraph->applyViewOverlayVisibility(*originalOverlays);

    // Restore whatever the user was actually looking at before generating
    // the report - don't leave them parked on the last exported view.
    viewport->resetToSystemCamera();

    // ---- Assemble the flowing HTML document --------------------------------
    QTextDocument doc;
    QString html = QStringLiteral("<h1>%1</h1>").arg(ui->titleEdit->text().toHtmlEscaped());

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

    if (ui->includeTableCheck->isChecked())
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
