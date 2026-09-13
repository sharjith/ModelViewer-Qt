#include "MassPropertiesDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "MeshProperties.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QStringList>
#include <QVector3D>

MassPropertiesDialog::MassPropertiesDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Mass Properties"));
	resize(560, 420);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Volume, surface area, and mass for the current selection - "
	                                  "recomputed fresh each time this dialog opens."), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	_noSelectionLabel = new QLabel(tr("Nothing selected - select one or more meshes first."), this);
	_noSelectionLabel->setWordWrap(true);
	layout->addWidget(_noSelectionLabel);

	_table = new QTableWidget(this);
	_table->setColumnCount(4);
	_table->setHorizontalHeaderLabels({ tr("Mesh"), tr("Volume (mm³)"), tr("Surface Area (mm²)"), tr("Mass (kg)") });
	_table->horizontalHeader()->setStretchLastSection(true);
	_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	_table->verticalHeader()->setVisible(false);
	_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_table->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(_table, 1);

	_totalsLabel = new QLabel(this);
	_totalsLabel->setWordWrap(true);
	_totalsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(_totalsLabel);

	_closeButton = new QPushButton(tr("Close"), this);
	connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch(1);
	buttonRow->addWidget(_closeButton);
	layout->addLayout(buttonRow);

	populate();
}

void MassPropertiesDialog::populate()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	_noSelectionLabel->setVisible(selected.empty());
	_table->setVisible(!selected.empty());
	_totalsLabel->setVisible(!selected.empty());
	if (selected.empty())
		return;

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	_table->setRowCount(static_cast<int>(selected.size()));

	double totalSurfaceArea = 0.0;

	double knownVolumeSubtotal = 0.0;
	int volumeExcludedCount = 0;
	QStringList volumeExclusionReasons;
	bool allHaveVolume = true;
	QVector3D volumeWeightedCentroidAccum;
	double volumeWeightSum = 0.0;

	double knownMassSubtotal = 0.0;
	int massExcludedCount = 0;
	QStringList massExclusionReasons;
	bool allHaveMass = true;
	QVector3D massWeightedCentroidAccum;
	double massWeightSum = 0.0;

	int row = 0;
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		MeshProperties props(mesh);

		_table->setItem(row, 0, new QTableWidgetItem(mesh->getName()));

		totalSurfaceArea += props.surfaceArea();
		_table->setItem(row, 2, new QTableWidgetItem(QString::number(props.surfaceArea(), 'f', 2)));

		if (props.hasValidVolume())
		{
			_table->setItem(row, 1, new QTableWidgetItem(QString::number(props.volume(), 'f', 2)));
			knownVolumeSubtotal += props.volume();
			volumeWeightedCentroidAccum += props.centerOfMass() * props.volume();
			volumeWeightSum += props.volume();
		}
		else
		{
			const QString reason = describeMeshPropertyUnavailableReason(props.volumeUnavailableReason());
			_table->setItem(row, 1, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++volumeExcludedCount;
			if (!volumeExclusionReasons.contains(reason))
				volumeExclusionReasons.append(reason);
			allHaveVolume = false;
		}

		if (props.hasMass())
		{
			_table->setItem(row, 3, new QTableWidgetItem(QString::number(props.weight(), 'f', 3)));
			knownMassSubtotal += props.weight();
			massWeightedCentroidAccum += props.centerOfMass() * props.weight();
			massWeightSum += props.weight();
		}
		else
		{
			// A mesh with no valid volume has no mass for that SAME reason
			// (there's nothing to multiply a density by) - only surface the
			// distinct "no density assigned" reason when volume itself was
			// actually fine.
			const QString reason = props.hasValidVolume()
				? describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::MissingDensity)
				: describeMeshPropertyUnavailableReason(props.volumeUnavailableReason());
			_table->setItem(row, 3, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++massExcludedCount;
			if (!massExclusionReasons.contains(reason))
				massExclusionReasons.append(reason);
			allHaveMass = false;
		}

		++row;
	}

	// Totals convention, applied identically to volume and mass (and to
	// every future aggregate this app ever adds alongside them): a complete
	// real total only when every contributing mesh had a valid value;
	// otherwise a clearly-labeled known-value subtotal plus an explicit
	// excluded-mesh count and reasons - never a partial sum silently
	// presented as if it were the whole selection's total.
	QString totals;
	totals += tr("Surface Area: %1 mm²\n").arg(totalSurfaceArea, 0, 'f', 2);

	if (volumeExcludedCount == 0)
		totals += tr("Volume: %1 mm³\n").arg(knownVolumeSubtotal, 0, 'f', 2);
	else
		totals += tr("Volume: %1 mm³ known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownVolumeSubtotal, 0, 'f', 2).arg(volumeExcludedCount).arg(selected.size())
			.arg(volumeExclusionReasons.join(QStringLiteral(", ")));

	// Mass is always "before Step 8, no material carries a real density"
	// today - MeshProperties never defaults density to a fake value, so this
	// correctly reads as fully excluded (known subtotal 0, all meshes
	// listed as missing density) rather than silently showing the old
	// hardcoded-1000-kg/m^3 estimate the previous version of this dialog's
	// source data used to compute.
	if (massExcludedCount == 0)
		totals += tr("Mass: %1 kg\n").arg(knownMassSubtotal, 0, 'f', 3);
	else
		totals += tr("Mass: %1 kg known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownMassSubtotal, 0, 'f', 3).arg(massExcludedCount).arg(selected.size())
			.arg(massExclusionReasons.join(QStringLiteral(", ")));

	// Geometric centroid: uniform-density-assumption centroid, volume-
	// weighted across the selection - available whenever every mesh has a
	// valid volume, independent of density/mass entirely.
	if (allHaveVolume && volumeWeightSum > 0.0)
	{
		const QVector3D centroid = volumeWeightedCentroidAccum / static_cast<float>(volumeWeightSum);
		totals += tr("Geometric Centroid: X %1, Y %2, Z %3\n")
			.arg(centroid.x(), 0, 'f', 3).arg(centroid.y(), 0, 'f', 3).arg(centroid.z(), 0, 'f', 3);
	}
	else
	{
		totals += tr("Geometric Centroid: N/A (%1)\n")
			.arg(allHaveVolume ? tr("selection has zero total volume") : tr("not every mesh has a valid volume"));
	}

	// True mass-weighted center of mass - NOT the same thing as the
	// geometric centroid above, and only meaningful once every mesh in the
	// selection has a real, known mass (see MeshProperties::hasMass()'s doc
	// comment on why weighting by a fake/default density would be
	// physically meaningless). Also guards the zero-total-mass case
	// explicitly - density==0 is a legitimately accepted real value, but an
	// all-zero-mass selection has no well-defined mass-weighted centroid
	// (would need to divide by zero), independent of "mass" itself being
	// perfectly well-known.
	if (allHaveMass && massWeightSum > 0.0)
	{
		const QVector3D centroid = massWeightedCentroidAccum / static_cast<float>(massWeightSum);
		totals += tr("Mass-Weighted Center of Mass: X %1, Y %2, Z %3")
			.arg(centroid.x(), 0, 'f', 3).arg(centroid.y(), 0, 'f', 3).arg(centroid.z(), 0, 'f', 3);
	}
	else
	{
		totals += tr("Mass-Weighted Center of Mass: N/A (%1)")
			.arg(!allHaveMass ? tr("not every mesh has a known mass") : tr("selection has zero total mass"));
	}

	_totalsLabel->setText(totals);
}
