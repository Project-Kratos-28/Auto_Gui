#include "missionpanel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QVBoxLayout>

#include <ament_index_cpp/get_package_prefix.hpp>

#include "waypointpanel.h"

// waypoint_mission.py cancels its Nav2 goal on SIGTERM; that can take ~5 s
// if Nav2 is slow to answer. Only SIGKILL after this.
static const int kCleanupTimeoutMs = 10000;

MissionPanel::MissionPanel(RosWorker *rosWorker, WaypointPanel *waypoints,
                           QWidget *parent)
    : QWidget(parent), rosWorker_(rosWorker), waypoints_(waypoints) {
  setStyleSheet(R"(
    QLabel#panelTitle { color: #FFFF55; font-size: 12px; font-weight: bold; }
    QLabel { color: #AAAAAA; font-size: 11px; }
    QPushButton {
        color: #CCCCCC; background-color: #1C1C1C; border: 1px solid #333333;
        padding: 6px 10px; font-size: 11px;
    }
    QPushButton:hover { border: 1px solid #FFFF55; color: #FFFFFF; }
    QPushButton:disabled { color: #555555; border: 1px solid #252525; }
    QPushButton#startBtn { color: #000000; background-color: #55CC55; font-weight: bold; }
    QPushButton#startBtn:disabled { background-color: #333333; color: #666666; }
    QPushButton#cancelBtn { color: #FFFFFF; background-color: #CC3333; font-weight: bold; }
    QPushButton#cancelBtn:disabled { background-color: #333333; color: #666666; }
    QPlainTextEdit {
        background-color: #0A0A0A; color: #AAAAAA; border: 1px solid #333333;
        font-family: monospace; font-size: 10px;
    }
  )");

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QLabel *title = new QLabel("MISSION", this);
  title->setObjectName("panelTitle");
  layout->addWidget(title);

  // ── rover_bridge mode ────────────────────────────────────────────
  QHBoxLayout *modeRow = new QHBoxLayout();
  bridgeLabel_ = new QLabel("Bridge: unknown", this);
  bridgeLabel_->setToolTip(
      "rover_bridge only drives from Nav2 in AUTO. Moving a joystick stick "
      "drops it back to MANUAL, and the GUI can't see that happen.");
  autoBtn_ = new QPushButton("AUTO", this);
  manualBtn_ = new QPushButton("MANUAL", this);
  connect(autoBtn_, &QPushButton::clicked, this, [this]() {
    QMetaObject::invokeMethod(rosWorker_, "callSetAutoMode",
                              Qt::QueuedConnection, Q_ARG(bool, true));
  });
  connect(manualBtn_, &QPushButton::clicked, this, [this]() {
    QMetaObject::invokeMethod(rosWorker_, "callSetAutoMode",
                              Qt::QueuedConnection, Q_ARG(bool, false));
  });
  modeRow->addWidget(bridgeLabel_, 1);
  modeRow->addWidget(autoBtn_);
  modeRow->addWidget(manualBtn_);
  layout->addLayout(modeRow);

  // ── start / cancel ───────────────────────────────────────────────
  QHBoxLayout *runRow = new QHBoxLayout();
  startBtn_ = new QPushButton("START MISSION", this);
  startBtn_->setObjectName("startBtn");
  startBtn_->setToolTip("Drive to the ticked waypoints, top to bottom");
  cancelBtn_ = new QPushButton("CANCEL (Esc)", this);
  cancelBtn_->setObjectName("cancelBtn");
  cancelBtn_->setToolTip("Cancel the Nav2 goal and stop the mission (Esc)");
  connect(startBtn_, &QPushButton::clicked, this, &MissionPanel::startMission);
  connect(cancelBtn_, &QPushButton::clicked, this,
          &MissionPanel::cancelMission);
  runRow->addWidget(startBtn_, 1);
  runRow->addWidget(cancelBtn_, 1);
  layout->addLayout(runRow);

  stateLabel_ = new QLabel(this);
  stateLabel_->setWordWrap(true);
  layout->addWidget(stateLabel_);

  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(1000);
  layout->addWidget(log_, 1);

  // ── mission process ──────────────────────────────────────────────
  process_ = new QProcess(this);
  process_->setProcessChannelMode(QProcess::MergedChannels);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("PYTHONUNBUFFERED", "1"); // stream log lines as they happen
  env.insert("RCUTILS_COLORIZED_OUTPUT", "0");
  process_->setProcessEnvironment(env);
  connect(process_, &QProcess::readyReadStandardOutput, this,
          &MissionPanel::onProcessOutput);
  connect(process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &MissionPanel::onProcessFinished);
  connect(process_, &QProcess::errorOccurred, this,
          &MissionPanel::onProcessError);

  killTimer_ = new QTimer(this);
  killTimer_->setSingleShot(true);
  connect(killTimer_, &QTimer::timeout, this, [this]() {
    if (isRunning()) {
      appendLog("!! Mission did not exit after cancel. Killing it. It may "
                "NOT have cancelled its Nav2 goal: check the rover.");
      process_->kill();
    }
  });

  connect(rosWorker_, &RosWorker::autoModeResult, this,
          &MissionPanel::onAutoModeResult);

  setState(State::Idle);
}

MissionPanel::~MissionPanel() { stopBlocking(); }

bool MissionPanel::isRunning() const {
  return process_ && process_->state() != QProcess::NotRunning;
}

void MissionPanel::stopBlocking() {
  if (!isRunning()) {
    return;
  }
  process_->terminate();
  if (!process_->waitForFinished(kCleanupTimeoutMs)) {
    process_->kill();
    process_->waitForFinished(1000);
  }
}

QString MissionPanel::missionScriptPath(QString *error) const {
  try {
    const QString path = QString::fromStdString(
                             ament_index_cpp::get_package_prefix("kratos_nav")) +
                         "/lib/kratos_nav/waypoint_mission.py";
    if (QFileInfo::exists(path)) {
      return path;
    }
    *error = "waypoint_mission.py missing at " + path;
  } catch (const std::exception &) {
    *error = "kratos_nav not found. Source auto_fastlio2's install/setup.bash "
             "before starting the GUI.";
  }
  return QString();
}

void MissionPanel::startMission() {
  if (isRunning()) {
    return;
  }
  const QStringList names = waypoints_->missionWaypoints();
  if (names.isEmpty()) {
    setState(State::Idle, "Tick at least one waypoint first.");
    return;
  }

  QString error;
  const QString script = missionScriptPath(&error);
  if (script.isEmpty()) {
    setState(State::Failed, error);
    return;
  }

  const auto answer = QMessageBox::question(
      this, "Start mission",
      QString("Drive autonomously to:\n\n%1\n\nrover_bridge must be in AUTO "
              "for the wheels to move.")
          .arg(names.join("  →  ")),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer != QMessageBox::Yes) {
    return;
  }

  // YAML list for the `waypoints` parameter. Names are single-quoted, and
  // YAML escapes a quote inside by doubling it.
  QStringList quoted;
  for (QString n : names) {
    quoted.append("'" + n.replace("'", "''") + "'");
  }
  const QString param = "waypoints:=[" + quoted.join(",") + "]";

  log_->clear();
  appendLog("$ waypoint_mission.py --ros-args -p " + param);
  // Run the script itself, not `ros2 run`: ros2 run doesn't forward SIGTERM,
  // so cancelling would orphan the script with its Nav2 goal still active.
  process_->start("python3",
                  QStringList() << script << "--ros-args" << "-p" << param);
  setState(State::Running, names.join(" → "));
}

void MissionPanel::cancelMission() {
  if (!isRunning() || state_ == State::Cancelling) {
    return;
  }
  appendLog("-- cancel requested (SIGTERM)");
  process_->terminate();
  killTimer_->start(kCleanupTimeoutMs);
  setState(State::Cancelling);
}

void MissionPanel::onProcessOutput() {
  while (process_->canReadLine()) {
    appendLog(QString::fromUtf8(process_->readLine()).trimmed());
  }
}

void MissionPanel::onProcessFinished(int exitCode,
                                     QProcess::ExitStatus status) {
  killTimer_->stop();
  onProcessOutput();
  const QString rest = QString::fromUtf8(process_->readAll()).trimmed();
  if (!rest.isEmpty()) {
    appendLog(rest);
  }

  if (state_ == State::Cancelling) {
    setState(State::Cancelled);
  } else if (status == QProcess::NormalExit && exitCode == 0) {
    setState(State::Complete);
  } else {
    setState(State::Failed,
             status == QProcess::CrashExit
                 ? "mission script crashed"
                 : QString("exit code %1, see log").arg(exitCode));
  }
}

void MissionPanel::onProcessError(QProcess::ProcessError error) {
  if (error == QProcess::FailedToStart) {
    setState(State::Failed, "could not start python3: " +
                                process_->errorString());
  }
}

void MissionPanel::onAutoModeResult(bool requested, bool success,
                                    QString message) {
  if (!success) {
    bridgeLabel_->setText("Bridge: " + message);
    bridgeLabel_->setStyleSheet("color: #FF5555;");
    return;
  }
  // The service replies with the mode it is now in
  const bool isAuto = message == "AUTO" || (message.isEmpty() && requested);
  bridgeLabel_->setText(isAuto ? "Bridge: AUTO (set from GUI)"
                               : "Bridge: MANUAL");
  bridgeLabel_->setStyleSheet(isAuto ? "color: #55FF55; font-weight: bold;"
                                     : "color: #AAAAAA;");
}

void MissionPanel::setState(State state, const QString &detail) {
  state_ = state;
  QString text;
  QString color;
  switch (state) {
  case State::Idle:
    text = "IDLE";
    color = "#AAAAAA";
    break;
  case State::Running:
    text = "RUNNING";
    color = "#55FF55";
    break;
  case State::Cancelling:
    text = "CANCELLING… (waiting for Nav2 goal cancel)";
    color = "#FFAA33";
    break;
  case State::Complete:
    text = "COMPLETE";
    color = "#55FF55";
    break;
  case State::Failed:
    text = "FAILED";
    color = "#FF5555";
    break;
  case State::Cancelled:
    text = "CANCELLED";
    color = "#FFAA33";
    break;
  }
  if (!detail.isEmpty()) {
    text += ": " + detail;
  }
  stateLabel_->setText(text);
  stateLabel_->setStyleSheet(
      QString("color: %1; font-weight: bold;").arg(color));

  const bool busy = state == State::Running || state == State::Cancelling;
  startBtn_->setEnabled(!busy);
  cancelBtn_->setEnabled(state == State::Running);
}

void MissionPanel::appendLog(const QString &line) {
  if (!line.isEmpty()) {
    log_->appendPlainText(line);
  }
}
