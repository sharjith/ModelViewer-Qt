#include "SimulationPanel.h"

#include <QComboBox>
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
	openButton->setToolTip(tr("Add a simulation result (VTK .vtu / .vtk) to this document, shown as its outer surface coloured by a result field.\n"
	                          "To open a result in its own document, use File > Open."));
	connect(openButton, &QPushButton::clicked, this, &SimulationPanel::openRequested);
	root->addWidget(openButton);

	_stack = new QStackedWidget(this);
	root->addWidget(_stack, 1);

	// ---- Page 0: empty state ---------------------------------------------------------------------------------
	auto* emptyPage = new QWidget(_stack);
	auto* emptyLayout = new QVBoxLayout(emptyPage);
	auto* hint = new QLabel(
		tr("No simulation result in this document.\n\nUse File > Open to open a .vtu or .vtk result in its own "
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

	_rangeModeCombo = new QComboBox(content);
	_rangeModeCombo->addItem(tr("Automatic (data range)"), false);
	_rangeModeCombo->addItem(tr("Custom"), true);
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
	form->addRow(tr("Minimum:"), _minSpin);
	_maxSpin = makeRangeSpin();
	form->addRow(tr("Maximum:"), _maxSpin);

	_colormapCombo = new QComboBox(content);
	_colormapCombo->addItem(tr("Rainbow (sequential)"), 0);
	_colormapCombo->addItem(tr("Blue - white - red (diverging)"), 1);
	form->addRow(tr("Colormap:"), _colormapCombo);

	_bandsCombo = new QComboBox(content);
	_bandsCombo->addItem(tr("Smooth"), 0);
	for (int bands : kBandChoices)
		_bandsCombo->addItem(tr("%1 bands").arg(bands), bands);
	form->addRow(tr("Contours:"), _bandsCombo);

	_noteLabel = new QLabel(content);
	_noteLabel->setWordWrap(true);
	form->addRow(_noteLabel);

	auto* units = new QLabel(
		tr("Units are not read from the file yet, so values are shown without a unit."), content);
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
	connect(_minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_updating) emitState(); });
	connect(_maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_updating) emitState(); });
	connect(_colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (!_updating) emitState(); });
	connect(_bandsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (!_updating) emitState(); });
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

	_rangeModeCombo->setCurrentIndex(state.customRange ? 1 : 0);
	_colormapCombo->setCurrentIndex(std::max(0, _colormapCombo->findData(state.colormap)));
	_bandsCombo->setCurrentIndex(std::max(0, _bandsCombo->findData(state.bands)));

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
		for (int c = 0; c < comps; ++c)
			_componentCombo->addItem(tr("Component %1").arg(c + 1), c);
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

void SimulationPanel::refreshRangeEdits()
{
	const bool custom = _rangeModeCombo->currentData().toBool();
	if (custom)
	{
		_minSpin->setEnabled(true);
		_maxSpin->setEnabled(true);
		return; // keep whatever the user set
	}
	DisplayScalar scalar;
	if (_dataset && buildDisplayScalar(*_dataset, _fieldCombo->currentData().toInt(), _componentCombo->currentData().isValid()
	                                   ? _componentCombo->currentData().toInt() : -1, scalar))
		setRangeDisplay(scalar.minValue, scalar.maxValue, false, false);
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
	state.customRange = _rangeModeCombo->currentData().toBool();
	if (state.customRange)
	{
		state.rangeMin = _minSpin->value();
		state.rangeMax = _maxSpin->value();
	}
	state.colormap = _colormapCombo->currentData().toInt();
	state.bands = _bandsCombo->currentData().toInt();
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
	refreshRangeEdits();
	emitState();
}

void SimulationPanel::onRangeModeChanged()
{
	if (_updating)
		return;
	const bool custom = _rangeModeCombo->currentData().toBool();
	if (custom)
	{
		// Start the custom range from the data range currently shown, so switching is a no-op until edited.
		DisplayScalar scalar;
		if (_dataset && buildDisplayScalar(*_dataset, _fieldCombo->currentData().toInt(),
		                                   _componentCombo->currentData().isValid() ? _componentCombo->currentData().toInt() : -1, scalar))
			setRangeDisplay(scalar.minValue, scalar.maxValue, true, true);
		else
			setRangeDisplay(_minSpin->value(), _maxSpin->value(), true, false);
	}
	else
		refreshRangeEdits();
	emitState();
}
