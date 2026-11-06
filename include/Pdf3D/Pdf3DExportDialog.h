#pragma once

#include <QDialog>

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
// Modal "Save as 3D PDF" options dialog: the title block fields (title, drawing number, revision, author, company, scale,
// notes) and whether to add a parts list sheet. Everything except the title and the notes is remembered between uses
// (QSettings "pdf3d/..."); the title defaults to the document name each time. The dialog also remembers its geometry.
// ---------------------------------------------------------------------------
class Pdf3DExportDialog : public QDialog
{
	Q_OBJECT

public:
	explicit Pdf3DExportDialog(const QString& defaultTitle, QWidget* parent = nullptr);
	~Pdf3DExportDialog() override;

	// Copies the dialog's values into the title block fields of `options` (title, drawing number, revision, author, company,
	// scale, notes, includePartsList). The other options are left untouched.
	void applyTo(Pdf3D::Pdf3DOptions& options) const;

protected:
	void done(int result) override;   // saves the remembered fields and the geometry on every way out

private:
	std::unique_ptr<Ui::Pdf3DExportDialog> ui;
};
