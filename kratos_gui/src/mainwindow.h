#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "ros_worker.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void onAddCamClicked();
  void onAddJoyNodeClicked();
  void onAddSettingsClicked();
  void onTabCloseRequested(int index);

private:
  Ui::MainWindow *ui;

  QThread rosThread_;
  RosWorker *rosWorker_ = nullptr;

  void setupAddMenu(QPushButton *btn);
};

#endif // MAINWINDOW_H
