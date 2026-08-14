#pragma once


#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

class QuickHelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickHelpDialog(QWidget* parent = nullptr);
    ~QuickHelpDialog() override = default;

private:
    void setupUI();
    void setupHomeTab();
    QWidget* createSampleModelButton(QWidget* parent, const QString& iconResourcePath,
        const QString& label, const QString& relativeModelPath);
    void openSampleModel(const QString& relativeModelPath);
    void setupMouseControlsTab();
    void setupKeyboardShortcutsTab();
    void setupViewToolbarTab();
    void setupMenuShortcutsTab();
    void setupCameraModesTab();
    void setupDisplayModesTab();
    void setupAdvancedFeaturesTab();
    void setupTipsAndTricksTab();

    QString createStyledHtml(const QString& title, const QString& content);
    QString createSection(const QString& heading, const QString& content);
    QString createTable(const QStringList& headers, const QList<QStringList>& rows);

    QTabWidget* _tabWidget;
    QWidget* _homeTab;
    QTextBrowser* _mouseControlsBrowser;
    QTextBrowser* _keyboardBrowser;
    QTextBrowser* _toolbarBrowser;
    QTextBrowser* _menuBrowser;
    QTextBrowser* _cameraBrowser;
    QTextBrowser* _displayBrowser;
    QTextBrowser* _advancedBrowser;
    QTextBrowser* _tipsBrowser;
    QPushButton* _closeButton;
	QCheckBox* _showOnStartupCheckBox;
};
