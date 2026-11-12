#include "Pdf3DExportDialog.h"
#include "ui_Pdf3DExportDialog.h"

#include "Pdf3DExporter.h"
#include "SheetPreviewWidget.h"
#include "SheetTemplateEditor.h"
#include "SheetRenderer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
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
#include <QVBoxLayout>

namespace
{
	constexpr int kViewIndexRole = Qt::UserRole;       // the index into the caller's ViewItem list, on the leaf items
	constexpr int kViewWarningRole = Qt::UserRole + 2;  // a limitation of an included view
	constexpr int kViewKeyRole = Qt::UserRole + 1;     // "group|name" of a view, to remember its tick state
}

Pdf3DExportDialog::Pdf3DExportDialog(const QString& defaultTitle, const QVector<ViewItem>& views, const QString& viewsInfo,
	const QVector<OverlayItem>& overlays, const QVector<AnimationItem>& animations,
	const GeometrySummary& geometry, bool openAfterSaving, int companionCount, bool hasCapturedVisibility, QWidget* parent)
	: QDialog(parent)
	, ui(std::make_unique<Ui::Pdf3DExportDialog>())
	, _viewCount(views.size())
	, _overlayCount(overlays.size())
	, _geometry(geometry)
{
	ui->setupUi(this);
	auto* destinationRow = new QHBoxLayout;
	auto* destinationLabel = new QLabel(tr("Save &to:"), this);
	_destination = new QLineEdit(this);
	_destination->setAccessibleName(tr("Save to"));
	destinationLabel->setBuddy(_destination);
	auto* browse = new QPushButton(tr("Browse..."), this);
	browse->setAutoDefault(false);
	destinationRow->addWidget(destinationLabel);
	destinationRow->addWidget(_destination, 1);
	destinationRow->addWidget(browse);
	ui->verticalLayout->insertLayout(ui->verticalLayout->indexOf(ui->buttonBox), destinationRow);
	ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Save 3D PDF"));
	connect(browse, &QPushButton::clicked, this, &Pdf3DExportDialog::browseOutputPath);
	connect(_destination, &QLineEdit::textChanged, this, &Pdf3DExportDialog::updateSaveButton);
	ui->capturedVisibilityCheck->setVisible(hasCapturedVisibility && !overlays.isEmpty());
	_openAfterSaving = new QCheckBox(tr("Open 3D PDF after saving"), this);
	_openAfterSaving->setChecked(openAfterSaving);
	ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->buttonBox), _openAfterSaving);

	auto* review = new QWidget(ui->tabWidget);
	auto* reviewLayout = new QVBoxLayout(review);
	_reviewSummary = new QLabel(review);
	_reviewSummary->setTextFormat(Qt::PlainText);
	_reviewSummary->setWordWrap(true);
	_reviewSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
	reviewLayout->addWidget(_reviewSummary);

	auto* openingForm = new QFormLayout;
	_openingView = new QComboBox(review);
	_openingView->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	_openingView->setMinimumContentsLength(28);
	openingForm->addRow(tr("Opening 3D view:"), _openingView);
	connect(_openingView, &QComboBox::currentIndexChanged, this, &Pdf3DExportDialog::updateCoverPreview);
	reviewLayout->addLayout(openingForm);
	auto* coverNote = new QLabel(tr("The cover uses the selected saved camera view when its clipping is supported. Other opening views use the current viewport for the cover."), review);
	coverNote->setWordWrap(true);
	reviewLayout->addWidget(coverNote);
	if (companionCount > 0)
	{
		auto* note = new QLabel(tr("Includes %n static sheet(s) in the Publish Sheets order.", nullptr, companionCount), review);
		note->setWordWrap(true);
		reviewLayout->addWidget(note);
	}
	reviewLayout->addStretch();
	ui->tabWidget->addTab(review, tr("Review"));
	_reviewNotice = new QPushButton(tr("Review export limitations"), this);
	_reviewNotice->setAutoDefault(false);
	ui->verticalLayout->insertWidget(0, _reviewNotice, 0, Qt::AlignLeft);
	connect(_reviewNotice, &QPushButton::clicked, this, [this, review] { ui->tabWidget->setCurrentWidget(review); });

	QSettings settings;
	restoreGeometry(settings.value(QStringLiteral("pdf3d/dialogGeometry2")).toByteArray());
	ui->titleEdit->setText(defaultTitle);
	// Drawing number and revision belong to one drawing, so they start empty for each model (setDeclaredDrawingNumber() fills the number when the
	// model declares one); author, company and scale are the user's and are remembered.
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
		item->setData(0, kViewWarningRole, view.warning);
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

	// Group overlays and use viewer visibility as the initial selection.
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
		item->setCheckState(0, overlay.initiallyChecked ? Qt::Checked : Qt::Unchecked);
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
	connect(ui->overlayTree, &QTreeWidget::itemChanged, this, [this] { updateReview(); });
	connect(ui->animationList, &QListWidget::currentRowChanged, this, [this] { updateReview(); });
	connect(ui->partsListCheck, &QCheckBox::toggled, this, [this] { updateReview(); });
	connect(ui->viewButtonsCheck, &QCheckBox::toggled, this, [this] { updateReview(); });
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

	_customFields.rebuild(*ui->formLayout, t, this, [this] { updatePreview(); });

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

