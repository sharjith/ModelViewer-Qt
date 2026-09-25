#include "SimulationPanel.h"

#include "ResultUnits.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QLabel>
#include <QLocale>
#include <QSignalBlocker>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
	// QDoubleSpinBox rejects scientific notation while typing ("1e9" is not accepted), but stress values are
	// naturally written that way. This subclass validates and parses through a QDoubleValidator in scientific
	// notation, and shows values with significant digits ('g'), so 2.2e9 reads "2.2035e+09" and 0.75 stays "0.75".
	class ScientificSpinBox : public QDoubleSpinBox
	{
	public:
		using QDoubleSpinBox::QDoubleSpinBox;

		QValidator::State validate(QString& text, int& pos) const override
		{
			QDoubleValidator validator(minimum(), maximum(), 1000);
			validator.setNotation(QDoubleValidator::ScientificNotation);
			validator.setLocale(locale());
			return validator.validate(text, pos);
		}

		double valueFromText(const QString& text) const override
		{
			bool ok = false;
			double v = locale().toDouble(text.trimmed(), &ok);
			if (!ok)
				v = QLocale::c().toDouble(text.trimmed(), &ok); // also accept '.' as the decimal separator
			return ok ? v : value();
		}

		QString textFromValue(double v) const override
		{
			QLocale loc = locale();
			loc.setNumberOptions(QLocale::OmitGroupSeparator);
			return loc.toString(v, 'g', 7);
		}
	};

	// Contour band counts offered next to "Smooth".
	const int kBandChoices[] = { 4, 6, 8, 10, 12, 16, 20, 24, 32, 64 };

	// Decimals that give the range spin boxes about four significant digits of a range spanning `span`.
	int decimalsForSpan(double span)
	{
		if (!(span > 0.0) || !std::isfinite(span))
			return 3;
		return std::clamp(4 - static_cast<int>(std::floor(std::log10(span))), 2, 12);
	}

	// Rounded outward so a custom range prefilled from the data range never clips the data.
	double roundDown(double v, int decimals)
	{
		const double p = std::pow(10.0, decimals);
		return std::floor(v * p) / p;
	}
	double roundUp(double v, int decimals)
	{
		const double p = std::pow(10.0, decimals);
		return std::ceil(v * p) / p;
	}
}

SimulationPanel::SimulationPanel(QWidget* parent)
	: QWidget(parent)
{
	buildUi();
	setSession(nullptr);
}

