#include "ModelObjectList.h"
#include <QKeyEvent>

ModelObjectList::ModelObjectList(QWidget* parent) : QListWidget(parent)
{
}

void ModelObjectList::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Space)
	{
		QList<QListWidgetItem*> selectedItems = this->selectedItems();
		if (selectedItems.size())
		{
			for (QListWidgetItem* item : selectedItems)
			{
				item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
			}
		}
	}
	else
	{
		QListWidget::keyPressEvent(event);
	}
}