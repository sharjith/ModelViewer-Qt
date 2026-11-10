#pragma once

#include "SheetTemplate.h"

#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace Ui
{
	class Pdf3DExportDialog;
}

namespace Pdf3D
{
	struct Pdf3DOptions;
	class SheetPreviewWidget;
}

// ---------------------------------------------------------------------------
// Pdf3DExportDialog
//
// Modal "Save as 3D PDF" options dialog, two tabs:
//  - Sheet: the sheet template (built-in or a JSON file of the user's, see SheetTemplate.h), the title block fields it uses
//    (title, drawing number, revision, author, company, scale, notes; fields the template does not use are hidden) plus the
//    template's own custom fields, and whether to add a parts list sheet. Everything except the title and the notes is remembered
//    between uses (QSettings "pdf3d/..."; custom fields per template, "pdf3d/templates/<id>/<field>").
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
		bool    overlays = true;     // show the measurements and annotations in this view (the checkbox in the third column)
	};

	// A measurement or annotation that can be written into the 3D model.
	struct OverlayItem
	{
		QString group;   // "Measurements" or "Annotations"
		QString name;
	};

	// An animation of the model that can be played in the PDF (only one can be chosen).
	struct AnimationItem
	{
		QString name;
		double  durationSeconds = 0.0;
	};

	Pdf3DExportDialog(const QString& defaultTitle, const QVector<ViewItem>& views, const QString& viewsInfo,
		const QVector<OverlayItem>& overlays, const QVector<AnimationItem>& animations, QWidget* parent = nullptr);
	~Pdf3DExportDialog() override;

	// Copies the sheet tab's values into `options`: the title block fields (title, drawing number, revision, author, company, scale,
	// notes, includePartsList), the chosen template id and the values of its custom fields. The other options are left untouched.
	void applyTo(Pdf3D::Pdf3DOptions& options) const;

	// What the sheet preview shows for the values the dialog does not ask for: the number of parts and the source file names (the
	// {parts} and {subtitle} placeholders).
	// The part number the model declares (NodeProperties::declaredPartNumber): it replaces the remembered drawing number, and the user may
	// change it. Does nothing for an empty string, so the remembered value stays.
	void setDeclaredDrawingNumber(const QString& number);
	void setSheetSample(int partCount, const QString& subtitle, const QString& material = QString(), const QString& mass = QString());

	// For each ViewItem passed to the constructor, in order: whether it is ticked.
	QVector<bool> viewSelection() const;

	// For each OverlayItem passed to the constructor, in order: whether it is ticked.
	QVector<bool> overlaySelection() const;

	// For each ViewItem, in order: whether the measurements and annotations are shown in that view.
	QVector<bool> viewOverlaySelection() const;

	// The chosen animation: an index into the AnimationItems passed to the constructor, or -1 for none; and how it plays.
	int  selectedAnimation() const;
	int  playbackIndex() const;        // 0 = loop, 1 = back and forth, 2 = once
	bool animationAutoStart() const;

protected:
	void done(int result) override;   // saves the remembered fields and the geometry on every way out

private:
	void updateOkEnabled();
	void setAllViewsChecked(bool checked);
	void setAllOverlaysChecked(bool checked);
	void reloadTemplates(const QString& selectId);   // rescans the templates, fills the combo and selects `selectId` (or the first)
	void applyTemplateSelection();                   // shows the fields the chosen template uses and builds its custom fields
	void templateNameEntered();                      // the combo's text was edited: select that template, or save a copy under the new name
	void chooseLogo();                               // the Logo button
	void editTemplate();                             // the Edit... button: the cell editor for the chosen template
	QMap<QString, QString> currentSheetValues() const;   // what the sheet would print now (the preview and the editor show it)
	void updatePreview();                            // redraws the sheet preview from the chosen template and the current values
	QString currentTemplateId() const;

	std::unique_ptr<Ui::Pdf3DExportDialog> ui;
	int _viewCount = 0;
	int _overlayCount = 0;
	QVector<Pdf3D::SheetTemplate> _templates;   // parallel to the combo
	QStringList _templateProblems;              // files of the user folder that could not be used, and warnings
	QMap<QString, QWidget*> _customFields;      // custom field id -> its editor (owned by the form layout)
	Pdf3D::SheetPreviewWidget* _preview = nullptr;   // below the fields, owned by the Sheet tab's layout
	int _sampleParts = 12;
	QString _sampleSubtitle;
	QString _sampleMaterial;   // what the exported parts declare ({material} and {mass} of the sheet), see PartData.h
	QString _sampleMass;
};
