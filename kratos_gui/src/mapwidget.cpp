#include "mapwidget.h"

#include <QAction>
#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rviz_common/view_controller.hpp>
#include <rviz_common/view_manager.hpp>
#include <rviz_common/visualization_frame.hpp>
#include <rviz_common/visualization_manager.hpp>

#include "missionpanel.h"
#include "waypointpanel.h"

static const char *kView2D = "rviz_default_plugins/TopDownOrtho";
static const char *kView3D = "rviz_default_plugins/Orbit";

QString MapWidget::rvizConfigPath() {
  const QString env = qEnvironmentVariable("KRATOS_RVIZ_CONFIG");
  if (!env.isEmpty()) {
    return env;
  }
  try {
    return QString::fromStdString(
               ament_index_cpp::get_package_share_directory("kratos_gui")) +
           "/config/kratos.rviz";
  } catch (const std::exception &) {
    return QString(); // RViz starts with its default (empty) config
  }
}

MapWidget::MapWidget(QApplication *app, RosWorker *rosWorker, QWidget *parent)
    : QWidget(parent) {
  setStyleSheet(R"(
    QWidget#mapToolbar { background-color: #1C1C1C; border-bottom: 1px solid #333333; }
    QPushButton#viewToggleBtn {
        color: #FFFF55; background-color: #252525; border: 1px solid #333333;
        padding: 4px 12px; font-size: 11px; font-weight: bold;
    }
    QPushButton#viewToggleBtn:hover { border: 1px solid #FFFF55; }
    QLabel#configLabel { color: #666666; font-size: 10px; }
  )");

  QVBoxLayout *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  // ── Toolbar ──────────────────────────────────────────────────────
  QWidget *toolbar = new QWidget(this);
  toolbar->setObjectName("mapToolbar");
  QHBoxLayout *tb = new QHBoxLayout(toolbar);
  tb->setContentsMargins(8, 4, 8, 4);

  viewToggleBtn_ = new QPushButton(toolbar);
  viewToggleBtn_->setObjectName("viewToggleBtn");
  viewToggleBtn_->setToolTip("Switch between top-down 2D and orbit 3D (Ctrl+D)");
  connect(viewToggleBtn_, &QPushButton::clicked, this, &MapWidget::toggle2D3D);
  tb->addWidget(viewToggleBtn_);
  tb->addStretch();

  const QString configPath = rvizConfigPath();
  configLabel_ = new QLabel(toolbar);
  configLabel_->setObjectName("configLabel");
  tb->addWidget(configLabel_);
  outer->addWidget(toolbar);

  // ── RViz | side panels ───────────────────────────────────────────
  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(1);
  splitter->setStyleSheet("QSplitter::handle { background-color: #333333; }");

  rvizNode_ =
      std::make_shared<rviz_common::ros_integration::RosNodeAbstraction>(
          "kratos_gui_rviz");
  frame_ = new rviz_common::VisualizationFrame(rvizNode_, splitter);
  frame_->setApp(app);
  frame_->setSplashPath("");
  // A QMainWindow child renders as a top-level window unless told otherwise
  frame_->setWindowFlags(Qt::Widget);

  if (!configPath.isEmpty() && QFileInfo::exists(configPath)) {
    frame_->initialize(rvizNode_, configPath);
    configLabel_->setText("RViz config: " + configPath);
  } else {
    frame_->initialize(rvizNode_);
    configLabel_->setText("RViz config not found: " + configPath);
    configLabel_->setStyleSheet("color: #FF5555;");
  }
  // RViz's File > Quit (Ctrl+Q) would close the embedded view, not the app
  for (QAction *action : frame_->findChildren<QAction *>()) {
    if (action->text().remove('&') == "Quit") {
      action->setShortcut(QKeySequence());
      action->setVisible(false);
      action->setEnabled(false);
    }
  }
  // Keep RViz's docked panels (Displays) narrow so the 3D view gets the room
  for (QDockWidget *dock : frame_->findChildren<QDockWidget *>()) {
    frame_->resizeDocks({dock}, {260}, Qt::Horizontal);
  }
  splitter->addWidget(frame_);

  QWidget *side = new QWidget(splitter);
  side->setStyleSheet("background-color: #121212;");
  QVBoxLayout *sideLayout = new QVBoxLayout(side);
  sideLayout->setContentsMargins(8, 8, 8, 8);
  sideLayout->setSpacing(8);
  waypointPanel_ = new WaypointPanel(rosWorker, side);
  missionPanel_ = new MissionPanel(rosWorker, waypointPanel_, side);
  sideLayout->addWidget(waypointPanel_, 3);
  sideLayout->addWidget(missionPanel_, 2);
  splitter->addWidget(side);

  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 0);
  splitter->setSizes(QList<int>() << 900 << 340);
  outer->addWidget(splitter, 1);

  // Keep the button honest when the view is changed from RViz's Views panel
  connect(frame_->getManager()->getViewManager(),
          &rviz_common::ViewManager::currentChanged, this,
          &MapWidget::updateViewButton);
  updateViewButton();
}

MapWidget::~MapWidget() {
  // The frame must go before rvizNode_; Qt would only delete it (as a child)
  // after this object's members are already destroyed.
  delete frame_;
  frame_ = nullptr;
}

void MapWidget::toggle2D3D() {
  rviz_common::ViewManager *views = frame_->getManager()->getViewManager();
  const bool is2D = views->getCurrent() &&
                    views->getCurrent()->getClassId() == kView2D;
  // The new controller copies the old one's focal point, so the map stays put
  views->setCurrentViewControllerType(is2D ? kView3D : kView2D);
}

void MapWidget::updateViewButton() {
  rviz_common::ViewManager *views = frame_->getManager()->getViewManager();
  const bool is2D = views->getCurrent() &&
                    views->getCurrent()->getClassId() == kView2D;
  viewToggleBtn_->setText(is2D ? "VIEW: 2D  ▸ 3D" : "VIEW: 3D  ▸ 2D");
}
