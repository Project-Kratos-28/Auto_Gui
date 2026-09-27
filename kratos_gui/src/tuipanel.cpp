#include "tuipanel.h"

#include "ros_worker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
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

    QLineEdit, QComboBox {
      background-color: #151515;
      color: #FFFFFF;
      border: 1px solid #333333;
      padding: 6px;
    }

    QCheckBox::indicator {
      width: 14px;
      height: 14px;
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
  link_->setWordWrap(true);
  state_ = new QLabel("stack: -");
  state_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mainLayout->addWidget(link_);
  mainLayout->addWidget(state_);

  // ───────── Settings (what start_tui.sh edits) ─────────

  auto *configBox = new QGroupBox("SETTINGS (applied on START)");
  auto *form = new QFormLayout(configBox);

  laptopPeer_ = new QLineEdit();
  laptopPeer_->setPlaceholderText("empty = multicast discovery");
  laptopPeer_->setToolTip("This laptop's IP, if the radio link drops "
                          "multicast and the Orin's topics do not show up");
  depth_ = new QComboBox();
  depth_->addItems({"zed", "ess", "none"});
  depth_->setToolTip("zed: ZED depth, ess: stereo DNN depth, none: no "
                     "camera (LiDAR only)");
  lidarZ_ = new QLineEdit();
  lidarZ_->setToolTip("LiDAR height above the ground (m). Nav2 and "
                      "pcd2pgm use their own copy: change them too");
  gui_ = new QComboBox();
  gui_->addItems({"auto", "true", "false"});
  gui_->setToolTip("RViz on the Orin's own screen when START runs");
  cloudini_ = new QCheckBox("compress the clouds for this laptop");
  domain_ = new QLabel("-");

  form->addRow("Laptop peer", laptopPeer_);
  form->addRow("Depth", depth_);
  form->addRow("LiDAR Z (m)", lidarZ_);
  form->addRow("Orin RViz", gui_);
  form->addRow("Cloudini", cloudini_);
  form->addRow("ROS Domain", domain_);

  mainLayout->addWidget(configBox);

  // ───────── Actions ─────────

  auto *actionBox = new QGroupBox("ACTIONS");
  auto *actionLayout = new QVBoxLayout(actionBox);

  startBtn_ = new QPushButton("START");
  stopBtn_ = new QPushButton("STOP");
  diagnoseBtn_ = new QPushButton("DIAGNOSE");
  healthBtn_ = new QPushButton("HEALTH");
  statusBtn_ = new QPushButton("STATUS");
  logBtn_ = new QPushButton("LOG");
  reloadBtn_ = new QPushButton("RELOAD SETTINGS");
  auto *clearBtn = new QPushButton("CLEAR");
  logFilter_ = new QLineEdit();
  logFilter_->setPlaceholderText("log filter, e.g. ERROR|WARN or glim");

  startBtn_->setToolTip("Save the settings on the Orin, then start.sh");
  stopBtn_->setToolTip("stop.sh: GLIM saves its map to ~/kratos_glim/maps");
  diagnoseBtn_->setToolTip("Docker, build, LiDAR port, MID-360, ZED, "
                           "network, lidar_z. Changes nothing");
  healthBtn_->setToolTip("Is every key topic publishing? Plus the last "
                         "errors in the log");
  logBtn_->setToolTip("Last 80 lines of the bring-up log (filtered)");
  reloadBtn_->setToolTip("Refill the fields from the Orin's files");

  auto *row1 = new QHBoxLayout();
  row1->addWidget(startBtn_);
  row1->addWidget(stopBtn_);
  auto *row2 = new QHBoxLayout();
  row2->addWidget(diagnoseBtn_);
  row2->addWidget(healthBtn_);
  auto *row3 = new QHBoxLayout();
  row3->addWidget(logBtn_);
  row3->addWidget(logFilter_, 1);
  auto *row4 = new QHBoxLayout();
  row4->addWidget(statusBtn_);
  row4->addWidget(reloadBtn_);
  row4->addWidget(clearBtn);
  actionLayout->addLayout(row1);
  actionLayout->addLayout(row2);
  actionLayout->addLayout(row3);
  actionLayout->addLayout(row4);
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
    appendOutput("> START (the first start after a rebuild takes minutes)");
    QMetaObject::invokeMethod(rosWorker_, "callSystemWith",
                              Qt::QueuedConnection, Q_ARG(QString, "start"),
                              Q_ARG(QStringList, settings()));
  });
  connect(stopBtn_, &QPushButton::clicked, this, [this]() { call("stop"); });
  connect(diagnoseBtn_, &QPushButton::clicked, this, [this]() {
    busy_ = true;
    updateButtons();
    call("diagnose");
  });
  connect(healthBtn_, &QPushButton::clicked, this, [this]() {
    busy_ = true;
    updateButtons();
    call("health");
  });
  connect(statusBtn_, &QPushButton::clicked, this, [this]() {
    manualStatus_ = true;
    call("status");
  });
  connect(logBtn_, &QPushButton::clicked, this, [this]() {
    callWith("log", {"log_filter=" + logFilter_->text()});
  });
  connect(logFilter_, &QLineEdit::returnPressed, logBtn_, &QPushButton::click);
  connect(reloadBtn_, &QPushButton::clicked, this,
          [this]() { call("get_config"); });
  connect(clearBtn, &QPushButton::clicked, output_, &QTextEdit::clear);

  connect(rosWorker_, &RosWorker::systemServiceStatusChanged, this,
          &TuiPanel::onServiceStatusChanged);
  connect(rosWorker_, &RosWorker::systemResult, this,
          &TuiPanel::onSystemResult);

  // Keep the state line fresh without anyone pressing STATUS. Also runs while
  // offline: the first answer flips the link to CONNECTED
  statusTimer_ = new QTimer(this);
  connect(statusTimer_, &QTimer::timeout, this, [this]() {
    QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                              Q_ARG(QString, "status"));
  });
  statusTimer_->start(3000);

  onServiceStatusChanged(false);
  // The worker only signals link changes: if the link was already up before
  // this tab opened, the reply to this first status call is what says so
  QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                            Q_ARG(QString, "status"));
}

