#include "Pdf3DExportDialog.h"
#include "ui_Pdf3DExportDialog.h"

#include "Pdf3DExporter.h"
#include "SheetPreviewWidget.h"
#include "SheetTemplateEditor.h"
#include "SheetRenderer.h"

#include <QComboBox>
#include <QDate>
#include <algorithm>
#include <cmath>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUrl>

#include "LanguageManager.h"

namespace
{
	constexpr int kViewIndexRole = Qt::UserRole;       // the index into the caller's ViewItem list, on the leaf items
	constexpr int kViewKeyRole = Qt::UserRole + 1;     // "group|name" of a view, to remember its tick state
	constexpr int kFixedFormRows = 7;                  // title .. notes in the Sheet tab's form; the template's custom fields follow

	// The locale of the application's language: numbers are typed and printed in its style (the system's when no language is loaded).
	QLocale appLocale()
	{
		const QString language = LanguageManager::instance().currentLanguage();
		return language.isEmpty() ? QLocale() : QLocale(language);
	}

	// The box of a number field: a spin box in the application's language style with the field's decimals, limits and unit. It can also be
	// empty (nothing is printed): the lowest position, one step below the minimum, stands for that and shows nothing, and the first step up
	// from it goes to 0 (or to the minimum when that is above 0), not to the minimum of a field without limits.
	class SheetNumberBox : public QDoubleSpinBox
	{
	public:
		SheetNumberBox(const Pdf3D::SheetField& field, const QLocale& locale, QWidget* parent = nullptr)
			: QDoubleSpinBox(parent)
			, _minimum(field.minimum)
			, _maximum(field.maximum)
			, _unit(field.unit)
		{
			setLocale(locale);
			setDecimals(field.decimals);
			_step = std::pow(10.0, -field.decimals);
			setRange(_minimum - _step, _maximum);
			setSingleStep(std::max(_step, std::min(1.0, (_maximum - _minimum) / 10.0)));
			setSpecialValueText(QStringLiteral(" "));
			setGroupSeparatorShown(false);
			if (!field.unit.isEmpty())
				setSuffix(QLatin1Char(' ') + field.unit);
			setEmpty();
		}

		bool isEmptyValue() const { return value() < _minimum - _step * 0.5; }
		void setEmpty() { setValue(_minimum - _step); }
		QString unitText() const { return _unit; }
		void stepBy(int steps) override
		{
			if (isEmptyValue())
				setValue(std::clamp(0.0, _minimum, _maximum));
			else
				QDoubleSpinBox::stepBy(steps);
		}

	protected:
		// text with nothing in it but the unit is "empty": it is accepted, so the number can be deleted to print nothing
		bool isBlankText(const QString& text) const
		{
			QString core = text;
			core.remove(prefix());
			core.remove(suffix());
			return core.trimmed().isEmpty();
		}
		QValidator::State validate(QString& input, int& pos) const override
		{
			return isBlankText(input) ? QValidator::Acceptable : QDoubleSpinBox::validate(input, pos);
		}
		double valueFromText(const QString& text) const override
		{
			return isBlankText(text) ? _minimum - _step : QDoubleSpinBox::valueFromText(text);
		}

	private:
		double _minimum, _maximum, _step = 0.01;
		QString _unit;
	};

	// The text of a custom field's editor, whatever its type. A number box gives the number as the sheet prints it (`formatted`) or, for
	// what is remembered, in the plain form with a point; empty when it is empty.
	QString editorValue(const QWidget* editor, const QLocale& locale, bool formatted)
	{
		if (const auto* number = dynamic_cast<const SheetNumberBox*>(editor))
		{
			if (number->isEmptyValue())
				return QString();
			if (!formatted)
				return QLocale::c().toString(number->value(), 'f', number->decimals());
			Pdf3D::SheetField field;
			field.decimals = number->decimals();
			field.unit = number->unitText();
			return Pdf3D::formatSheetNumber(field, number->value(), locale);
		}
		if (const auto* line = qobject_cast<const QLineEdit*>(editor))
			return line->text().trimmed();
		if (const auto* plain = qobject_cast<const QPlainTextEdit*>(editor))
			return plain->toPlainText().trimmed();
		if (const auto* combo = qobject_cast<const QComboBox*>(editor))
			return combo->currentText();
		if (const auto* date = qobject_cast<const QDateEdit*>(editor))
			return date->date().toString(Qt::ISODate);
		return QString();
	}
}

