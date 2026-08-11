#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QTextBrowser;
class QPushButton;
QT_END_NAMESPACE

// Shows app/library version info and the GPU info MainWindow already
// gathers at startup (see MainWindow::graphicsInfo()) - replaces the old
// plain QMessageBox::about() call, which had no room for the splash image
// or a properly formatted version/library breakdown.
class AboutDialog : public QDialog
{
	Q_OBJECT

public:
	// graphicsInfo: the same GPU Renderer/Vendor/OpenGL/Shader Version
	// text MainWindow::graphicsInfo() already returns - passed in rather
	// than re-gathered here, since that requires a current GL context.
	explicit AboutDialog(const QString& graphicsInfo, QWidget* parent = nullptr);
	~AboutDialog() override = default;

private:
	void setupUI(const QString& graphicsInfo);
	QString buildDetailsHtml(const QString& graphicsInfo) const;

	QLabel* _logoLabel = nullptr;
	QTextBrowser* _detailsBrowser = nullptr;
	QPushButton* _closeButton = nullptr;
};