QStringList TuiPanel::settings() const {
  return {"laptop_peer=" + laptopPeer_->text(),
          "depth=" + depth_->currentText(),
          "lidar_z=" + lidarZ_->text(),
          "gui=" + gui_->currentText(),
          QString("cloudini=") + (cloudini_->isChecked() ? "true" : "false")};
}

void TuiPanel::call(const QString &action) {
  appendOutput("> " + action.toUpper());
  QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                            Q_ARG(QString, action));
}

void TuiPanel::callWith(const QString &action, const QStringList &settings) {
  appendOutput("> " + action.toUpper());
  QMetaObject::invokeMethod(rosWorker_, "callSystemWith", Qt::QueuedConnection,
                            Q_ARG(QString, action), Q_ARG(QStringList, settings));
}

void TuiPanel::applyConfig(const QString &config) {
  for (const QString &line : config.split('\n')) {
    const QString k = line.section('=', 0, 0), v = line.section('=', 1);
    if (k == "laptop_peer") laptopPeer_->setText(v);
    else if (k == "depth") depth_->setCurrentText(v);
    else if (k == "lidar_z") lidarZ_->setText(v);
    else if (k == "gui") gui_->setCurrentText(v);
    else if (k == "cloudini") cloudini_->setChecked(v == "true");
    else if (k == "ros_domain") {
      const QString mine = qEnvironmentVariable("ROS_DOMAIN_ID", "0");
      domain_->setText(v + (v == mine ? "  (same as this laptop)"
                                      : "  (THIS LAPTOP USES " + mine + ")"));
    }
  }
  configLoaded_ = true;
}

void TuiPanel::onServiceStatusChanged(bool online) {
  online_ = online;
  if (online) {
    link_->setText("● Orin system_control: CONNECTED");
    link_->setStyleSheet("color: #55FF55; font-weight: bold;");
    if (!configLoaded_) {
      QMetaObject::invokeMethod(rosWorker_, "callSystem", Qt::QueuedConnection,
                                Q_ARG(QString, "get_config"));
    }
  } else {
    link_->setText("● Orin system_control: NOT REACHABLE (Orin on? same "
                   "network and ROS_DOMAIN_ID? systemctl --user status "
                   "kratos-system-control on the Orin)");
    link_->setStyleSheet("color: #FF5555; font-weight: bold;");
    busy_ = false;
  }
  updateButtons();
}

void TuiPanel::onSystemResult(QString action, bool success, QString message) {
  if (action == "status") {
    if (success) {
      // a reply proves the link even if the 1 s poll has not flipped yet
      if (!online_) {
        onServiceStatusChanged(true);
      }
      state_->setText(message);
      state_->setStyleSheet(message.contains("stack=running")
                                ? "color: #55FF55;" : "color: #FFFF55;");
    }
    if (!manualStatus_) {
      return;
    }
    manualStatus_ = false;
  }
  if (action == "get_config" && success) {
    const bool quiet = !configLoaded_;
    applyConfig(message);
    if (quiet) {
      return;  // the automatic load on connect
    }
  }
  if (action == "start" || action == "diagnose" || action == "health") {
    busy_ = false;
    updateButtons();
  }
  QString body = message.trimmed().toHtmlEscaped();
  body.replace(QRegularExpression("^(\\[FAIL\\].*)$",
                                  QRegularExpression::MultilineOption),
               "<span style='color:#FF5555'>\\1</span>");
  body.replace(QRegularExpression("^(\\[WARN\\].*)$",
                                  QRegularExpression::MultilineOption),
               "<span style='color:#FFFF55'>\\1</span>");
  body.replace(QRegularExpression("^(\\[OK  \\].*)$",
                                  QRegularExpression::MultilineOption),
               "<span style='color:#55FF55'>\\1</span>");
  appendOutput(QString("%1 %2:").arg(action.toUpper(),
                                     success ? "OK" : "FAILED"),
               success ? "#55FF55" : "#FF5555");
  output_->append("<pre style='margin:0'>" + body + "</pre>");

  // GLIM needs ~10 s still for its IMU init; then check that it all came up
  if (action == "start" && success) {
    QTimer::singleShot(15000, this, [this]() {
      if (online_ && !busy_) {
        busy_ = true;
        updateButtons();
        call("health");
      }
    });
  }
}

void TuiPanel::updateButtons() {
  for (QPushButton *b : {startBtn_, diagnoseBtn_, healthBtn_}) {
    b->setEnabled(online_ && !busy_);
  }
  for (QPushButton *b : {stopBtn_, statusBtn_, logBtn_, reloadBtn_}) {
    b->setEnabled(online_);
  }
}

void TuiPanel::appendOutput(const QString &text, const QString &color) {
  const QString line = QDateTime::currentDateTime().toString("HH:mm:ss ") +
                       text.trimmed().toHtmlEscaped();
  // Always a tag: without one QTextEdit guesses plain text and shows the escapes (&gt;)
  output_->append(color.isEmpty()
                      ? "<span>" + line + "</span>"
                      : "<span style='color:" + color + "'>" + line + "</span>");
}
