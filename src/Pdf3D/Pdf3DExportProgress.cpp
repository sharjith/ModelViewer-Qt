#include "Pdf3DExportProgress.h"

#include "U3dWorker.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>
#include <exception>
#include <memory>

namespace Pdf3D
{
namespace
{

class ExportProgressDialog : public QDialog
{
    Q_DECLARE_TR_FUNCTIONS(Pdf3DExportProgress)

public:
    explicit ExportProgressDialog(QWidget* parent) : QDialog(parent)
    {
        setWindowTitle(tr("Save as 3D PDF"));
        setWindowModality(Qt::ApplicationModal);
        setMinimumWidth(380);

        auto* layout = new QVBoxLayout(this);
        _label = new QLabel(tr("Preparing the export..."), this);
        _label->setWordWrap(true);
        layout->addWidget(_label);

        auto* progress = new QProgressBar(this);
        progress->setRange(0, 0);
        layout->addWidget(progress);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        _cancel = buttons->button(QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::rejected, this, &ExportProgressDialog::reject);
        layout->addWidget(buttons);
    }

    void reject() override
    {
        State expected = State::Running;
        if (_state.compare_exchange_strong(expected, State::Canceled))
        {
            _cancel->setEnabled(false);
            _label->setText(tr("Canceling the export..."));
        }
    }

    bool wasCanceled() const { return _state.load() == State::Canceled; }

    // Called from the exporter thread. Starting the commit and accepting cancellation are mutually exclusive.
    bool beginStage(ExportStage stage)
    {
        if (stage == ExportStage::CommitFile)
        {
            State expected = State::Running;
            if (!_state.compare_exchange_strong(expected, State::Committing))
                return false;
        }
        else if (wasCanceled())
        {
            return false;
        }

        QMetaObject::invokeMethod(this, [this, stage] {
            if (!wasCanceled())
                showStage(stage);
        }, Qt::QueuedConnection);
        return true;
    }

private:
    enum class State { Running, Canceled, Committing };

    void showStage(ExportStage stage)
    {
        switch (stage)
        {
        case ExportStage::EncodeModel:
            _label->setText(tr("Encoding the interactive model..."));
            break;
        case ExportStage::ComposeSheets:
            _label->setText(tr("Drawing the PDF sheets..."));
            break;
        case ExportStage::EmbedModel:
            _label->setText(tr("Adding views and interactive content..."));
            break;
        case ExportStage::WriteFile:
            _label->setText(tr("Writing the PDF..."));
            break;
        case ExportStage::CommitFile:
            _cancel->setEnabled(false);
            _label->setText(tr("Finalizing the PDF..."));
            break;
        }
    }

    std::atomic<State> _state{State::Running};
    QLabel* _label = nullptr;
    QPushButton* _cancel = nullptr;
};

} // namespace

Pdf3DResult exportWithProgress(QWidget* parent, const U3dScene& scene, const Pdf3DOptions& options, const QString& path)
{
    ExportProgressDialog dialog(parent);
    Pdf3DResult result;
    const QString executable = QCoreApplication::applicationFilePath();
    const auto isCanceled = [&dialog] { return dialog.wasCanceled(); };
    const auto save = [&] {
        try
        {
            const ExportControl control{
                [&dialog](ExportStage stage) { return dialog.beginStage(stage); },
                [&](const U3dScene& model) { return encodeU3dInWorker(executable, model, isCanceled); },
                isCanceled
            };
            result = Pdf3DExporter::exportToFile(scene, options, path, control);
        }
        catch (const std::exception& error)
        {
            result.error = QCoreApplication::translate("Pdf3DExportProgress", "Export failed: %1")
                .arg(QString::fromLocal8Bit(error.what()));
        }
        catch (...)
        {
            result.error = QCoreApplication::translate("Pdf3DExportProgress", "The export failed with an unexpected error.");
        }
    };
    const std::unique_ptr<QThread> worker(QThread::create(save));
    QObject::connect(worker.get(), &QThread::finished, &dialog, &QDialog::accept, Qt::QueuedConnection);
    worker->start();
    if (dialog.exec() != QDialog::Accepted)
        dialog.reject();
    worker->wait();

    if (dialog.wasCanceled())
    {
        result = {};
        result.canceled = true;
    }
    return result;
}

} // namespace Pdf3D
