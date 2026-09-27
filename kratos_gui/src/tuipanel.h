#ifndef TUIPANEL_H
#define TUIPANEL_H

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTextEdit;
class QTimer;
class RosWorker;

// The Orin's start_tui.sh as a GUI tab: every button is a /kratos_system/*
// service on the Orin (system_control.py), which runs ~/kratos_glim's
// start.sh / stop.sh and its checks. Nobody needs the Orin's terminal.
class TuiPanel : public QWidget {
  Q_OBJECT

public:
  explicit TuiPanel(RosWorker *rosWorker, QWidget *parent = nullptr);

private slots:
  void onServiceStatusChanged(bool online);
  void onSystemResult(QString action, bool success, QString message);

private:
  void call(const QString &action);
  void callWith(const QString &action, const QStringList &settings);
  QStringList settings() const;
  void applyConfig(const QString &config);
  void appendOutput(const QString &text, const QString &color = QString());
  void updateButtons();

  RosWorker *rosWorker_;
  bool online_ = false;
  bool busy_ = false;          // start / diagnose / health waiting for a reply
  bool manualStatus_ = false;  // STATUS pressed: also print it in the output
  bool configLoaded_ = false;  // fields filled from the Orin's files

  QLineEdit *mid360Ip_;
  QLineEdit *orinLidarIp_;
  QLineEdit *laptopPeer_;
  QComboBox *depth_;
  QLineEdit *lidarZ_;
  QComboBox *gui_;
  QCheckBox *cloudini_;
  QLabel *domain_;
  QLineEdit *logFilter_;
  QLabel *link_;
  QLabel *state_;
  QPushButton *startBtn_;
  QPushButton *stopBtn_;
  QPushButton *diagnoseBtn_;
  QPushButton *healthBtn_;
  QPushButton *rvizBtn_;
  QPushButton *statusBtn_;
  QPushButton *logBtn_;
  QPushButton *reloadBtn_;
  QTextEdit *output_;
  QTimer *statusTimer_;
};

#endif
