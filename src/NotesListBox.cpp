#include "NotesListBox.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{
	// A plain-text tooltip does not word-wrap in Qt (rich text does) - long enough and it renders as one very wide
	// single line. Every entry's row already wraps in the list itself (that's this whole widget's point), but its
	// tooltip is set separately from `full`, which is plain text - wrap it as rich text so it wraps too. `\n` (the
	// multi-subject case's one-name-per-line list) becomes <br>, everything else is HTML-escaped first so a mesh
	// name containing '<'/'&' can't break the markup.
	QString toRichTooltip(const QString& plain)
	{
		return QStringLiteral("<html><body><p>%1</p></body></html>")
			.arg(plain.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
	}
}

NotesListBox::NotesListBox(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	_title = new QLabel(this);
	QFont titleFont = _title->font();
	titleFont.setBold(true);
	_title->setFont(titleFont);
	layout->addWidget(_title);

	_list = new QListWidget(this);
	_list->setWordWrap(true);
	_list->setIconSize(QSize(16, 16));
	_list->setTextElideMode(Qt::ElideNone);
	_list->setSelectionMode(QAbstractItemView::NoSelection);
	_list->setFocusPolicy(Qt::NoFocus);
	_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	_list->setUniformItemSizes(false);
	_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu(this);
		connect(menu.addAction(tr("Copy All")), &QAction::triggered, this, &NotesListBox::copyAll);
		menu.exec(_list->viewport()->mapToGlobal(pos));
	});
	layout->addWidget(_list);

	setVisible(false);
}

void NotesListBox::setMaximumListHeight(int pixels)
{
	_maxListHeight = std::max(40, pixels);
	refit();
}

int NotesListBox::rowCount() const
{
	return _list->count();
}

void NotesListBox::setNotes(const QVector<Note>& notes)
{
	_list->clear();
	_plainTexts.clear();

	// Group notes that share a message (and severity): same wording for many meshes becomes one row.
	struct Group
	{
		QString message;
		Severity severity;
		QStringList subjects;
	};
	QVector<Group> groups;
	for (const Note& note : notes)
	{
		if (note.message.isEmpty())
			continue;
		Group* found = nullptr;
		for (Group& group : groups)
		{
			if (group.message == note.message && group.severity == note.severity)
			{
				found = &group;
				break;
			}
		}
		if (!found)
		{
			groups.append({ note.message, note.severity, QStringList() });
			found = &groups.last();
		}
		if (!note.subject.isEmpty() && !found->subjects.contains(note.subject))
			found->subjects.append(note.subject);
	}

	constexpr int kMaxNamesInTooltip = 30;
	for (const Group& group : std::as_const(groups))
	{
		QString text;
		QString full;
		if (group.subjects.isEmpty())
		{
			text = group.message;
			full = group.message;
		}
		else if (group.subjects.size() == 1)
		{
			text = tr("%1: %2").arg(group.subjects.first(), group.message);
			full = text;
		}
		else
		{
			text = tr("%1 meshes: %2").arg(group.subjects.size()).arg(group.message);
			QStringList shown = group.subjects.mid(0, kMaxNamesInTooltip);
			if (group.subjects.size() > kMaxNamesInTooltip)
				shown << QStringLiteral("...");
			full = text + QLatin1Char('\n') + shown.join(QLatin1Char('\n'));
		}

		auto* item = new QListWidgetItem(
			style()->standardIcon(group.severity == Severity::Warning ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxInformation),
			text, _list);
		item->setToolTip(toRichTooltip(full));
		_plainTexts.append(full);
	}

	_title->setText(tr("Notes (%1)").arg(_list->count()));
	setVisible(_list->count() > 0);
	refit();
}

void NotesListBox::refit()
{
	if (_list->count() == 0)
		return;

	// Word-wrapped rows need an explicit height: measure each text at the width the list will really have (its
	// viewport less the icon and the row padding), twice, because the vertical scrollbar appearing narrows it.
	const QFontMetrics metrics(_list->font());
	const int iconWidth = _list->iconSize().width() + 12;
	int viewportWidth = _list->viewport()->width();
	if (viewportWidth < 80)
		viewportWidth = std::max(80, width() - 4);

	int total = 0;
	for (int pass = 0; pass < 2; ++pass)
	{
		total = 0;
		const int textWidth = std::max(60, viewportWidth - iconWidth - 8);
		for (int i = 0; i < _list->count(); ++i)
		{
			QListWidgetItem* item = _list->item(i);
			const QRect bounds = metrics.boundingRect(QRect(0, 0, textWidth, 100000), Qt::TextWordWrap | Qt::AlignLeft, item->text());
			const int rowHeight = std::max(_list->iconSize().height(), bounds.height()) + 8;
			item->setSizeHint(QSize(viewportWidth, rowHeight));
			total += rowHeight;
		}
		const int frame = _list->frameWidth() * 2;
		const bool needsScroll = total + frame > _maxListHeight;
		_list->setFixedHeight(std::min(total + frame, _maxListHeight));
		const int scrollWidth = needsScroll ? _list->style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
		const int newViewportWidth = std::max(80, width() - _list->frameWidth() * 2 - scrollWidth);
		if (newViewportWidth == viewportWidth)
			break;
		viewportWidth = newViewportWidth;
	}
}

void NotesListBox::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (event->size().width() != event->oldSize().width())
		refit();
}

void NotesListBox::copyAll()
{
	QApplication::clipboard()->setText(_plainTexts.join(QStringLiteral("\n\n")));
}