void Pdf3DExportDialog::setDeclaredDrawingNumber(const QString& number)
{
	if (number.isEmpty())
		return;
	ui->drawingNumberEdit->setText(number);
	ui->drawingNumberEdit->setToolTip(tr("Taken from the model. You can change it."));
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

void Pdf3DExportDialog::setCoverPreviewProvider(std::function<QImage(int)> provider)
{
	_coverPreviewProvider = std::move(provider);
	updateCoverPreview();
}

void Pdf3DExportDialog::updateCoverPreview()
{
	if (!_coverPreviewProvider)
		return;
	const int selected = _openingView->currentIndex();
	const int index = _openingView->count() > 1 ? _openingView->itemData(qMax(1, selected)).toInt() : -1;
	if (index == _previewView)
		return;
	_previewView = index;
	_coverPreview = _coverPreviewProvider(index);
	_preview->setPoster(_coverPreview);
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
	Pdf3D::SheetTemplateEditor editor(_templates[index], currentSheetValues(), Pdf3D::userSheetTemplateFolder(), Pdf3D::sheetFieldLocale(), this);
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
	options.fieldValues = _customFields.values();
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

int Pdf3DExportDialog::openingViewIndex() const
{
	// The first combo item means "first included view". The remaining entries
	// are the checked views in the same order the exporter emits them.
	return qMax(0, _openingView->currentIndex() - 1);
}

bool Pdf3DExportDialog::openAfterSaving() const
{
	return _openAfterSaving->isChecked();
}

bool Pdf3DExportDialog::useCapturedOverlayVisibility() const
{
	return ui->capturedVisibilityCheck->isChecked();
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
	const QSignalBlocker blocker(ui->overlayTree);
	for (QTreeWidgetItemIterator it(ui->overlayTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
	updateReview();
}

void Pdf3DExportDialog::setAllViewsChecked(bool checked)
{
	const QSignalBlocker blocker(ui->viewsTree);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
	updateOkEnabled();
}

void Pdf3DExportDialog::updateOkEnabled()
{
	const QVariant oldSelection = _openingView->currentData();
	const QSignalBlocker blocker(_openingView);
	_openingView->clear();
	_openingView->addItem(tr("First included view"), -1);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		const auto* item = *it;
		const QVariant index = item->data(0, kViewIndexRole);
		if (index.isValid() && item->checkState(0) == Qt::Checked)
			_openingView->addItem(tr("%1 (%2)").arg(item->text(0), item->parent()->text(0)), index);
	}
	_openingView->setCurrentIndex(qMax(0, _openingView->findData(oldSelection)));
	const bool any = _openingView->count() > 1;
	_openingView->setEnabled(any);
	updateSaveButton();
	updateCoverPreview();
	updateReview();
}

void Pdf3DExportDialog::setSuggestedOutputPath(const QString& path)
{
	const QFileInfo info(path);
	_suggestedFolder = info.absolutePath();
	_destination->setText(QDir::toNativeSeparators(info.absoluteFilePath()));
	_destination->setCursorPosition(0);
}

QString Pdf3DExportDialog::enteredOutputPath() const
{
	const QString entered = QDir::fromNativeSeparators(_destination->text().trimmed());
	if (entered.isEmpty() || entered.endsWith(QLatin1Char('/')))
		return {};
	QString path = QDir(_suggestedFolder).absoluteFilePath(entered);
	const QFileInfo info(path);
	if (info.isDir() || info.fileName() == QLatin1String(".") || info.fileName() == QLatin1String(".."))
		return {};
	if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
		path += QStringLiteral(".pdf");
	return QDir::cleanPath(path);
}

void Pdf3DExportDialog::browseOutputPath()
{
	const QString current = enteredOutputPath();
	const QString path = QFileDialog::getSaveFileName(this, tr("Choose 3D PDF Destination"),
		current.isEmpty() ? _suggestedFolder : current, tr("PDF files (*.pdf)"), nullptr, QFileDialog::DontConfirmOverwrite);
	if (!path.isEmpty())
		_destination->setText(QDir::toNativeSeparators(path));
}

void Pdf3DExportDialog::updateSaveButton()
{
	auto* save = ui->buttonBox->button(QDialogButtonBox::Ok);
	const bool hasView = _openingView->count() > 1;
	const bool hasPath = !_destination->text().trimmed().isEmpty();
	save->setEnabled(hasView && hasPath);
	if (!hasView)
		save->setToolTip(tr("Select at least one view."));
	else
		save->setToolTip(hasPath ? QString() : tr("Enter a destination for the PDF."));
}

void Pdf3DExportDialog::updateReview()
{
	const QLocale locale;
	QStringList lines;
	lines << tr("Visible geometry: %1 mesh instances, %2 mesh resources, %3 triangles across those resources.")
		.arg(locale.toString(qlonglong(_geometry.meshInstances)), locale.toString(qlonglong(_geometry.meshResources)),
		     locale.toString(_geometry.triangles));

	qsizetype overlays = 0;
	for (const bool enabled : overlaySelection())
		overlays += enabled;
	lines << tr("Included: %1 views, %2 measurements or annotations.").arg(_openingView->count() - 1).arg(overlays);
	lines << (ui->partsListCheck->isChecked() ? tr("Parts list: included.") : tr("Parts list: off."));
	lines << (ui->viewButtonsCheck->isChecked() ? tr("View buttons: on.") : tr("View buttons: off."));
	lines << tr("Animation: %1").arg(ui->animationList->currentItem() ? ui->animationList->currentItem()->text() : tr("None"));

	QStringList limitations;
	if (_geometry.omitsWireGeometry)
		limitations << tr("Lines and points appear only in the cover image if visible there.");
	if (_geometry.omitsTextures)
		limitations << tr("The interactive model uses material colors. Texture maps appear only in the cover image if visible there.");
	if (_geometry.meshInstances >= 5000 || _geometry.triangles >= 500000)
		limitations << tr("This is a large export. Creating and opening the interactive model may take longer.");
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		if ((*it)->checkState(0) == Qt::Checked && (*it)->data(0, kViewWarningRole).toBool())
			limitations << (*it)->text(1);
	}
	limitations.removeDuplicates();
	if (!limitations.isEmpty())
		lines << QString() << tr("Export limitations") << limitations;
	_reviewSummary->setText(lines.join(QLatin1Char('\n')));
	_reviewNotice->setVisible(!limitations.isEmpty());
}

void Pdf3DExportDialog::done(int result)
{
	if (result == QDialog::Accepted)
	{
		if (_openingView->count() < 2)
			return;
		const QString path = enteredOutputPath();
		const QFileInfo file(path);
		if (path.isEmpty() || !QDir(file.absolutePath()).exists() || (file.exists() && !file.isFile()))
		{
			QMessageBox::warning(this, windowTitle(), tr("Enter a PDF filename in an existing folder."));
			_destination->setFocus();
			return;
		}
		if ((file.exists() || file.isSymLink()) &&
			QMessageBox::question(this, tr("Replace PDF?"), tr("Replace the existing file “%1”?").arg(file.fileName()),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;
		_outputPath = path;
	}
	QSettings settings;
	settings.setValue(QStringLiteral("pdf3d/dialogGeometry2"), saveGeometry());
	if (result == QDialog::Accepted)
	{
		settings.remove(QStringLiteral("pdf3d/drawingNumber"));   // older versions remembered these two
		settings.remove(QStringLiteral("pdf3d/revision"));
		settings.setValue(QStringLiteral("pdf3d/author"), ui->authorEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/company"), ui->companyEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/scale"), ui->scaleEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/partsList"), ui->partsListCheck->isChecked());
		settings.setValue(QStringLiteral("pdf3d/viewButtons"), ui->viewButtonsCheck->isChecked());
		settings.setValue(QStringLiteral("pdf3d/template"), currentTemplateId());
		_customFields.save();
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