void SimulationPanel::buildUi()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);

	auto* openButton = new QPushButton(tr("Add Result..."), this);
	openButton->setToolTip(tr("Add a simulation result (.vtu, .vtk, .frd) to this document, shown as its outer surface coloured by a result field.\n"
	                          "To open a result in its own document, use File > Open."));
	connect(openButton, &QPushButton::clicked, this, &SimulationPanel::openRequested);
	root->addWidget(openButton);

	_stack = new QStackedWidget(this);
	root->addWidget(_stack, 1);

	// ---- Page 0: empty state ---------------------------------------------------------------------------------
	auto* emptyPage = new QWidget(_stack);
	auto* emptyLayout = new QVBoxLayout(emptyPage);
	auto* hint = new QLabel(
		tr("No simulation result in this document.\n\nUse File > Open to open a .vtu, .vtk or .frd result in its own "
		   "document, or \"Add Result...\" to add one to this document. A result is shown as its outer surface, "
		   "coloured by a result field; the controls for the field, range, colormap and contours appear here."),
		emptyPage);
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	emptyLayout->addWidget(hint);
	emptyLayout->addStretch(1);
	_stack->addWidget(emptyPage);

	// ---- Page 1: controls ------------------------------------------------------------------------------------
	auto* scroll = new QScrollArea(_stack);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* content = new QWidget(scroll);
	auto* form = new QFormLayout(content);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	_fileLabel = new QLabel(content);
	_fileLabel->setWordWrap(true);
	_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	form->addRow(tr("Result:"), _fileLabel);

	_infoLabel = new QLabel(content);
	_infoLabel->setWordWrap(true);
	form->addRow(tr("Mesh:"), _infoLabel);

	_fieldCombo = new QComboBox(content);
	form->addRow(tr("Field:"), _fieldCombo);

	_componentLabel = new QLabel(tr("Component:"), content);
	_componentCombo = new QComboBox(content);
	form->addRow(_componentLabel, _componentCombo);

	// ---- Units of the current field. Result files carry no units, so the file unit starts as a labelled guess;
	// "Show in" converts. See ResultUnits.h.
	_kindCombo = new QComboBox(content);
	form->addRow(tr("Quantity:"), _kindCombo);
	_fileUnitCombo = new QComboBox(content);
	form->addRow(tr("Values are in:"), _fileUnitCombo);
	_displayUnitCombo = new QComboBox(content);
	form->addRow(tr("Show in:"), _displayUnitCombo);
	_unitStatusLabel = new QLabel(content);
	_unitStatusLabel->setWordWrap(true);
	form->addRow(_unitStatusLabel);

	_rangeModeCombo = new QComboBox(content);
	_rangeModeCombo->addItem(tr("Automatic (data range)"), 1);
	_rangeModeCombo->addItem(tr("Custom"), 2);
	form->addRow(tr("Range:"), _rangeModeCombo);

	// Adaptive stepping makes the arrows and the wheel step relative to the current value, which suits ranges from
	// 1e-6 to 1e9; keyboard tracking off so a value is applied on Enter/focus-out, not on every typed digit.
	auto makeRangeSpin = [content]() {
		auto* spin = new ScientificSpinBox(content);
		spin->setRange(-1.0e30, 1.0e30);
		spin->setDecimals(3);
		spin->setKeyboardTracking(false);
		spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
		spin->setAccelerated(true);
		return spin;
	};
	_minSpin = makeRangeSpin();
	_minLabel = new QLabel(tr("Minimum:"), content);
	form->addRow(_minLabel, _minSpin);
	_maxSpin = makeRangeSpin();
	_maxLabel = new QLabel(tr("Maximum:"), content);
	form->addRow(_maxLabel, _maxSpin);

	_colormapCombo = new QComboBox(content);
	_colormapCombo->addItem(tr("Rainbow (sequential)"), 0);
	_colormapCombo->addItem(tr("Blue - white - red (diverging)"), 1);
	form->addRow(tr("Colormap:"), _colormapCombo);

	_bandsCombo = new QComboBox(content);
	_bandsCombo->addItem(tr("Smooth"), 0);
	for (int bands : kBandChoices)
		_bandsCombo->addItem(tr("%1 bands").arg(bands), bands);
	form->addRow(tr("Contours:"), _bandsCombo);

	_markersCheck = new QCheckBox(tr("Mark minimum and maximum"), content);
	_markersCheck->setToolTip(tr("Label the smallest and largest value on the visible surface. The true extreme can lie inside "
	                             "the volume, where it cannot be shown."));
	form->addRow(_markersCheck);

	// ---- Deformed shape: the displacement field times a scale factor added to the geometry.
	_deformCheck = new QCheckBox(tr("Show deformed shape"), content);
	form->addRow(_deformCheck);
	_deformScaleSpin = new ScientificSpinBox(content);
	_deformScaleSpin->setRange(1.0e-9, 1.0e12);
	_deformScaleSpin->setDecimals(6);
	_deformScaleSpin->setKeyboardTracking(false);
	_deformScaleSpin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
	_deformScaleSpin->setAccelerated(true);
	_deformScaleSpin->setToolTip(tr("Factor applied to the displacements. 1 is the true deformation; results are usually "
	                                "exaggerated so that it is visible."));
	_deformAutoButton = new QPushButton(tr("Auto"), content);
	_deformAutoButton->setToolTip(tr("Choose a factor that makes the largest displacement about a tenth of the model size"));
	auto* scaleRow = new QHBoxLayout();
	scaleRow->addWidget(_deformScaleSpin, 1);
	scaleRow->addWidget(_deformAutoButton);
	form->addRow(tr("Scale factor:"), scaleRow);
	_deformInfoLabel = new QLabel(content);
	_deformInfoLabel->setWordWrap(true);
	form->addRow(_deformInfoLabel);

	_noteLabel = new QLabel(content);
	_noteLabel->setWordWrap(true);
	form->addRow(_noteLabel);

	auto* units = new QLabel(
		tr("Result files do not store units. The unit above is a guess from the field name and the file type until "
		   "you confirm it; choosing a different \"Show in\" unit converts the values and the legend."), content);
	units->setWordWrap(true);
	form->addRow(units);

	scroll->setWidget(content);
	_stack->addWidget(scroll);

	// ---- Signals ----------------------------------------------------------------------------------------------
	connect(_fieldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onFieldChanged(); });
	connect(_componentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (_updating)
			return;
		refreshRangeEdits();
		emitState();
	});
	connect(_rangeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onRangeModeChanged(); });
	connect(_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onKindEdited(); });
	connect(_fileUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onUnitEdited(); });
	connect(_displayUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onUnitEdited(); });
	connect(_minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_updating) emitState(); });
	connect(_maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_updating) emitState(); });
	connect(_colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (!_updating) emitState(); });
	connect(_bandsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (!_updating) emitState(); });
	connect(_markersCheck, &QCheckBox::toggled, this, [this](bool) { if (!_updating) emitState(); });
	connect(_deformCheck, &QCheckBox::toggled, this, [this](bool on) {
		_deformScaleSpin->setEnabled(on && _deformCheck->isEnabled());
		_deformAutoButton->setEnabled(on && _deformCheck->isEnabled());
		if (!_updating)
			emitState();
	});
	connect(_deformScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_updating) emitState(); });
	connect(_deformAutoButton, &QPushButton::clicked, this, [this]() {
		_deformScaleSpin->setValue(_autoDeformScale); // emits through valueChanged (unless it already is that value)
	});
}

