#ifndef TUIPANEL_H
#define TUIPANEL_H

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QTextEdit;
class QTimer;
class RosWorker;

// Starts/stops bringup.sh on the Orin through the system_control node's
// /kratos_system/* services, and shows its state, checks and log.
class TuiPanel : public QWidget {
  Q_OBJECT

public:
  explicit TuiPanel(RosWorker *rosWorker, QWidget *parent = nullptr);

private slots:
  void onServiceStatusChanged(bool online);
  void onSystemResult(QString action, bool success, QString message);

private:
  void call(const QString &action);
  void appendOutput(const QString &text);
  void updateButtons();

  RosWorker *rosWorker_;
  bool online_ = false;
  bool busy_ = false;          // start or check waiting for its reply
  bool manualStatus_ = false;  // STATUS pressed: also print it in the log

  QLineEdit *mid360Ip_;
  QLineEdit *lidarZ_;
  QCheckBox *orinRviz_;
  QLabel *link_;
  QLabel *state_;
  QPushButton *startBtn_;
  QPushButton *stopBtn_;
  QPushButton *checkBtn_;
  QPushButton *statusBtn_;
  QPushButton *logBtn_;
  QTextEdit *output_;
  QTimer *statusTimer_;
};

#endif
