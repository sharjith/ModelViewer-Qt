#include "DialogLayoutHelpers.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
	// The placeholder label lives on the view's viewport and follows its size and its row count.
	//
	// Parented to the VIEWPORT, not the view itself: QAbstractScrollArea's destructor deletes its viewport outright,
	// ahead of the view's own QObject children (its model among them) being torn down by the normal end-of-~QObject
	// child cleanup. Parenting here means this hint is destroyed in that same early step, before the model's
	// destructor can emit a rows-removed signal into a lambda that would otherwise reach back into a
	// half-destroyed view/viewport and crash. (_view is a QPointer as a second line of defence, in case a future
	// caller reparents things differently.)
	class EmptyViewHint : public QObject
	{
	public:
		EmptyViewHint(QAbstractItemView* view, const QString& text)
			: QObject(view->viewport()), _view(view)
		{
			_label = new QLabel(text, view->viewport());
			_label->setWordWrap(true);
			_label->setAlignment(Qt::AlignCenter);
			_label->setAttribute(Qt::WA_TransparentForMouseEvents);
			QPalette palette = _label->palette();
			palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
			_label->setPalette(palette);

			view->viewport()->installEventFilter(this);
			if (QAbstractItemModel* model = view->model())
			{
				connect(model, &QAbstractItemModel::rowsInserted, this, [this]() { refresh(); });
				connect(model, &QAbstractItemModel::rowsRemoved, this, [this]() { refresh(); });
				connect(model, &QAbstractItemModel::modelReset, this, [this]() { refresh(); });
				connect(model, &QAbstractItemModel::layoutChanged, this, [this]() { refresh(); });
			}
			refresh();
		}

		bool eventFilter(QObject* watched, QEvent* event) override
		{
			if (_view && watched == _view->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
				reposition();
			return false;
		}

	private:
		void refresh()
		{
			if (!_view || !_label)
				return;
			const QAbstractItemModel* model = _view->model();
			const bool empty = !model || model->rowCount(_view->rootIndex()) == 0;
			_label->setVisible(empty);
			reposition();
		}

		void reposition()
		{
			if (!_view || !_label)
				return;
			_label->setGeometry(_view->viewport()->rect().adjusted(12, 8, -12, -8));
		}

		QPointer<QAbstractItemView> _view;
		QPointer<QLabel> _label;
	};
}

void DialogLayout::keepNaturalHeight(QLabel* label)
{
	if (label)
		label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

void DialogLayout::makeCentredHint(QLabel* label)
{
	if (!label)
		return;
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignCenter);
	label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void DialogLayout::pinActionToBottom(QVBoxLayout* layout, QLabel* statusLabel, QWidget* actionButton, bool packAtTop)
{
	if (!layout || !statusLabel || !actionButton)
		return;
	keepNaturalHeight(statusLabel);
	layout->removeWidget(statusLabel);
	layout->removeWidget(actionButton);
	if (packAtTop)
		layout->addStretch(1);
	layout->addWidget(statusLabel);
	layout->addWidget(actionButton);
}

void DialogLayout::attachEmptyHint(QAbstractItemView* view, const QString& text)
{
	if (view)
		new EmptyViewHint(view, text); // owned by the view
}
