#ifndef TUIPANEL_H
#define TUIPANEL_H

#include <QWidget>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QTextEdit;

class TuiPanel : public QWidget {
  Q_OBJECT

public:
  explicit TuiPanel(QWidget *parent = nullptr);

private slots:
  void startSystem();
  void stopSystem();
  void diagnoseSystem();
  void healthCheck();
  void openRviz();
  void showLog();

private:
  QLineEdit *mid360Ip_;
  QLineEdit *orinLidarIp_;
  QLineEdit *rosDomain_;
  QLineEdit *laptopPeer_;
  QComboBox *depth_;
  QLineEdit *lidarZ_;
  QComboBox *gui_;
  QCheckBox *cloudini_;
  QTextEdit *output_;

  void appendOutput(const QString &text);
};

#endif
