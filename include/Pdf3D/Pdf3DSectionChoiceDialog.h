#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

#include <memory>

class QComboBox;

namespace Ui
{
	class Pdf3DSectionChoiceDialog;
}

// ---------------------------------------------------------------------------
// Pdf3DSectionChoiceDialog
//
// Shown by "Save as 3D PDF" when saved views (captured views, scene states) use more than one clipping plane: a PDF view can show one
// section plane only (verified in Acrobat), so for each such view the user picks which plane to export, or "each plane in its own
// view". Remembers its geometry.
// ---------------------------------------------------------------------------
class Pdf3DSectionChoiceDialog : public QDialog
{
	Q_OBJECT

public:
	struct Item
	{
		QString     viewName;
		QStringList planes;   // one description per clipping plane, e.g. "X = 12.5 (keep +X side)"
	};

	explicit Pdf3DSectionChoiceDialog(const QVector<Item>& items, QWidget* parent = nullptr);
	~Pdf3DSectionChoiceDialog() override;

	// For each item, in order: the index of the plane to export, or -1 for every plane in a view of its own.
	QVector<int> choices() const;

protected:
	void done(int result) override;   // saves the geometry on every way out

private:
	std::unique_ptr<Ui::Pdf3DSectionChoiceDialog> ui;
	QVector<QComboBox*> _combos;
};