Pdf3DExportDialog::Pdf3DExportDialog(const QString& defaultTitle, const QVector<ViewItem>& views, const QString& viewsInfo,
	const QVector<OverlayItem>& overlays, const QVector<AnimationItem>& animations, QWidget* parent)
	: QDialog(parent)
	, ui(std::make_unique<Ui::Pdf3DExportDialog>())
	, _viewCount(views.size())
	, _overlayCount(overlays.size())
{
	ui->setupUi(this);

	QSettings settings;
	restoreGeometry(settings.value(QStringLiteral("pdf3d/dialogGeometry2")).toByteArray());
	ui->titleEdit->setText(defaultTitle);
	ui->drawingNumberEdit->setText(settings.value(QStringLiteral("pdf3d/drawingNumber")).toString());
	ui->revisionEdit->setText(settings.value(QStringLiteral("pdf3d/revision")).toString());
	ui->authorEdit->setText(settings.value(QStringLiteral("pdf3d/author")).toString());
	ui->companyEdit->setText(settings.value(QStringLiteral("pdf3d/company")).toString());
	ui->scaleEdit->setText(settings.value(QStringLiteral("pdf3d/scale")).toString());
	ui->partsListCheck->setChecked(settings.value(QStringLiteral("pdf3d/partsList"), true).toBool());
	ui->viewButtonsCheck->setChecked(settings.value(QStringLiteral("pdf3d/viewButtons"), true).toBool());
	ui->titleEdit->selectAll();

	// the sheet template: built-in and user templates in a combo; the fields below follow the chosen one
	// the combo is editable: typing a new name and pressing Enter saves the current template under that name
	ui->templateCombo->setEditable(true);
	ui->templateCombo->setInsertPolicy(QComboBox::NoInsert);
	connect(ui->templateCombo, &QComboBox::currentIndexChanged, this, [this]() { applyTemplateSelection(); });
	connect(ui->templateCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() { templateNameEntered(); });
	connect(ui->logoTemplateButton, &QPushButton::clicked, this, [this]() { chooseLogo(); });
	connect(ui->editTemplateButton, &QPushButton::clicked, this, [this]() { editTemplate(); });
	connect(ui->reloadTemplatesButton, &QPushButton::clicked, this, [this]() { reloadTemplates(currentTemplateId()); });
	connect(ui->openTemplatesButton, &QPushButton::clicked, this, []() {
		const QString folder = Pdf3D::userSheetTemplateFolder();
		if (!folder.isEmpty())
		{
			QDir().mkpath(folder);
			QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
		}
	});
	connect(ui->sampleTemplateButton, &QPushButton::clicked, this, [this]() {
		QString error;
		const QString id = Pdf3D::writeSampleSheetTemplate(Pdf3D::userSheetTemplateFolder(), &error);
		if (id.isEmpty())
		{
			QMessageBox::warning(this, tr("Sheet template"), error);
			return;
		}
		reloadTemplates(id);
	});
	// the sheet preview below the fields, drawn by the exporter's own code; it follows every edit
	_preview = new Pdf3D::SheetPreviewWidget(this);
	_preview->setMinimumSize(380, 270);
	ui->sheetBodyLayout->setStretch(0, 2);   // the fields at the left, the preview taking the larger share at the right
	ui->sheetBodyLayout->addWidget(_preview, 3);
	for (QLineEdit* edit : { ui->titleEdit, ui->drawingNumberEdit, ui->revisionEdit, ui->authorEdit, ui->companyEdit, ui->scaleEdit })
		connect(edit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
	connect(ui->notesEdit, &QPlainTextEdit::textChanged, this, [this]() { updatePreview(); });
	reloadTemplates(settings.value(QStringLiteral("pdf3d/template")).toString());

	// Views the user unticked earlier (and views they switched the measurements off for) stay unticked: remembered by group and name,
	// across documents. Everything else starts ticked.
	const QStringList uncheckedViews = settings.value(QStringLiteral("pdf3d/viewsUnchecked")).toStringList();
	const QStringList noOverlayViews = settings.value(QStringLiteral("pdf3d/viewsWithoutMeasurements")).toStringList();

	// the views, grouped in the order the groups first appear
	QTreeWidgetItem* group = nullptr;
	QString groupName;
	for (int i = 0; i < views.size(); ++i)
	{
		const ViewItem& view = views[i];
		if (!group || view.group != groupName)
		{
			group = new QTreeWidgetItem(ui->viewsTree, QStringList{view.group});
			group->setFlags(group->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
			group->setCheckState(0, Qt::Checked);
			QFont f = group->font(0);
			f.setBold(true);
			group->setFont(0, f);
			groupName = view.group;
		}
		auto* item = new QTreeWidgetItem(group, QStringList{view.name, view.note});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		const QString key = view.group + QLatin1Char('|') + view.name;
		item->setData(0, kViewKeyRole, key);
		item->setCheckState(0, uncheckedViews.contains(key) ? Qt::Unchecked : Qt::Checked);
		item->setData(0, kViewIndexRole, i);
		item->setCheckState(2, (view.overlays && !noOverlayViews.contains(key)) ? Qt::Checked : Qt::Unchecked);   // show the measurements and annotations in this view
		if (!view.note.isEmpty())
		{
			item->setToolTip(1, view.note);
			if (view.warning)
				item->setForeground(1, palette().color(QPalette::Link));
			else
				item->setForeground(1, palette().color(QPalette::Disabled, QPalette::Text));
		}
	}
	ui->viewsTree->expandAll();
	ui->viewsTree->resizeColumnToContents(0);
	// the column is only useful when there is something to show
	ui->viewsTree->setColumnHidden(2, overlays.isEmpty());

	// the info line is deliberately subtle: a remark, not a warning dialog
	ui->viewsInfoLabel->setText(viewsInfo);
	ui->viewsInfoLabel->setVisible(!viewsInfo.isEmpty());
	QPalette infoPalette = ui->viewsInfoLabel->palette();
	infoPalette.setColor(QPalette::WindowText, palette().color(QPalette::Disabled, QPalette::Text));
	ui->viewsInfoLabel->setPalette(infoPalette);

	connect(ui->viewsTree, &QTreeWidget::itemChanged, this, [this]() { updateOkEnabled(); });
	connect(ui->selectAllButton, &QPushButton::clicked, this, [this]() { setAllViewsChecked(true); });
	connect(ui->selectNoneButton, &QPushButton::clicked, this, [this]() { setAllViewsChecked(false); });
	ui->tabWidget->setTabEnabled(1, !views.isEmpty());

	// the measurements and annotations, grouped; all ticked at first
	QTreeWidgetItem* overlayGroup = nullptr;
	QString overlayGroupName;
	for (int i = 0; i < overlays.size(); ++i)
	{
		const OverlayItem& overlay = overlays[i];
		if (!overlayGroup || overlay.group != overlayGroupName)
		{
			overlayGroup = new QTreeWidgetItem(ui->overlayTree, QStringList{overlay.group});
			overlayGroup->setFlags(overlayGroup->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
			overlayGroup->setCheckState(0, Qt::Checked);
			QFont f = overlayGroup->font(0);
			f.setBold(true);
			overlayGroup->setFont(0, f);
			overlayGroupName = overlay.group;
		}
		auto* item = new QTreeWidgetItem(overlayGroup, QStringList{overlay.name});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Checked);
		item->setData(0, kViewIndexRole, i);
	}
	ui->overlayTree->expandAll();
	connect(ui->overlayAllButton, &QPushButton::clicked, this, [this]() { setAllOverlaysChecked(true); });
	connect(ui->overlayNoneButton, &QPushButton::clicked, this, [this]() { setAllOverlaysChecked(false); });
	ui->tabWidget->setTabEnabled(2, !overlays.isEmpty());

	// the animations: "No animation" first and selected, then one row per clip
	auto* none = new QListWidgetItem(tr("No animation"), ui->animationList);
	none->setData(kViewIndexRole, -1);
	for (int i = 0; i < animations.size(); ++i)
	{
		auto* item = new QListWidgetItem(tr("%1 (%2 s)").arg(animations[i].name).arg(animations[i].durationSeconds, 0, 'f', 1), ui->animationList);
		item->setData(kViewIndexRole, i);
	}
	ui->animationList->setCurrentRow(0);
	ui->playbackCombo->setCurrentIndex(qBound(0, settings.value(QStringLiteral("pdf3d/animationPlayback"), 0).toInt(), 2));
	ui->autoStartCheck->setChecked(settings.value(QStringLiteral("pdf3d/animationAutoStart"), true).toBool());
	// the tab stays available even without clips, saying why there is nothing to pick
	if (animations.isEmpty())
		ui->animationIntroLabel->setText(tr("This model has no animation that moves parts. Make one in the Animations panel, or with Create Animation in the Exploded View panel, and it will be listed here."));
	ui->playbackCombo->setEnabled(!animations.isEmpty());
	ui->autoStartCheck->setEnabled(!animations.isEmpty());
	updateOkEnabled();
}

Pdf3DExportDialog::~Pdf3DExportDialog() = default;

QString Pdf3DExportDialog::currentTemplateId() const
{
	const int i = ui->templateCombo->currentIndex();
	return (i >= 0 && i < _templates.size()) ? _templates[i].id : QString();
}

void Pdf3DExportDialog::reloadTemplates(const QString& selectId)
{
	_templates = Pdf3D::builtInSheetTemplates();
	_templateProblems.clear();
	_templates += Pdf3D::loadUserSheetTemplates(Pdf3D::userSheetTemplateFolder(), &_templateProblems);
	{
		const QSignalBlocker blocker(ui->templateCombo);
		ui->templateCombo->clear();
		for (const Pdf3D::SheetTemplate& t : _templates)
			ui->templateCombo->addItem(t.name, t.id);
		int index = ui->templateCombo->findData(selectId);
		ui->templateCombo->setCurrentIndex(index >= 0 ? index : 0);
	}
	applyTemplateSelection();
}

// The combo's text was edited: a name of an existing template selects it, a new name saves a copy of the current template under it.
void Pdf3DExportDialog::templateNameEntered()
{
	const QString name = ui->templateCombo->currentText().trimmed();
	const int current = ui->templateCombo->currentIndex();
	if (name.isEmpty() || current < 0 || current >= _templates.size())
	{
		if (current >= 0 && current < _templates.size())
			ui->templateCombo->setEditText(_templates[current].name);
		return;
	}
	for (int i = 0; i < _templates.size(); ++i)
	{
		if (_templates[i].name.compare(name, Qt::CaseInsensitive) == 0)
		{
			ui->templateCombo->setCurrentIndex(i);   // the name of an existing template: select it
			ui->templateCombo->setEditText(_templates[i].name);
			return;
		}
	}
	QString error;
	const QString id = Pdf3D::saveSheetTemplateAs(_templates[current], name, Pdf3D::userSheetTemplateFolder(), &error);
	if (id.isEmpty())
	{
		QMessageBox::warning(this, tr("Sheet template"), error);
		ui->templateCombo->setEditText(_templates[current].name);
		return;
	}
	reloadTemplates(id);
}

// Logo...: pick a file; a built-in template is first saved as a copy of the user's, then the logo is added and the template saved.
void Pdf3DExportDialog::chooseLogo()
{
	const int current = ui->templateCombo->currentIndex();
	if (current < 0 || current >= _templates.size())
		return;
	const QString file = QFileDialog::getOpenFileName(this, tr("Logo"), QString(), tr("Images (*.png *.jpg *.jpeg *.svg)"));
	if (file.isEmpty())
		return;

	const QString folder = Pdf3D::userSheetTemplateFolder();
	Pdf3D::SheetTemplate t = _templates[current];
	QString error;
	if (!t.id.startsWith(QLatin1String("user:")))
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Save as a template"),
			tr("A built-in template cannot be changed. Name for your copy with the logo:"), QLineEdit::Normal, tr("My %1").arg(t.name), &ok).trimmed();
		if (!ok || name.isEmpty())
			return;
		const QString id = Pdf3D::saveSheetTemplateAs(t, name, folder, &error);
		if (id.isEmpty())
		{
			QMessageBox::warning(this, tr("Sheet template"), error);
			return;
		}
		t = Pdf3D::findSheetTemplate(id);
		if (t.id != id)
		{
			QMessageBox::warning(this, tr("Sheet template"), tr("The new template could not be read back."));
			return;
		}
	}
	if (!Pdf3D::setSheetTemplateLogo(&t, file, folder, &error) || Pdf3D::writeUserSheetTemplate(t, folder, &error).isEmpty())
	{
		QMessageBox::warning(this, tr("Sheet template"), tr("The logo could not be added: %1").arg(error));
		reloadTemplates(t.id.startsWith(QLatin1String("user:")) ? t.id : currentTemplateId());   // a copy made above stays and is listed
		return;
	}
	reloadTemplates(t.id);
}

