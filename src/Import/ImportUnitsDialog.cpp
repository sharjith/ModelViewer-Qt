#include "ImportUnitsDialog.h"
#include "DialogLayoutHelpers.h"
#include "ModelViewer.h"
#include "SceneNode.h"
#include "LengthUnits.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>

namespace
{
// Display order matches LengthUnits.h's own enum declaration order.
// LengthUnit::Unknown is deliberately excluded - a user picking a unit is
// always choosing a concrete, known one; "Unknown" is only ever a resolution
// state, never a selectable choice.
struct UnitChoice { LengthUnit unit; const char* label; };
const UnitChoice kUnitChoices[] = {
	{ LengthUnit::Millimeter, QT_TR_NOOP("Millimeter (mm)") },
	{ LengthUnit::Centimeter, QT_TR_NOOP("Centimeter (cm)") },
	{ LengthUnit::Meter,      QT_TR_NOOP("Meter (m)") },
	{ LengthUnit::Inch,       QT_TR_NOOP("Inch (in)") },
	{ LengthUnit::Foot,       QT_TR_NOOP("Foot (ft)") },
};
}

ImportUnitsDialog::ImportUnitsDialog(ModelViewer* modelViewer, SceneNode* fileNode, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, _fileNode(fileNode)
{
	setWindowTitle(tr("Import Units"));

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	const QString fileName = _fileNode ? _fileNode->name : QString();
	auto* introLabel = new QLabel(tr("Real-world length unit for \"%1\" - used to convert Mass Properties/"
	                                  "Surface Analysis results (volume, area, distances) into millimetres "
	                                  "internally. Only affects this one imported file.").arg(fileName), this);
	introLabel->setWordWrap(true);
	DialogLayout::keepNaturalHeight(introLabel);
	layout->addWidget(introLabel);

	_unitCombo = new QComboBox(this);
	for (const UnitChoice& choice : kUnitChoices)
		_unitCombo->addItem(tr(choice.label), static_cast<int>(choice.unit));

	// Preselect whatever unit is currently in EFFECT for this file, not
	// necessarily an already-explicit one - same three-step fallback
	// resolveEffectiveImportUnit() applies (this file node's own importUnit,
	// else the document default, else Millimeter), just without that
	// function's mesh->file-node lookup since fileNode is already known here.
	LengthUnit effectiveUnit = LengthUnit::Millimeter;
	if (_fileNode && _fileNode->importUnit != LengthUnit::Unknown)
		effectiveUnit = _fileNode->importUnit;
	else if (_modelViewer && _modelViewer->defaultImportUnit() != LengthUnit::Unknown)
		effectiveUnit = _modelViewer->defaultImportUnit();
	const int preselectIndex = _unitCombo->findData(static_cast<int>(effectiveUnit));
	if (preselectIndex >= 0)
		_unitCombo->setCurrentIndex(preselectIndex);

	layout->addWidget(_unitCombo);

	layout->addStretch(1);

	auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ImportUnitsDialog::onAccept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttonBox);
}

void ImportUnitsDialog::onAccept()
{
	if (_fileNode)
	{
		const LengthUnit chosen = static_cast<LengthUnit>(_unitCombo->currentData().toInt());
		LengthUnit previousEffective = _fileNode->importUnit;
		if (previousEffective == LengthUnit::Unknown && _modelViewer)
			previousEffective = _modelViewer->defaultImportUnit();
		if (previousEffective == LengthUnit::Unknown)
			previousEffective = LengthUnit::Millimeter;
		_fileNode->importUnit = chosen;
		_fileNode->importUnitUserOverridden = true;
		if (_modelViewer)
		{
			_modelViewer->markNonUndoDocumentModified();
			if (chosen != previousEffective)
				_modelViewer->notifyImportUnitsChanged();
		}
	}
	accept();
}
