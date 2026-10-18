#include "FloatingPanelDialog.h"

#include <QIcon>
#include <QMoveEvent>
#include <QPainter>
#include <QPixmap>
#include <QSizePolicy>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

FloatingPanelDialog::FloatingPanelDialog(QWidget* parent, const QString& title,
                                         Qt::WindowFlags extraFlags)
    : QDialog(parent, Qt::Window | extraFlags)
{
    setWindowTitle(title);
    setObjectName("floatingPanelDialog");
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowCloseButtonHint);

    // ---- main layout (toolbar + content) ----
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(6, 6, 6, 6);
    _mainLayout->setSpacing(0);

    // ---- thin toolbar strip ----
    _toolbar = new QWidget(this);
    _toolbar->setObjectName("floatingPanelToolbar");
    _toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    _toolbarLayout = new QHBoxLayout(_toolbar);
    _toolbarLayout->setContentsMargins(4, 2, 4, 2);
    _toolbarLayout->setSpacing(4);

    // Spacer pushes pin button to the right
    _toolbarLayout->addStretch();

    _reattachButton = new QToolButton(_toolbar);
    _reattachButton->setAutoRaise(true);
    _reattachButton->setToolTip(tr("Reattach to panel"));
    _reattachIcon = QIcon(QPixmap(":/icons/res/reattach.png"));
    _reattachButton->setIcon(_reattachIcon);
    connect(_reattachButton, &QToolButton::clicked,
            this, &FloatingPanelDialog::reattachRequested);
    _toolbarLayout->addWidget(_reattachButton);

    // Pin button -> lock panel position while floating
    _pinButton = new QToolButton(_toolbar);
    _pinButton->setCheckable(true);
    _pinButton->setChecked(false);
    _pinButton->setAutoRaise(true);
    _pinButton->setToolTip(tr("Lock panel position"));

    // Build a two-state icon: normal = pinned, off = unpinned
    _pinIcon   = QIcon(QPixmap(":/icons/res/pin.png"));
    _unpinIcon = QIcon(QPixmap(":/icons/res/unpin.png"));
    _pinButton->setIcon(_unpinIcon);

    // Swap icon and tooltip whenever the pin state changes
    connect(_pinButton, &QToolButton::toggled, this, [this](bool checked) {
        _pinButton->setIcon(checked ? _pinIcon : _unpinIcon);
        _pinButton->setToolTip(checked
            ? tr("Unlock panel position")
            : tr("Lock panel position"));
        if (checked)
            _lockedPosition = pos();
    });

    _toolbarLayout->addWidget(_pinButton);
    _mainLayout->addWidget(_toolbar);

    setContentTransparencyEnabled(false);
}

void FloatingPanelDialog::addContentWidget(QWidget* widget)
{
    _contentWidget = widget;

    // Content gets a 6 px margin and expands to fill all remaining space.
    _contentWrapper = new QWidget(this);
    _contentWrapper->setObjectName("floatingPanelContentWrapper");
    QVBoxLayout* wl  = new QVBoxLayout(_contentWrapper);
    wl->setContentsMargins(6, 4, 6, 6);
    wl->addWidget(widget);
    _mainLayout->addWidget(_contentWrapper, 1 /*stretch*/);
}

QWidget* FloatingPanelDialog::takeContentWidget()
{
    if (!_contentWidget)
        return nullptr;

    QWidget* widget = _contentWidget;
    _contentWidget = nullptr;

    if (_contentWrapper)
    {
        if (QLayout* layout = _contentWrapper->layout())
            layout->removeWidget(widget);
        _mainLayout->removeWidget(_contentWrapper);
        _contentWrapper->deleteLater();
        _contentWrapper = nullptr;
    }

    widget->setParent(nullptr);
    return widget;
}

void FloatingPanelDialog::setContentTransparencyEnabled(bool enabled)
{
    _contentTransparencyEnabled = enabled;
    setAttribute(Qt::WA_TranslucentBackground, enabled);

    if (!enabled)
    {
        setStyleSheet(QString());
        update();
        return;
    }

    setStyleSheet(
        "QDialog#floatingPanelDialog {"
        "  background: transparent;"
        "}"
        "QWidget#floatingPanelToolbar {"
        "  background-color: rgba(24, 24, 24, 160);"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "}"
        "QWidget#floatingPanelContentWrapper {"
        "  background-color: rgba(24, 24, 24, 128);"
        "  border-bottom-left-radius: 6px;"
        "  border-bottom-right-radius: 6px;"
        "}"
        "QWidget#floatingPanelContentWrapper QTreeWidget {"
        "  background-color: rgba(24, 24, 24, 96);"
        "  alternate-background-color: rgba(40, 40, 40, 96);"
        "}"
        "QWidget#floatingPanelContentWrapper QTreeWidget::item {"
        "  background: transparent;"
        "}"
        "QWidget#floatingPanelContentWrapper QHeaderView::section {"
        "  background-color: rgba(24, 24, 24, 160);"
        "}"
    );
    update();
}

void FloatingPanelDialog::paintEvent(QPaintEvent* event)
{
    if (_contentTransparencyEnabled)
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QColor(255, 255, 255, 32));
        painter.setBrush(QColor(18, 18, 18, 110));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    }

    QDialog::paintEvent(event);
}

void FloatingPanelDialog::moveEvent(QMoveEvent* event)
{
    QDialog::moveEvent(event);

    if (_restoringLockedPosition)
        return;

    if (!_pinButton->isChecked())
    {
        _lockedPosition = pos();
        return;
    }

    if (_lockedPosition.isNull())
    {
        _lockedPosition = event->oldPos();
        if (_lockedPosition.isNull())
            _lockedPosition = pos();
    }

    if (pos() == _lockedPosition)
        return;

    const QPoint targetPos = _lockedPosition;
    QTimer::singleShot(0, this, [this, targetPos]() {
        if (!_pinButton->isChecked())
            return;
        _restoringLockedPosition = true;
        move(targetPos);
        _restoringLockedPosition = false;
    });
}

#ifdef Q_OS_WIN
bool FloatingPanelDialog::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);

    if (_pinButton && _pinButton->isChecked() && message)
    {
        MSG* msg = static_cast<MSG*>(message);
        if ((msg->message == WM_NCLBUTTONDOWN && msg->wParam == HTCAPTION) ||
            (msg->message == WM_SYSCOMMAND && ((msg->wParam & 0xFFF0) == SC_MOVE)))
        {
            if (result)
                *result = 0;
            return true;
        }
    }

    return QDialog::nativeEvent(eventType, message, result);
}
#endif

bool FloatingPanelDialog::isPinned() const
{
    return _pinButton->isChecked();
}

void FloatingPanelDialog::reject()
{
    return;
}


