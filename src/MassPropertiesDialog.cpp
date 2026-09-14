#include "MassPropertiesDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "MeshProperties.h"
#include "Material.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QStringList>
#include <QVector3D>
#include <QApplication>
#include <QMap>

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

	// This app has no real per-document/per-import unit policy yet (a known,
	// disclosed prerequisite - see MeshProperties.cpp's own doc comment) -
	// every length-bearing value here is computed straight from the mesh's
	// raw coordinates on the ASSUMPTION they're millimetres, the same
	// assumption every other length-bearing control in this app already
	// makes. Said explicitly here (not just in a code comment) since a
	// wrongly-labeled engineering measurement is worse than an admittedly
	// unverified one.
	auto* unitsNote = new QLabel(tr("Units below assume millimetre input (not yet verified against the "
	                                 "source file/import) - treat mm²/mm³/kg as provisional. Density comes "
	                                 "from each mesh's assigned material; library-supplied values are typical/"
	                                 "nominal figures for a generic grade, not an exact spec - verify before "
	                                 "relying on Mass for an engineering-critical calculation."), this);
	unitsNote->setWordWrap(true);
	layout->addWidget(unitsNote);

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

	// Per-material mass breakdown, in its OWN table rather than appended as
	// more lines onto _totalsLabel above - a selection spanning many
	// distinctly-named materials would otherwise keep growing that label
	// without bound and push the table/buttons off-screen. A QTableWidget
	// scrolls natively within whatever space this layout gives it, the same
	// way _table above already does, instead of forcing the dialog itself
	// to grow to fit every row.
	_materialBreakdownLabel = new QLabel(tr("Mass by Material:"), this);
	layout->addWidget(_materialBreakdownLabel);

	_materialTable = new QTableWidget(this);
	_materialTable->setColumnCount(2);
	_materialTable->setHorizontalHeaderLabels({ tr("Material"), tr("Mass (kg)") });
	_materialTable->horizontalHeader()->setStretchLastSection(true);
	_materialTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	_materialTable->verticalHeader()->setVisible(false);
	_materialTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_materialTable->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(_materialTable, 1);

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
	_materialBreakdownLabel->setVisible(false);
	_materialTable->setVisible(false);
	_materialTable->setRowCount(0);
	if (selected.empty())
		return;

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	_table->setRowCount(static_cast<int>(selected.size()));

	double knownSurfaceAreaSubtotal = 0.0;
	int surfaceAreaExcludedCount = 0;
	QStringList surfaceAreaExclusionReasons;

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

	// Mass rollup grouped by material NAME - the only identity Material
	// exposes (it carries no UUID), so two differently-configured materials
	// that happen to share a display name are indistinguishable here; a
	// known, documented limitation (per Step 8's plan) rather than a silent
	// inaccuracy. Computed per-mesh, before grouping - each mesh's mass
	// comes from ITS OWN material's density, never a group-level density
	// applied after the fact, so two same-named-but-differently-configured
	// materials never silently share one density value.
	struct MaterialMassGroup
	{
		double knownMassSubtotal = 0.0;
		int excludedCount = 0;
		int totalCount = 0;
		QStringList exclusionReasons;
	};
	QMap<QString, MaterialMassGroup> massByMaterial;

	// Cheap interim mitigation, not real async: MeshProperties' CGAL topology
	// checks (is_closed/does_self_intersect/does_bound_a_volume) run
	// synchronously on the UI thread per mesh below and can take a
	// noticeable moment on a CAD-sized selection - a busy cursor at least
	// signals that something is happening rather than looking frozen. Real
	// background computation with progress/cancellation is a separate,
	// larger piece of work (see this dialog's own follow-up notes).
	QApplication::setOverrideCursor(Qt::WaitCursor);

	int row = 0;
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		MeshProperties props(mesh);

		// Sourced from THIS mesh's own material - no fallback density, ever
		// (see Material::hasDensity()'s own doc comment on why a material
		// with no known density must never silently default to one).
		const Material meshMaterial = mesh->getMaterial();
		if (meshMaterial.hasDensity())
			props.setDensity(meshMaterial.density());

		_table->setItem(row, 0, new QTableWidgetItem(mesh->getName()));

		// hasValidGeometry() gates this the same way hasValidVolume()/
		// hasMass() gate the other two columns below - surfaceArea() reads
		// 0.0f (not a real zero-area result) whenever the per-triangle scan
		// itself failed (invalid/degenerate indices, or threw), and that
		// must never be silently summed into the total as if it were a
		// legitimate answer.
		if (props.hasValidGeometry())
		{
			_table->setItem(row, 2, new QTableWidgetItem(QString::number(props.surfaceArea(), 'f', 2)));
			knownSurfaceAreaSubtotal += props.surfaceArea();
		}
		else
		{
			const QString reason = describeMeshPropertyUnavailableReason(props.volumeUnavailableReason());
			_table->setItem(row, 2, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++surfaceAreaExcludedCount;
			if (!surfaceAreaExclusionReasons.contains(reason))
				surfaceAreaExclusionReasons.append(reason);
		}

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

		MaterialMassGroup& group = massByMaterial[meshMaterial.name()];
		++group.totalCount;

		if (props.hasMass())
		{
			_table->setItem(row, 3, new QTableWidgetItem(QString::number(props.weight(), 'f', 3)));
			knownMassSubtotal += props.weight();
			massWeightedCentroidAccum += props.centerOfMass() * props.weight();
			massWeightSum += props.weight();
			group.knownMassSubtotal += props.weight();
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

			++group.excludedCount;
			if (!group.exclusionReasons.contains(reason))
				group.exclusionReasons.append(reason);
		}

		++row;
	}

	QApplication::restoreOverrideCursor();

	// Totals convention, applied identically to volume and mass (and to
	// every future aggregate this app ever adds alongside them): a complete
	// real total only when every contributing mesh had a valid value;
	// otherwise a clearly-labeled known-value subtotal plus an explicit
	// excluded-mesh count and reasons - never a partial sum silently
	// presented as if it were the whole selection's total.
	QString totals;
	if (surfaceAreaExcludedCount == 0)
		totals += tr("Surface Area: %1 mm²\n").arg(knownSurfaceAreaSubtotal, 0, 'f', 2);
	else
		totals += tr("Surface Area: %1 mm² known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownSurfaceAreaSubtotal, 0, 'f', 2).arg(surfaceAreaExcludedCount).arg(selected.size())
			.arg(surfaceAreaExclusionReasons.join(QStringLiteral(", ")));

	if (volumeExcludedCount == 0)
		totals += tr("Volume: %1 mm³\n").arg(knownVolumeSubtotal, 0, 'f', 2);
	else
		totals += tr("Volume: %1 mm³ known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownVolumeSubtotal, 0, 'f', 2).arg(volumeExcludedCount).arg(selected.size())
			.arg(volumeExclusionReasons.join(QStringLiteral(", ")));

	// Mass is sourced entirely from each mesh's own material's density
	// (Step 8) - MeshProperties never defaults density to a fake value, so
	// a mesh whose material has no assigned density (or no material at
	// all) correctly reads as excluded here rather than silently using the
	// old hardcoded-1000-kg/m^3 estimate this dialog's data used to compute
	// before Step 8.
	if (massExcludedCount == 0)
		totals += tr("Mass: %1 kg\n").arg(knownMassSubtotal, 0, 'f', 3);
	else
		totals += tr("Mass: %1 kg known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownMassSubtotal, 0, 'f', 3).arg(massExcludedCount).arg(selected.size())
			.arg(massExclusionReasons.join(QStringLiteral(", ")));

	// Per-material breakdown, same known-subtotal + excluded-count + reasons
	// convention as the assembly-level totals above - a group with some but
	// not all contributing meshes lacking density shows its OWN subtotal and
	// exclusions, never a bare "-" the way an earlier draft of this dialog
	// would have. Grouped by material NAME (see massByMaterial's own doc
	// comment on why - Material carries no UUID).
	//
	// Rendered into its own scrollable table (_materialTable), not appended
	// as more text onto _totalsLabel - a selection spanning many distinctly-
	// named materials must not keep growing a plain label without bound and
	// push the rest of the dialog off-screen (P2 Codex fix).
	_materialBreakdownLabel->setVisible(!massByMaterial.isEmpty());
	_materialTable->setVisible(!massByMaterial.isEmpty());
	_materialTable->setRowCount(static_cast<int>(massByMaterial.size()));
	int materialRow = 0;
	for (auto it = massByMaterial.constBegin(); it != massByMaterial.constEnd(); ++it)
	{
		const MaterialMassGroup& group = it.value();
		auto* nameItem = new QTableWidgetItem(it.key());
		nameItem->setToolTip(it.key());
		_materialTable->setItem(materialRow, 0, nameItem);
		const QString massText = (group.excludedCount == 0)
			? tr("%1 kg").arg(group.knownMassSubtotal, 0, 'f', 3)
			: tr("%1 kg known (%2 of %3 mesh(es) excluded - %4)")
				.arg(group.knownMassSubtotal, 0, 'f', 3)
				.arg(group.excludedCount).arg(group.totalCount)
				.arg(group.exclusionReasons.join(QStringLiteral(", ")));
		auto* massItem = new QTableWidgetItem(massText);
		// A mixed-validity group's exclusion-reasons list can run longer than
		// the column is wide (elided text in a fixed-height row silently hides
		// which reasons are involved) - the tooltip always carries the full,
		// un-elided string regardless of column width.
		massItem->setToolTip(massText);
		_materialTable->setItem(materialRow, 1, massItem);
		++materialRow;
	}

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
