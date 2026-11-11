#include "SheetFieldEditors.h"

#include "LanguageManager.h"
#include "SheetTemplate.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{

// The locale of the application's language: numbers are typed and printed in its style (the system's when no language is loaded).
QLocale sheetFieldLocale()
{
	const QString language = LanguageManager::instance().currentLanguage();
	return language.isEmpty() ? QLocale() : QLocale(language);
}

namespace
{

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

} // namespace

void SheetFieldEditors::rebuild(QFormLayout& form, const SheetTemplate& tpl, QWidget* context, const std::function<void()>& changed)
{
	for (QWidget* editor : _editors)
		form.removeRow(editor);
	_editors.clear();
	_templateId = tpl.id;
	const QSettings settings;
	const QLocale locale = sheetFieldLocale();
	for (const Pdf3D::SheetField& f : tpl.fields)
	{
		const QString key = QStringLiteral("pdf3d/templates/%1/%2").arg(tpl.id, f.id);
		const QString stored = settings.value(key, f.defaultValue).toString();
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
			auto* e = new SheetNumberBox(f, locale);
			double remembered = 0.0;
			if (Pdf3D::parseSheetNumber(settings.contains(key) ? stored : f.defaultValue, QLocale::c(), &remembered)
				|| (settings.contains(key) && Pdf3D::parseSheetNumber(stored, locale, &remembered)))
				e->setValue(std::clamp(remembered, f.minimum, f.maximum));
			const QString unit = f.unit.isEmpty() ? QString()
				: QCoreApplication::translate("Pdf3DExportDialog", ", unit %1").arg(f.unit);
			e->setToolTip(QCoreApplication::translate("Pdf3DExportDialog",
				"A number. Leave it empty to print nothing; %1 decimals%2.").arg(f.decimals).arg(unit));
			editor = e;
		}
		else
		{
			editor = new QLineEdit(stored);
		}
		form.addRow(f.label + QLatin1Char(':'), editor);
		_editors.insert(f.id, editor);
		if (auto* line = qobject_cast<QLineEdit*>(editor))
			QObject::connect(line, &QLineEdit::textChanged, context, changed);
		else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
			QObject::connect(spin, &QDoubleSpinBox::valueChanged, context, changed);
		else if (auto* plain = qobject_cast<QPlainTextEdit*>(editor))
			QObject::connect(plain, &QPlainTextEdit::textChanged, context, changed);
		else if (auto* combo = qobject_cast<QComboBox*>(editor))
			QObject::connect(combo, &QComboBox::currentIndexChanged, context, changed);
		else if (auto* date = qobject_cast<QDateEdit*>(editor))
			QObject::connect(date, &QDateEdit::dateChanged, context, changed);
	}
}

QMap<QString, QString> SheetFieldEditors::values() const
{
	const QLocale locale = sheetFieldLocale();
	QMap<QString, QString> result;
	for (auto it = _editors.constBegin(); it != _editors.constEnd(); ++it)
		result.insert(it.key(), editorValue(it.value(), locale, true));
	return result;
}

void SheetFieldEditors::save() const
{
	QSettings settings;
	const QLocale locale = sheetFieldLocale();
	for (auto it = _editors.constBegin(); it != _editors.constEnd(); ++it)
	{
		// Start from the template's date default (usually today) for each job.
		if (!qobject_cast<const QDateEdit*>(it.value()))
			settings.setValue(QStringLiteral("pdf3d/templates/%1/%2").arg(_templateId, it.key()), editorValue(it.value(), locale, false));
	}
}

} // namespace Pdf3D
