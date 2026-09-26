#include "tuipanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

TuiPanel::TuiPanel(QWidget *parent)
    : QWidget(parent) {

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

    QTextEdit {
      background-color: #050505;
      color: #AAAAAA;
      border: 1px solid #333333;
      font-family: monospace;
    }
  )");

  auto *mainLayout = new QVBoxLayout(this);

  auto *title = new QLabel("KRATOS SYSTEM CONTROL");
  title->setStyleSheet(
      "color: #FFFF55; font-size: 18px; font-weight: bold;");
  mainLayout->addWidget(title);

  // ───────── Configuration ─────────

  auto *configBox = new QGroupBox("CONFIGURATION");
  auto *form = new QFormLayout(configBox);

  mid360Ip_ = new QLineEdit("192.168.1.1");
  orinLidarIp_ = new QLineEdit("192.168.1.2");
  rosDomain_ = new QLineEdit("0");
  laptopPeer_ = new QLineEdit("192.168.1.100");

  depth_ = new QComboBox();
  depth_->addItems({"zed", "none"});

  lidarZ_ = new QLineEdit("0.60");

  gui_ = new QComboBox();
  gui_->addItems({"auto", "rviz", "none"});

  cloudini_ = new QCheckBox("Enable Cloudini");

  form->addRow("MID-360 IP", mid360Ip_);
  form->addRow("Orin LiDAR IP", orinLidarIp_);
  form->addRow("ROS Domain", rosDomain_);
  form->addRow("Laptop Peer", laptopPeer_);
  form->addRow("Depth", depth_);
  form->addRow("LiDAR Z", lidarZ_);
  form->addRow("GUI", gui_);
  form->addRow("", cloudini_);

  mainLayout->addWidget(configBox);

  // ───────── Actions ─────────

  auto *actionBox = new QGroupBox("ACTIONS");
  auto *actionLayout = new QVBoxLayout(actionBox);

  auto *row1 = new QHBoxLayout();
  auto *startBtn = new QPushButton("START");
  auto *stopBtn = new QPushButton("STOP");

  row1->addWidget(startBtn);
  row1->addWidget(stopBtn);

  auto *row2 = new QHBoxLayout();
  auto *diagnoseBtn = new QPushButton("DIAGNOSE");
  auto *healthBtn = new QPushButton("HEALTH");

  row2->addWidget(diagnoseBtn);
  row2->addWidget(healthBtn);

  auto *row3 = new QHBoxLayout();
  auto *rvizBtn = new QPushButton("RVIZ");
  auto *logBtn = new QPushButton("LOG");

  row3->addWidget(rvizBtn);
  row3->addWidget(logBtn);

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

  connect(startBtn, &QPushButton::clicked,
          this, &TuiPanel::startSystem);

  connect(stopBtn, &QPushButton::clicked,
          this, &TuiPanel::stopSystem);

  connect(diagnoseBtn, &QPushButton::clicked,
          this, &TuiPanel::diagnoseSystem);

  connect(healthBtn, &QPushButton::clicked,
          this, &TuiPanel::healthCheck);

  connect(rvizBtn, &QPushButton::clicked,
          this, &TuiPanel::openRviz);

  connect(logBtn, &QPushButton::clicked,
          this, &TuiPanel::showLog);
}

void TuiPanel::appendOutput(const QString &text) {
  output_->append(text);
}

void TuiPanel::startSystem() {
  appendOutput("Starting Kratos stack...");

  auto *process = new QProcess(this);

  QString program = QDir::homePath() + "/kratos_glim/start_tui.sh";

  process->setProgram("bash");
  process->setArguments({program});

  connect(process, &QProcess::readyReadStandardOutput,
          this, [this, process]() {
            appendOutput(
                QString::fromLocal8Bit(process->readAllStandardOutput()));
          });

  connect(process, &QProcess::readyReadStandardError,
          this, [this, process]() {
            appendOutput(
                QString::fromLocal8Bit(process->readAllStandardError()));
          });

  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this,
          [this, process](int code, QProcess::ExitStatus) {
            appendOutput(QString("Process exited: %1").arg(code));
            process->deleteLater();
          });

  process->start();
}

void TuiPanel::stopSystem() {
  appendOutput("STOP requested.");
  QProcess::execute("bash", {
      "-c",
      "cd ~/kratos_glim && ./start.sh --stop"
  });
}

void TuiPanel::diagnoseSystem() {
  appendOutput("Running diagnostics...");
  QProcess::startDetached(
      "bash",
      {"-c", "cd ~/kratos_glim && ./start_tui.sh"});
}

void TuiPanel::healthCheck() {
  appendOutput("Running health check...");
  QProcess::execute("bash", {
      "-c",
      "cd ~/kratos_glim && ./start_tui.sh"
  });
}

void TuiPanel::openRviz() {
  appendOutput("Opening RViz...");
  QProcess::startDetached(
      "bash",
      {"-c", "cd ~/kratos_glim && ./start_tui.sh"});
}

void TuiPanel::showLog() {
  appendOutput("Opening logs...");
  QProcess::startDetached(
      "bash",
      {"-c", "cd ~/kratos_glim && ./start_tui.sh"});
}
