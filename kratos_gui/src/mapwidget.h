#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <memory>

#include <rviz_common/ros_integration/ros_node_abstraction.hpp>

#include "ros_worker.h"

namespace rviz_common {
class VisualizationFrame;
}

class WaypointPanel;
class MissionPanel;

/// The MAP tab: a full embedded RViz (point clouds, /map, TF, waypoint
/// markers, Nav2 plan) with the waypoint and mission panels beside it.
class MapWidget : public QWidget {
  Q_OBJECT

public:
  MapWidget(QApplication *app, RosWorker *rosWorker,
            QWidget *parent = nullptr);
  ~MapWidget() override;

  WaypointPanel *waypointPanel() const { return waypointPanel_; }
  MissionPanel *missionPanel() const { return missionPanel_; }

  /// RViz config loaded at startup. $KRATOS_RVIZ_CONFIG overrides the
  /// packaged config/kratos.rviz.
  static QString rvizConfigPath();

public slots:
  void toggle2D3D();

private:
  void updateViewButton();

  // RViz keeps only a weak_ptr to this node, so the widget owns it.
  std::shared_ptr<rviz_common::ros_integration::RosNodeAbstraction> rvizNode_;
  rviz_common::VisualizationFrame *frame_ = nullptr;

  QPushButton *viewToggleBtn_ = nullptr;
  QLabel *configLabel_ = nullptr;
  WaypointPanel *waypointPanel_ = nullptr;
  MissionPanel *missionPanel_ = nullptr;
};

#endif // MAPWIDGET_H