void SimulationPanel::setSession(const SimulationSession* session)
{
	_updating = true;
	if (!session || !session->dataset)
	{
		_dataset.reset();
		_stack->setCurrentIndex(0);
		_updating = false;
		return;
	}

	_dataset = session->dataset;
	const SimulationViewState& state = session->state;
	_fileLabel->setText(QFileInfo(session->filePath).fileName());
	_fileLabel->setToolTip(session->filePath);
	_infoLabel->setText(tr("%1 nodes, %2 cells, %3 surface triangles")
		.arg(_dataset->nodeCount()).arg(_dataset->cellCount()).arg(session->surface ? session->surface->triangleCount() : 0));

	populateFields(state.fieldIndex);
	populateComponents(_fieldCombo->currentData().toInt(), state.component);
	populateUnits(_fieldCombo->currentData().toInt());

	_step = state.step;
	populateRangeModes(_dataset->stepCount() > 1, state);
	_colormapCombo->setCurrentIndex(std::max(0, _colormapCombo->findData(state.colormap)));
	_bandsCombo->setCurrentIndex(std::max(0, _bandsCombo->findData(state.bands)));

	_markersCheck->setChecked(state.markExtrema);

	const bool canDeform = session->displacementField >= 0;
	_autoDeformScale = session->autoDeformScale;
	_deformCheck->setEnabled(canDeform);
	_deformCheck->setChecked(canDeform && state.deform);
	_deformScaleSpin->setValue(std::max(1.0e-9, state.deformScale));
	_deformScaleSpin->setEnabled(canDeform && state.deform);
	_deformAutoButton->setEnabled(canDeform && state.deform);
	_deformInfoLabel->setText(canDeform
		? (session->modal
			? tr("Displacement field: %1. Mode shapes have no physical amplitude, so each mode is drawn with its largest "
			     "displacement at a tenth of the model size; the factor scales that (1 = a tenth).")
			: tr("Displacement field: %1 (applied in the file's own length unit)"))
			.arg(_dataset->fields[static_cast<std::size_t>(session->displacementField)].name)
		: tr("This result has no displacement field, so it cannot be shown deformed."));

	// Custom range: show the state's values; automatic: refreshRangeEdits() shows the data range.
	if (state.customRange)
		setRangeDisplay(state.rangeMin, state.rangeMax, true, false);
	else
		refreshRangeEdits();

	QStringList notes = session->warnings;
	std::size_t cellFields = 0;
	for (const ResultField& f : _dataset->fields)
		if (f.association == ResultFieldAssociation::Cell)
			++cellFields;
	if (cellFields > 0)
		notes << tr("%1 cell-data field(s) in the file cannot be shown yet (only node data).").arg(cellFields);
	_noteLabel->setText(notes.join(QLatin1Char('\n')));
	_noteLabel->setVisible(!notes.isEmpty());

	_stack->setCurrentIndex(1);
	_updating = false;
}

