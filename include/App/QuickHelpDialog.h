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

    void showShortcutsTab();      // the Start Page's "View Shortcuts" button
    void showMouseControlsTab();  // ... and "Mouse Controls"

private:
    void setupUI();
    void setupMouseControlsTab();
    void setupKeyboardShortcutsTab();
    void setupViewToolbarTab();
    void setupMenuShortcutsTab();
    void setupCameraModesTab();
    void setupDisplayModesTab();
    void setupAdvancedFeaturesTab();
    void setupMeasurementTab();
    void setupMeshEditingTab();
    void setupSimulationTab();     // QuickHelpDialogResults.cpp
    void setupPlot3DTab();
    void setupAnalysisScenesTab();
    void setupTipsAndTricksTab();

    QString createStyledHtml(const QString& title, const QString& content);
    QString createSection(const QString& heading, const QString& content);
    QString createTable(const QStringList& headers, const QList<QStringList>& rows);

    QTabWidget* _tabWidget;
    QTextBrowser* _mouseControlsBrowser;
    QTextBrowser* _keyboardBrowser;
    QTextBrowser* _toolbarBrowser;
    QTextBrowser* _menuBrowser;
    QTextBrowser* _cameraBrowser;
    QTextBrowser* _displayBrowser;
    QTextBrowser* _advancedBrowser;
    QTextBrowser* _measurementBrowser;
    QTextBrowser* _meshEditingBrowser;
    QTextBrowser* _simulationBrowser;
    QTextBrowser* _plot3DBrowser;
    QTextBrowser* _analysisBrowser;
    QTextBrowser* _tipsBrowser;
    QPushButton* _closeButton;
};
