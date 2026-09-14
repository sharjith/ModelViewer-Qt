#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "TabbedViewportToolbar.h"
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <initializer_list>

void MainWindow::setupViewMenus()
{
    _viewActions.insert(QStringLiteral("fit"), ui->actionViewFit);
    _viewActions.insert(QStringLiteral("windowZoom"), ui->actionViewWindowZoom);
    _viewActions.insert(QStringLiteral("multi"), ui->actionViewMulti);
    _viewActions.insert(QStringLiteral("realistic"), ui->actionViewRealistic);
    _viewActions.insert(QStringLiteral("clipping"), ui->actionViewClipping);
    _viewActions.insert(QStringLiteral("exploded"), ui->actionViewExploded);
    _viewActions.insert(QStringLiteral("turntable"), ui->actionViewTurntable);
    _viewActions.insert(QStringLiteral("axis"), ui->actionViewAxis);
    _viewActions.insert(QStringLiteral("lasso"), ui->actionViewLasso);
    _viewActions.insert(QStringLiteral("swap"), ui->actionViewSwap);
    _viewActions.insert(QStringLiteral("pin"), ui->actionViewPin);
    _viewActions.insert(QStringLiteral("rotate"), ui->actionViewRotate);
    _viewActions.insert(QStringLiteral("pan"), ui->actionViewPan);
    _viewActions.insert(QStringLiteral("zoom"), ui->actionViewZoom);
    _viewActions.insert(QStringLiteral("top"), ui->actionViewTop);
    _viewActions.insert(QStringLiteral("bottom"), ui->actionViewBottom);
    _viewActions.insert(QStringLiteral("front"), ui->actionViewFront);
    _viewActions.insert(QStringLiteral("rear"), ui->actionViewRear);
    _viewActions.insert(QStringLiteral("left"), ui->actionViewLeft);
    _viewActions.insert(QStringLiteral("right"), ui->actionViewRight);
    _viewActions.insert(QStringLiteral("iso"), ui->actionViewIso);
    _viewActions.insert(QStringLiteral("dimetric"), ui->actionViewDimetric);
    _viewActions.insert(QStringLiteral("trimetric"), ui->actionViewTrimetric);
    _viewActions.insert(QStringLiteral("orbit"), ui->actionViewOrbit);
    _viewActions.insert(QStringLiteral("fly"), ui->actionViewFly);
    _viewActions.insert(QStringLiteral("firstPerson"), ui->actionViewFirstPerson);
    _viewActions.insert(QStringLiteral("zUp"), ui->actionViewZUp);
    _viewActions.insert(QStringLiteral("yUp"), ui->actionViewYUp);
    _viewActions.insert(QStringLiteral("perspective"), ui->actionViewPerspective);
    _viewActions.insert(QStringLiteral("ortho"), ui->actionViewOrtho);
    _viewActions.insert(QStringLiteral("shaded"), ui->actionViewShaded);
    _viewActions.insert(QStringLiteral("hollow"), ui->actionViewHollow);
    _viewActions.insert(QStringLiteral("meshEdges"), ui->actionViewMeshEdges);
    _viewActions.insert(QStringLiteral("wireframe"), ui->actionViewWireframe);
    _viewActions.insert(QStringLiteral("shadedEdges"), ui->actionViewShadedEdges);
    _viewActions.insert(QStringLiteral("ads"), ui->actionViewAds);
    _viewActions.insert(QStringLiteral("pbr"), ui->actionViewPbr);
    _viewActions.insert(QStringLiteral("rayTraced"), ui->actionViewRayTraced);
    _viewActions.insert(QStringLiteral("smooth"), ui->actionViewSmooth);
    _viewActions.insert(QStringLiteral("flat"), ui->actionViewFlat);
    _viewActions.insert(QStringLiteral("debugEnabled"), ui->actionViewDebugEnabled);
    _viewActions.insert(QStringLiteral("boundingBox"), ui->actionViewBoundingBox);
    _viewActions.insert(QStringLiteral("vertexNormals"), ui->actionViewVertexNormals);
    _viewActions.insert(QStringLiteral("faceNormals"), ui->actionViewFaceNormals);
    auto exclusive = [this](std::initializer_list<const char*> keys, bool optional = false) {
        auto* group = new QActionGroup(this);
        group->setExclusionPolicy(optional ? QActionGroup::ExclusionPolicy::ExclusiveOptional : QActionGroup::ExclusionPolicy::Exclusive);
        for (const char* key : keys) group->addAction(_viewActions.value(QLatin1String(key)));
    };
    exclusive({"rotate", "pan", "zoom"}, true);
    exclusive({"orbit", "fly", "firstPerson"});
    exclusive({"zUp", "yUp"});
    exclusive({"ortho", "perspective"});
    exclusive({"shaded", "hollow", "meshEdges", "wireframe", "shadedEdges"});
    exclusive({"ads", "pbr", "rayTraced"});
    exclusive({"smooth", "flat"});
    exclusive({"boundingBox", "vertexNormals", "faceNormals"});
    for (auto it = _viewActions.cbegin(); it != _viewActions.cend(); ++it) {
        const QString command = it.key();
        // These actions deliberately have no shortcuts. The toolbar's
        // viewport-scoped registrations remain the sole keyboard bindings.
        connect(it.value(), &QAction::triggered, this, [this, command](bool checked) {
            if (command == QLatin1String("pin")) TabbedViewportToolbar::setPinnedPreference(checked);
            else if (auto* viewer = activeMdiChild()) viewer->getViewportWidget()->executeViewCommand(command, checked);
            updateViewMenus();
        });
    }
    connect(ui->menuView, &QMenu::aboutToShow, this, &MainWindow::updateViewMenus);
    connect(ui->menuSelection, &QMenu::aboutToShow, this, &MainWindow::updateViewMenus);
    for (QMenu* menu : ui->menuView->findChildren<QMenu*>())
        connect(menu, &QMenu::aboutToShow, this, &MainWindow::updateViewMenus);
}

void MainWindow::updateViewMenus()
{
    if (_shuttingDown) return;
    auto* viewer = activeMdiChild();
    const QVariantMap state = viewer ? viewer->getViewportWidget()->viewMenuState() : QVariantMap();
    for (auto it = _viewActions.cbegin(); it != _viewActions.cend(); ++it) {
        auto* action = it.value();
        // Only triggered() dispatches commands. Keep changed() available to
        // QActionGroup so its exclusive-selection bookkeeping stays current.
        const bool pin = it.key() == QLatin1String("pin");
        action->setEnabled(pin || (viewer && state.value(QStringLiteral("available.") + it.key(), true).toBool()));
        if (action->isCheckable()) action->setChecked(pin
            ? QSettings().value(QStringLiteral("ViewportToolbar/pinned"), false).toBool()
            : state.value(it.key(), false).toBool());
    }
}