void Pdf3DExportDialog::applyTemplateSelection()
{
	const int index = ui->templateCombo->currentIndex();
	if (index < 0 || index >= _templates.size())
		return;
	const Pdf3D::SheetTemplate& t = _templates[index];

	// the title block fields the template does not use are not asked for (the title is always asked: it is also the document title)
	const QSet<QString> used = Pdf3D::usedSheetPlaceholders(t);
	const struct { const char* name; QWidget* label; QWidget* edit; } rows[] = {
		{ "drawingNumber", ui->drawingNumberLabel, ui->drawingNumberEdit }, { "revision", ui->revisionLabel, ui->revisionEdit },
		{ "author", ui->authorLabel, ui->authorEdit }, { "company", ui->companyLabel, ui->companyEdit },
		{ "scale", ui->scaleLabel, ui->scaleEdit }, { "notes", ui->notesLabel, ui->notesEdit } };
	for (const auto& r : rows)
	{
		const bool show = used.contains(QLatin1String(r.name));
		r.label->setVisible(show);
		r.edit->setVisible(show);
	}

	// the template's own fields, below the standard ones; the last values used with this template come back
	while (ui->formLayout->rowCount() > kFixedFormRows)
		ui->formLayout->removeRow(ui->formLayout->rowCount() - 1);
	_customFields.clear();
	const QSettings settings;
	const QLocale locale = appLocale();
	for (const Pdf3D::SheetField& f : t.fields)
	{
		const QString stored = settings.value(QStringLiteral("pdf3d/templates/%1/%2").arg(t.id, f.id), f.defaultValue).toString();
		QWidget* editor = nullptr;
		if (f.type == QLatin1String("multiline"))
		{
			auto* e = new QPlainTextEdit;
			e->setMinimumHeight(60);
			e->setPlainText(stored);
			editor = e;
		}
		else if (f.type == QLatin1String("date"))
		{
			const QDate fallback = QDate::fromString(f.defaultValue, Qt::ISODate);
			auto* e = new QDateEdit(fallback.isValid() ? fallback : QDate::currentDate());
			e->setCalendarPopup(true);
			e->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
			editor = e;
		}
		else if (f.type == QLatin1String("choice"))
		{
			auto* e = new QComboBox;
			e->addItems(f.options);
			const int i = e->findText(stored);
			if (i >= 0)
				e->setCurrentIndex(i);
			editor = e;
		}
		else if (f.type == QLatin1String("number"))
		{
			// a spin box in the language's style; what was used last time comes back (empty stays empty), the first time the default does
			const QString key = QStringLiteral("pdf3d/templates/%1/%2").arg(t.id, f.id);
			auto* e = new SheetNumberBox(f, locale);
			double remembered = 0.0;
			if (Pdf3D::parseSheetNumber(settings.contains(key) ? stored : f.defaultValue, QLocale::c(), &remembered)
				|| (settings.contains(key) && Pdf3D::parseSheetNumber(stored, locale, &remembered)))
				e->setValue(std::clamp(remembered, f.minimum, f.maximum));
			e->setToolTip(tr("A number. Leave it empty to print nothing; %1 decimals%2.").arg(f.decimals).arg(f.unit.isEmpty() ? QString() : tr(", unit %1").arg(f.unit)));
			editor = e;
		}
		else
		{
			editor = new QLineEdit(stored);
		}
		ui->formLayout->addRow(f.label + QLatin1Char(':'), editor);
		_customFields.insert(f.id, editor);
		if (const auto* line = qobject_cast<QLineEdit*>(editor))
			connect(line, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
		else if (const auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
			connect(spin, &QDoubleSpinBox::valueChanged, this, [this]() { updatePreview(); });
		else if (const auto* plain = qobject_cast<QPlainTextEdit*>(editor))
			connect(plain, &QPlainTextEdit::textChanged, this, [this]() { updatePreview(); });
		else if (const auto* combo = qobject_cast<QComboBox*>(editor))
			connect(combo, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });
		else if (const auto* date = qobject_cast<QDateEdit*>(editor))
			connect(date, &QDateEdit::dateChanged, this, [this]() { updatePreview(); });
	}

	const bool isUser = t.id.startsWith(QLatin1String("user:"));
	ui->editTemplateButton->setEnabled(true);   // a built-in template can be edited too, then saved under a new name
	const QSizeF pagePt = Pdf3D::sheetPageSizePt(t);
	QString info = tr("%1 template. Page %2 x %3 mm, %4.").arg(isUser ? tr("Your") : tr("Built-in"))
		.arg(qRound(pagePt.width() * 25.4 / 72.0)).arg(qRound(pagePt.height() * 25.4 / 72.0))
		.arg(t.landscape ? tr("landscape") : tr("portrait"));
	if (!_templateProblems.isEmpty())
		info += QLatin1Char('\n') + tr("In the templates folder:") + QLatin1Char('\n') + _templateProblems.join(QLatin1Char('\n'));
	ui->templateInfoLabel->setText(info);
	QPalette infoPalette = ui->templateInfoLabel->palette();
	infoPalette.setColor(QPalette::WindowText, palette().color(QPalette::Disabled, QPalette::Text));
	ui->templateInfoLabel->setPalette(infoPalette);
	updatePreview();
}

void Pdf3DExportDialog::setSheetSample(int partCount, const QString& subtitle, const QString& material, const QString& mass)
{
	_sampleParts = partCount;
	_sampleSubtitle = subtitle;
	_sampleMaterial = material;
	_sampleMass = mass;
	updatePreview();
}

QMap<QString, QString> Pdf3DExportDialog::currentSheetValues() const
{
	Pdf3D::Pdf3DOptions options;
	applyTo(options);
	options.subtitle = _sampleSubtitle;
	return Pdf3D::sheetValues(options, _sampleParts, 1, 1);
}

void Pdf3DExportDialog::updatePreview()
{
	const int index = ui->templateCombo->currentIndex();
	if (!_preview || index < 0 || index >= _templates.size())
		return;
	_preview->setTemplate(_templates[index]);
	_preview->setValues(currentSheetValues());
}

// Edit...: the cell editor with a live preview; a saved template is listed again and selected.
void Pdf3DExportDialog::editTemplate()
{
	const int index = ui->templateCombo->currentIndex();
	if (index < 0 || index >= _templates.size())
		return;
	Pdf3D::SheetTemplateEditor editor(_templates[index], currentSheetValues(), Pdf3D::userSheetTemplateFolder(), appLocale(), this);
	if (editor.exec() == QDialog::Accepted && !editor.savedTemplateId().isEmpty())
		reloadTemplates(editor.savedTemplateId());
}

void Pdf3DExportDialog::applyTo(Pdf3D::Pdf3DOptions& options) const
{
	options.title = ui->titleEdit->text().trimmed();
	options.drawingNumber = ui->drawingNumberEdit->text().trimmed();
	options.revision = ui->revisionEdit->text().trimmed();
	options.author = ui->authorEdit->text().trimmed();
	options.company = ui->companyEdit->text().trimmed();
	options.scale = ui->scaleEdit->text().trimmed();
	options.notes = ui->notesEdit->toPlainText().trimmed();
	options.includePartsList = ui->partsListCheck->isChecked();
	options.viewButtons = ui->viewButtonsCheck->isChecked();
	options.templateId = currentTemplateId();
	options.sceneMaterial = _sampleMaterial;
	options.sceneMass = _sampleMass;
	options.fieldValues.clear();
	for (auto it = _customFields.constBegin(); it != _customFields.constEnd(); ++it)
		options.fieldValues.insert(it.key(), editorValue(it.value(), appLocale(), true));
}

QVector<bool> Pdf3DExportDialog::viewSelection() const
{
	QVector<bool> selection(_viewCount, false);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(0) == Qt::Checked);
	}
	return selection;
}

