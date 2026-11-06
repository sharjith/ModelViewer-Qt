#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include <memory>

namespace Ui
{
	class Pdf3DExportDialog;
}

namespace Pdf3D
{
	struct Pdf3DOptions;
}

// ---------------------------------------------------------------------------
// Pdf3DExportDialog
//
// Modal "Save as 3D PDF" options dialog, two tabs:
//  - Sheet: the title block fields (title, drawing number, revision, author, company, scale, notes) and whether to add a parts
//    list sheet. Everything except the title and the notes is remembered between uses (QSettings "pdf3d/...").
//  - Views: every view the document can put into the PDF's view list (standard views, saved camera views, cameras of the model
//    files, scene states, exploded views), grouped, with a checkbox each and an optional note per view. The caller builds the
//    list from the document's stored data, so nothing depends on a panel being open. A preview is planned.
// The dialog also remembers its geometry.
// ---------------------------------------------------------------------------
class Pdf3DExportDialog : public QDialog
{
	Q_OBJECT

public:
	struct ViewItem
	{
		QString group;               // top level heading, e.g. "Standard views"
		QString name;                // the view's name in the PDF
		QString note;                // optional remark shown in the second column
		bool    warning = false;     // the note is a limitation of this view (shown highlighted)
	};

	Pdf3DExportDialog(const QString& defaultTitle, const QVector<ViewItem>& views, const QString& viewsInfo, QWidget* parent = nullptr);
	~Pdf3DExportDialog() override;

	// Copies the sheet tab's values into the title block fields of `options` (title, drawing number, revision, author, company,
	// scale, notes, includePartsList). The other options are left untouched.
	void applyTo(Pdf3D::Pdf3DOptions& options) const;

	// For each ViewItem passed to the constructor, in order: whether it is ticked.
	QVector<bool> viewSelection() const;

protected:
	void done(int result) override;   // saves the remembered fields and the geometry on every way out

private:
	void updateOkEnabled();
	void setAllViewsChecked(bool checked);

	std::unique_ptr<Ui::Pdf3DExportDialog> ui;
	int _viewCount = 0;
};
