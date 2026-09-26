#include "tuipanel.h"

#include "ros_worker.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

TuiPanel::TuiPanel(RosWorker *rosWorker, QWidget *parent)
    : QWidget(parent), rosWorker_(rosWorker) {

  setStyleSheet(R"(
    QWidget {
      background-color: #0A0A0A;
      color: #CCCCCC;
      font-family: 'Roboto', 'Segoe UI', Arial;
    }

    QGroupBox {
      border: 1px solid #333333;
      margin-top: 10px;
      padding: 12px;
      color: #FFFF55;
      font-weight: bold;
    }

    QLineEdit {
      background-color: #151515;
      color: #FFFFFF;
      border: 1px solid #333333;
      padding: 6px;
    }

    QCheckBox::indicator {
      width: 14px; height: 14px;
      border: 1px solid #666666;
      background-color: #151515;
    }

    QCheckBox::indicator:checked {
      background-color: #FFFF55;
    }

    QPushButton {
      background-color: #1C1C1C;
      color: #CCCCCC;
      border: 1px solid #444444;
      padding: 7px 14px;
    }

    QPushButton:hover {
      border: 1px solid #FFFF55;
      color: #FFFF55;
    }

    QPushButton:disabled {
      color: #555555;
      border: 1px solid #2A2A2A;
    }

    QTextEdit {
      background-color: #050505;
      color: #AAAAAA;
      border: 1px solid #333333;
      font-family: monospace;
    }
  )");

  auto *mainLayout = new QVBoxLayout(this);

  auto *title = new QLabel("KRATOS SYSTEM CONTROL");
  title->setStyleSheet("color: #FFFF55; font-size: 18px; font-weight: bold;");
  mainLayout->addWidget(title);

  // ───────── Link / state ─────────

  link_ = new QLabel();
  state_ = new QLabel("state: -");
  state_->setWordWrap(true);
  state_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mainLayout->addWidget(link_);
  mainLayout->addWidget(state_);

  // ───────── Start options (bringup.sh flags) ─────────

  auto *configBox = new QGroupBox("START OPTIONS");
  auto *form = new QFormLayout(configBox);

  mid360Ip_ = new QLineEdit();
  mid360Ip_->setPlaceholderText("empty = search 192.168.1.100-199");
  lidarZ_ = new QLineEdit("0.60");
  lidarZ_->setToolTip("Must match nav2_params.yaml and pcd2pgm_live.yaml, "
                      "bringup refuses to start otherwise");
  orinRviz_ = new QCheckBox("RViz on the Orin's own screen");

  form->addRow("MID-360 IP", mid360Ip_);
  form->addRow("LiDAR Z (m)", lidarZ_);
  form->addRow("", orinRviz_);
  const QString domain = qEnvironmentVariable("ROS_DOMAIN_ID", "0 (unset)");
  form->addRow("ROS Domain", new QLabel(domain + "  (must match the Orin)"));

  mainLayout->addWidget(configBox);

  // ───────── Actions ─────────

  auto *actionBox = new QGroupBox("ACTIONS");
  auto *actionLayout = new QVBoxLayout(actionBox);

  startBtn_ = new QPushButton("START");
  stopBtn_ = new QPushButton("STOP");
  checkBtn_ = new QPushButton("CHECK");
  statusBtn_ = new QPushButton("STATUS");
  logBtn_ = new QPushButton("LOG");
  auto *clearBtn = new QPushButton("CLEAR");

  startBtn_->setToolTip("Start bringup.sh on the Orin");
  stopBtn_->setToolTip("Save waypoints and stop everything. Press again to "
                       "force-kill");
  checkBtn_->setToolTip("bringup.sh --check: pre-flight checks, starts nothing");
  statusBtn_->setToolTip("State and health of every node");
  logBtn_->setToolTip("Last 60 lines of bringup.log");

  auto *row1 = new QHBoxLayout();
  row1->addWidget(startBtn_);
  row1->addWidget(stopBtn_);
  auto *row2 = new QHBoxLayout();
  row2->addWidget(checkBtn_);
  row2->addWidget(statusBtn_);
  auto *row3 = new QHBoxLayout();
  row3->addWidget(logBtn_);
  row3->addWidget(clearBtn);
  actionLayout->addLayout(row1);
  actionLayout->addLayout(row2);
  actionLayout->addLayout(row3);
  mainLayout->addWidget(actionBox);

  // ───────── Output ─────────

  auto *outputBox = new QGroupBox("OUTPUT");
  auto *outputLayout = new QVBoxLayout(outputBox);
  output_ = new QTextEdit();
  output_->setReadOnly(true);
  outputLayout->addWidget(output_);
  mainLayout->addWidget(outputBox, 1);

  connect(startBtn_, &QPushButton::clicked, this, [this]() {
    busy_ = true;
    updateButtons();
    appendOutput("> START");
    QMetaObject::invokeMethod(rosWorker_, "callSystemStart",
                              Qt::QueuedConnection,
                              Q_ARG(QString, lidarZ_->text()),
                              Q_ARG(QString, mid360Ip_->text()),
                              Q_ARG(bool, orinRviz_->isChecked()));
  });
  connect(stopBtn_, &QPushButton::clicked, this, [this]() { call("stop"); });
  connect(checkBtn_, &QPushButton::clicked, this, [this]() {
    busy_ = true;
    updateButtons();
    call("check");
  });
  connect(statusBtn_, &QPushButton::clicked, this, [this]() {
    manualStatus_ = true;
    call("status");
  });
  connect(logBtn_, &QPushButton::clicked, this, [this]() { call("log"); });
  connect(clearBtn, &QPushButton::clicked, output_, &QTextEdit::clear);

  connect(rosWorker_, &RosWorker::systemServiceStatusChanged, this,
          &TuiPanel::onServiceStatusChanged);
  connect(rosWorker_, &RosWorker::systemResult, this,
          &TuiPanel::onSystemResult);

  // Keep the state line fresh without anyone pressing STATUS
  statusTimer_ = new QTimer(this);
  connect(statusTimer_, &QTimer::timeout, this, [this]() {
    if (online_) {
      QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                                Q_ARG(QString, "status"));
    }
  });
  statusTimer_->start(3000);

  onServiceStatusChanged(false);
  // The link may already be up: the first poll after this tab opens says so
  QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                            Q_ARG(QString, "status"));
}