int Pdf3DExportDialog::selectedAnimation() const
{
	const QListWidgetItem* item = ui->animationList->currentItem();
	return item ? item->data(kViewIndexRole).toInt() : -1;
}

int Pdf3DExportDialog::playbackIndex() const
{
	return ui->playbackCombo->currentIndex();
}

bool Pdf3DExportDialog::animationAutoStart() const
{
	return ui->autoStartCheck->isChecked();
}

QVector<bool> Pdf3DExportDialog::viewOverlaySelection() const
{
	QVector<bool> selection(_viewCount, true);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(2) == Qt::Checked);
	}
	return selection;
}

QVector<bool> Pdf3DExportDialog::overlaySelection() const
{
	QVector<bool> selection(_overlayCount, false);
	for (QTreeWidgetItemIterator it(ui->overlayTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(0) == Qt::Checked);
	}
	return selection;
}

void Pdf3DExportDialog::setAllOverlaysChecked(bool checked)
{
	for (QTreeWidgetItemIterator it(ui->overlayTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
}

void Pdf3DExportDialog::setAllViewsChecked(bool checked)
{
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
}

void Pdf3DExportDialog::updateOkEnabled()
{
	// the PDF needs at least one view
	const QVector<bool> selection = viewSelection();
	bool any = false;
	for (const bool b : selection)
		any = any || b;
	if (QPushButton* ok = ui->buttonBox->button(QDialogButtonBox::Ok))
	{
		ok->setEnabled(any);
		ok->setToolTip(any ? QString() : tr("Tick at least one view."));
	}
}

void Pdf3DExportDialog::done(int result)
{
	QSettings settings;
	settings.setValue(QStringLiteral("pdf3d/dialogGeometry2"), saveGeometry());
	if (result == QDialog::Accepted)
	{
		settings.setValue(QStringLiteral("pdf3d/drawingNumber"), ui->drawingNumberEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/revision"), ui->revisionEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/author"), ui->authorEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/company"), ui->companyEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/scale"), ui->scaleEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/partsList"), ui->partsListCheck->isChecked());
		settings.setValue(QStringLiteral("pdf3d/viewButtons"), ui->viewButtonsCheck->isChecked());
		settings.setValue(QStringLiteral("pdf3d/template"), currentTemplateId());
		// the custom fields of the chosen template (a date is always today's unless the template says otherwise)
		for (auto it = _customFields.constBegin(); it != _customFields.constEnd(); ++it)
		{
			if (!qobject_cast<const QDateEdit*>(it.value()))
				settings.setValue(QStringLiteral("pdf3d/templates/%1/%2").arg(currentTemplateId(), it.key()), editorValue(it.value(), appLocale(), false));
		}
		// the view selection: forget what this list showed, then remember what is unticked in it (views of other documents stay)
		QStringList unchecked = settings.value(QStringLiteral("pdf3d/viewsUnchecked")).toStringList();
		QStringList noOverlays = settings.value(QStringLiteral("pdf3d/viewsWithoutMeasurements")).toStringList();
		for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
		{
			if (!(*it)->data(0, kViewIndexRole).isValid())
				continue;
			const QString key = (*it)->data(0, kViewKeyRole).toString();
			unchecked.removeAll(key);
			noOverlays.removeAll(key);
			if ((*it)->checkState(0) != Qt::Checked)
				unchecked.append(key);
			if ((*it)->checkState(2) != Qt::Checked)
				noOverlays.append(key);
		}
		settings.setValue(QStringLiteral("pdf3d/viewsUnchecked"), unchecked);
		settings.setValue(QStringLiteral("pdf3d/viewsWithoutMeasurements"), noOverlays);
		settings.setValue(QStringLiteral("pdf3d/animationPlayback"), ui->playbackCombo->currentIndex());
		settings.setValue(QStringLiteral("pdf3d/animationAutoStart"), ui->autoStartCheck->isChecked());
	}
	QDialog::done(result);
}
