#ifndef MISSIONPANEL_H
#define MISSIONPANEL_H

#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "ros_worker.h"

class WaypointPanel;

/// Runs kratos_nav's waypoint_mission.py on the ticked waypoints and flips
/// rover_bridge between MANUAL and AUTO.
class MissionPanel : public QWidget {
  Q_OBJECT

public:
  MissionPanel(RosWorker *rosWorker, WaypointPanel *waypoints,
               QWidget *parent = nullptr);
  ~MissionPanel() override;

  bool isRunning() const;

  /// Cancel a running mission and block until the script has cleaned up
  /// (cancelled its Nav2 goal). Used when the GUI closes.
  void stopBlocking();

public slots:
  void startMission();
  void cancelMission();

private slots:
  void onProcessOutput();
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);
  void onProcessError(QProcess::ProcessError error);
  void onAutoModeResult(bool requested, bool success, QString message);

private:
  enum class State { Idle, Running, Cancelling, Complete, Failed, Cancelled };
  void setState(State state, const QString &detail = QString());
  void appendLog(const QString &line);
  QString missionScriptPath(QString *error) const;

  RosWorker *rosWorker_;
  WaypointPanel *waypoints_;

  QProcess *process_ = nullptr;
  QTimer *killTimer_ = nullptr;
  State state_ = State::Idle;

  QLabel *bridgeLabel_;
  QPushButton *autoBtn_;
  QPushButton *manualBtn_;
  QLabel *stateLabel_;
  QPushButton *startBtn_;
  QPushButton *cancelBtn_;
  QPlainTextEdit *log_;
};

#endif // MISSIONPANEL_H