void SimulationPanel::populateFields(int selectedFieldIndex)
{
	_fieldCombo->clear();
	if (!_dataset)
		return;
	for (std::size_t i = 0; i < _dataset->fields.size(); ++i)
	{
		const ResultField& f = _dataset->fields[i];
		if (f.association != ResultFieldAssociation::Node || f.stepData.empty() || f.stepData[0].empty())
			continue;
		const QString kind = f.components == 1 ? QString() : tr(" (%1 components)").arg(f.components);
		_fieldCombo->addItem(f.name + kind, static_cast<int>(i));
	}
	if (_fieldCombo->count() == 0)
	{
		_fieldCombo->addItem(tr("(no node fields)"), -1);
		_fieldCombo->setEnabled(false);
		return;
	}
	_fieldCombo->setEnabled(true);
	_fieldCombo->setCurrentIndex(std::max(0, _fieldCombo->findData(selectedFieldIndex)));
}

void SimulationPanel::populateComponents(int fieldIndex, int selectedComponent)
{
	_componentCombo->clear();
	int comps = 1;
	if (_dataset && fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < _dataset->fields.size())
		comps = _dataset->fields[static_cast<std::size_t>(fieldIndex)].components;

	if (comps == 1)
	{
		_componentLabel->setVisible(false);
		_componentCombo->setVisible(false);
		return;
	}
	_componentLabel->setVisible(true);
	_componentCombo->setVisible(true);
	if (comps == 3)
	{
		_componentCombo->addItem(tr("Magnitude"), -1);
		_componentCombo->addItem(tr("X"), 0);
		_componentCombo->addItem(tr("Y"), 1);
		_componentCombo->addItem(tr("Z"), 2);
	}
	else // tensors and other multi-component fields: no meaningful magnitude, pick a component
	{
		const std::vector<QString>* names = nullptr;
		if (_dataset && fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < _dataset->fields.size())
			names = &_dataset->fields[static_cast<std::size_t>(fieldIndex)].componentNames;
		for (int c = 0; c < comps; ++c)
			_componentCombo->addItem(names && names->size() == static_cast<std::size_t>(comps)
			                             ? (*names)[static_cast<std::size_t>(c)]
			                             : tr("Component %1").arg(c + 1),
			                         c);
	}
	const int index = _componentCombo->findData(selectedComponent);
	_componentCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void SimulationPanel::setRangeDisplay(double lo, double hi, bool custom, bool outward)
{
	const int decimals = decimalsForSpan(hi - lo);
	if (outward)
	{
		lo = roundDown(lo, decimals);
		hi = roundUp(hi, decimals);
	}
	const QSignalBlocker blockMin(_minSpin);
	const QSignalBlocker blockMax(_maxSpin);
	_minSpin->setDecimals(decimals);
	_maxSpin->setDecimals(decimals);
	_minSpin->setValue(lo);
	_maxSpin->setValue(hi);
	_minSpin->setEnabled(custom);
	_maxSpin->setEnabled(custom);
}

int SimulationPanel::rangeMode() const
{
	return _rangeModeCombo->currentData().toInt();
}

void SimulationPanel::populateRangeModes(bool multiStep, const SimulationViewState& state)
{
	const QSignalBlocker block(_rangeModeCombo);
	_rangeModeCombo->clear();
	if (multiStep)
	{
		// A fixed scale over all steps keeps the frames of an animation comparable, so it is the default.
		_rangeModeCombo->addItem(tr("Automatic (all steps)"), 0);
		_rangeModeCombo->addItem(tr("Automatic (this step)"), 1);
	}
	else
		_rangeModeCombo->addItem(tr("Automatic (data range)"), 1);
	_rangeModeCombo->addItem(tr("Custom"), 2);
	const int wanted = state.customRange ? 2 : ((state.allStepsRange && multiStep) ? 0 : 1);
	_rangeModeCombo->setCurrentIndex(std::max(0, _rangeModeCombo->findData(wanted)));
}

bool SimulationPanel::currentDataRange(bool allSteps, float& lo, float& hi) const
{
	if (!_dataset)
		return false;
	const int field = _fieldCombo->currentData().toInt();
	const int component = _componentCombo->currentData().isValid() ? _componentCombo->currentData().toInt() : -1;
	if (allSteps && _dataset->stepCount() > 1)
		return computeAllStepsRange(*_dataset, field, component, lo, hi);
	DisplayScalar scalar;
	if (!buildDisplayScalar(*_dataset, field, component, scalar, _step))
		return false;
	lo = scalar.minValue;
	hi = scalar.maxValue;
	return true;
}

void SimulationPanel::refreshRangeEdits()
{
	const int mode = rangeMode();
	if (mode == 2)
	{
		_minSpin->setEnabled(true);
		_maxSpin->setEnabled(true);
		return; // keep whatever the user set
	}
	_lastAutoAllSteps = mode == 0;
	float lo = 0.0f, hi = 0.0f;
	if (currentDataRange(mode == 0, lo, hi))
		setRangeDisplay(lo, hi, false, false);
	else
		setRangeDisplay(0.0, 0.0, false, false);
}

SimulationViewState SimulationPanel::currentState() const
{
	SimulationViewState state;
	state.fieldIndex = _fieldCombo->currentData().isValid() ? _fieldCombo->currentData().toInt() : -1;
	// isHidden(), not !isVisible(): the tab may simply not be the current one, which says nothing about the field.
	state.component = !_componentCombo->isHidden() && _componentCombo->currentData().isValid()
		? _componentCombo->currentData().toInt() : -1;
	const int mode = rangeMode();
	state.customRange = mode == 2;
	state.allStepsRange = mode == 0;
	state.step = _step; // informational: ModelViewer keeps the timeline's own step
	if (state.customRange)
	{
		state.rangeMin = _minSpin->value();
		state.rangeMax = _maxSpin->value();
	}
	state.colormap = _colormapCombo->currentData().toInt();
	state.bands = _bandsCombo->currentData().toInt();
	state.markExtrema = _markersCheck->isChecked();
	state.deform = _deformCheck->isChecked();
	state.deformScale = _deformScaleSpin->value();
	return state;
}

void SimulationPanel::emitState()
{
	emit viewStateChanged(currentState());
}

void SimulationPanel::onFieldChanged()
{
	if (_updating)
		return;
	const int fieldIndex = _fieldCombo->currentData().toInt();
	// Default component for the new field: the magnitude of a vector, the first component of a tensor.
	populateComponents(fieldIndex, -1);
	populateUnits(fieldIndex);
	refreshRangeEdits();
	emitState();
}

void SimulationPanel::onRangeModeChanged()
{
	if (_updating)
		return;
	if (rangeMode() == 2)
	{
		// Start the custom range from the automatic range that was showing, so switching is a no-op until edited.
		float lo = 0.0f, hi = 0.0f;
		if (currentDataRange(_lastAutoAllSteps, lo, hi))
			setRangeDisplay(lo, hi, true, true);
		else
			setRangeDisplay(_minSpin->value(), _maxSpin->value(), true, false);
	}
	else
		refreshRangeEdits();
	emitState();
}

// ---------------------------------------------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------------------------------------------

void SimulationPanel::populateUnits(int fieldIndex)
{
	const ResultField* field = nullptr;
	if (_dataset && fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < _dataset->fields.size())
		field = &_dataset->fields[static_cast<std::size_t>(fieldIndex)];

	const QSignalBlocker blockKind(_kindCombo);
	_kindCombo->clear();
	_kindCombo->addItem(tr("Not specified"), QString());
	for (const QuantityKindInfo& kind : quantityKinds())
		_kindCombo->addItem(kind.label, kind.id);
	const QString kindId = field ? field->quantityKind : QString();
	_kindCombo->setCurrentIndex(std::max(0, _kindCombo->findData(kindId)));
	_kindCombo->setEnabled(field != nullptr);

	populateUnitCombos(kindId, field ? field->fileUnit : QString(),
	                   field ? (field->displayUnit.isEmpty() ? field->fileUnit : field->displayUnit) : QString());
	updateUnitLabels(fieldIndex);

	if (!field)
		_unitStatusLabel->setText(QString());
	else if (field->quantityKind.isEmpty() || field->fileUnit.isEmpty())
		_unitStatusLabel->setText(tr("Unit not specified. Choose the quantity, then the unit the values are written in."));
	else if (!field->unitConfirmed)
		_unitStatusLabel->setText(tr("Assumed from the field name and the file type. Confirm or change it."));
	else
		_unitStatusLabel->setText(QString());
	_unitStatusLabel->setVisible(!_unitStatusLabel->text().isEmpty());
}

void SimulationPanel::populateUnitCombos(const QString& kindId, const QString& fileUnit, const QString& displayUnit)
{
	const QSignalBlocker blockFile(_fileUnitCombo);
	const QSignalBlocker blockDisplay(_displayUnitCombo);
	_fileUnitCombo->clear();
	_displayUnitCombo->clear();
	const QStringList symbols = unitSymbols(kindId);
	_fileUnitCombo->addItem(tr("(not specified)"), QString());
	if (symbols.isEmpty())
	{
		_displayUnitCombo->addItem(tr("(not specified)"), QString());
		_fileUnitCombo->setEnabled(false);
		_displayUnitCombo->setEnabled(false);
		return;
	}
	for (const QString& symbol : symbols)
	{
		_fileUnitCombo->addItem(symbol, symbol);
		_displayUnitCombo->addItem(symbol, symbol);
	}
	_fileUnitCombo->setCurrentIndex(std::max(0, _fileUnitCombo->findData(fileUnit)));
	_fileUnitCombo->setEnabled(true);
	// Showing in another unit needs to know what the numbers are in first.
	if (fileUnit.isEmpty())
	{
		_displayUnitCombo->insertItem(0, tr("(not specified)"), QString());
		_displayUnitCombo->setCurrentIndex(0);
		_displayUnitCombo->setEnabled(false);
	}
	else
	{
		_displayUnitCombo->setCurrentIndex(std::max(0, _displayUnitCombo->findData(displayUnit.isEmpty() ? fileUnit : displayUnit)));
		_displayUnitCombo->setEnabled(true);
	}
}

// The range boxes are in the display unit; say which.
void SimulationPanel::updateUnitLabels(int fieldIndex)
{
	QString unit;
	if (_dataset && fieldIndex >= 0 && static_cast<std::size_t>(fieldIndex) < _dataset->fields.size())
	{
		const ResultField& f = _dataset->fields[static_cast<std::size_t>(fieldIndex)];
		unit = f.displayUnit.isEmpty() ? f.fileUnit : f.displayUnit;
	}
	_minLabel->setText(unit.isEmpty() ? tr("Minimum:") : tr("Minimum [%1]:").arg(unit));
	_maxLabel->setText(unit.isEmpty() ? tr("Maximum:") : tr("Maximum [%1]:").arg(unit));
}

void SimulationPanel::onKindEdited()
{
	if (_updating)
		return;
	const QString kindId = _kindCombo->currentData().toString();
	// A new quantity: the unit is NOT assumed - the user says what the values are written in next.
	populateUnitCombos(kindId, QString(), QString());
	emit unitsChanged(_fieldCombo->currentData().toInt(), kindId, QString(), QString());
}

void SimulationPanel::onUnitEdited()
{
	if (_updating)
		return;
	const QString fileUnit = _fileUnitCombo->currentData().toString();
	QString displayUnit = _displayUnitCombo->currentData().toString();
	if (fileUnit.isEmpty())
		displayUnit.clear();
	else if (displayUnit.isEmpty())
		displayUnit = fileUnit; // just told what the values are in: show them as they are until another unit is chosen
	emit unitsChanged(_fieldCombo->currentData().toInt(), _kindCombo->currentData().toString(), fileUnit, displayUnit);
}
