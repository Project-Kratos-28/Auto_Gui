#ifndef WAYPOINTPANEL_H
#define WAYPOINTPANEL_H

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include "ros_worker.h"

/// Tag, list, inspect and save GLIM waypoints (waypoint_manager services).
/// The list is also the mission plan: checked waypoints, top to bottom.
class WaypointPanel : public QWidget {
  Q_OBJECT

public:
  explicit WaypointPanel(RosWorker *rosWorker, QWidget *parent = nullptr);

  /// Checked waypoint names in list order, without the " (pending)" suffix.
  QStringList missionWaypoints() const;

public slots:
  /// Tag the rover's current pose (name box, or the next free wpN).
  void logWaypoint();

private slots:
  void onServiceStatusChanged(bool online);
  void onWaypointsListed(QStringList names);
  void onWaypointAdded(bool success, QString name, QString message);
  void onWaypointResolved(QString name, bool found, double x, double y,
                          double z, double yaw);
  void onWaypointsSaved(bool success, QString message);
  void onSaveClicked();
  void onItemClicked(QListWidgetItem *item);

private:
  QString nextAutoName() const;
  QListWidgetItem *findItem(const QString &name) const;
  void setItemPending(QListWidgetItem *item, bool pending);
  void showStatus(const QString &text, bool ok);

  RosWorker *rosWorker_;

  QLabel *serviceLabel_;
  QLineEdit *nameEdit_;
  QPushButton *logBtn_;
  QPushButton *saveBtn_;
  QListWidget *list_;
  QLabel *poseLabel_;
  QLabel *statusLabel_;
};

#endif // WAYPOINTPANEL_H
