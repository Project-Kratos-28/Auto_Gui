#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "ros_worker.h"

class MapWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QApplication *app, QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void onAddCamClicked();
  void onAddJoyNodeClicked();
  void onAddSettingsClicked();
  void onOpenMapClicked();
  void onTabCloseRequested(int index);

private:
  Ui::MainWindow *ui;

  QThread rosThread_;
  RosWorker *rosWorker_ = nullptr;
  MapWidget *mapWidget_ = nullptr; // permanent first tab

  void setupAddMenu(QPushButton *btn);
};

#endif // MAINWINDOW_H
