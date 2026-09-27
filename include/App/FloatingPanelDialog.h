#pragma once


#include <QDialog>
#include <QIcon>
#include <QMoveEvent>
#include <QPoint>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

/**
 * A floating tool dialog with a pinnable state.
 *
 * When pinned (default), pressing Escape does nothing — the panel stays open.
 * When unpinned, Escape closes the dialog normally.
 * The X (close) button always closes regardless of pin state.
 *
 * Usage:
 *   auto* dlg = new FloatingPanelDialog(parent, tr("My Panel"));
 *   dlg->addContentWidget(myWidget);
 *   connect(dlg, &QDialog::finished, this, &MyClass::onPanelClosed);
 *   dlg->show();
 */
class FloatingPanelDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FloatingPanelDialog(QWidget* parent, const QString& title,
                                 Qt::WindowFlags extraFlags = Qt::Tool);

    /**
     * Add the main content widget.  It is placed below the pin toolbar
     * and expands to fill the remaining space.
     */
    void addContentWidget(QWidget* widget);

    /**
     * Remove and return the content widget previously added via addContentWidget.
     * Returns nullptr if no content widget is currently attached.
     */
    QWidget* takeContentWidget();
    void setContentTransparencyEnabled(bool enabled);

    bool isPinned() const;

signals:
    void reattachRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    /**
     * Overridden so that Escape does not close the dialog when pinned.
     * The X button goes through closeEvent, not reject(), so it is unaffected.
     */
    void reject() override;

private:
    QVBoxLayout* _mainLayout;
    QHBoxLayout* _toolbarLayout;
    QWidget*     _toolbar;
    QToolButton* _reattachButton;
    QToolButton* _pinButton;
    QIcon        _reattachIcon;
    QIcon        _pinIcon;
    QIcon        _unpinIcon;
    QWidget*     _contentWrapper = nullptr;
    QWidget*     _contentWidget = nullptr;
    bool         _contentTransparencyEnabled = false;
    QPoint       _lockedPosition;
    bool         _restoringLockedPosition = false;
};