void TuiPanel::call(const QString &action) {
  appendOutput("> " + action.toUpper());
  QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                            Q_ARG(QString, action));
}

void TuiPanel::onServiceStatusChanged(bool online) {
  online_ = online;
  if (online) {
    link_->setText("● Orin system_control: CONNECTED");
    link_->setStyleSheet("color: #55FF55; font-weight: bold;");
  } else {
    link_->setText("● Orin system_control: NOT REACHABLE (is the Orin on, "
                   "the service installed, same ROS_DOMAIN_ID?)");
    link_->setStyleSheet("color: #FF5555; font-weight: bold;");
    busy_ = false;
  }
  updateButtons();
}

void TuiPanel::onSystemResult(QString action, bool success, QString message) {
  if (action == "status") {
    // "running=... state=..." first, health lines after
    const QString first = message.section('\n', 0, 0);
    if (success) {
      // a reply proves the link even if the 1 s poll has not flipped yet
      if (!online_) {
        onServiceStatusChanged(true);
      }
      state_->setText(first);
      const bool ready = first.contains("state=ready");
      const bool bad = first.contains("state=failed") ||
                       first.contains("state=degraded");
      state_->setStyleSheet(ready ? "color: #55FF55;"
                                  : bad ? "color: #FF5555;" : "color: #FFFF55;");
    }
    if (!manualStatus_) {
      return;
    }
    manualStatus_ = false;
  }
  if (action == "start" || action == "check") {
    busy_ = false;
    updateButtons();
  }
  appendOutput(QString("%1 %2:\n%3")
                   .arg(action.toUpper(), success ? "OK" : "FAILED",
                        message.trimmed()));
}

void TuiPanel::updateButtons() {
  startBtn_->setEnabled(online_ && !busy_);
  checkBtn_->setEnabled(online_ && !busy_);
  stopBtn_->setEnabled(online_);
  statusBtn_->setEnabled(online_);
  logBtn_->setEnabled(online_);
}

void TuiPanel::appendOutput(const QString &text) {
  output_->append(QDateTime::currentDateTime().toString("HH:mm:ss ") +
                  text.trimmed());
}
