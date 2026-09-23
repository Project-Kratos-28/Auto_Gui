#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QComboBox>
#include <QGridLayout>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "ros_worker.h"
#include "videoplayerwidget.h"

#include <QShortcut>
#include <QKeySequence>

namespace Ui {
class CameraWidget;
}

/// One cell in the grid: holds a VideoPlayerWidget + an overlay combo to pick
/// which camera stream to show.
struct GridCell {
  QWidget *frame = nullptr;
  VideoPlayerWidget *player = nullptr;
  QComboBox *streamPicker = nullptr;
  QString assignedCamera; // empty = no stream
};

class CameraWidget : public QWidget {
  Q_OBJECT

public:
  explicit CameraWidget(RosWorker *rosWorker, QWidget *parent = nullptr);
  ~CameraWidget() override;



private slots:
  void onCamerasUpdated(CameraStatusList cameras);
  void onLayoutChanged(int index);
  void onSizeChanged(int index);
  void onCellStreamChanged(int cellIndex);
  void onSidebarToggle();
  void onSidebarCameraClicked();


private:
  Ui::CameraWidget *ui;

  RosWorker *rosWorker_;

  // Toolbar widgets

  QPushButton *sidebarSplitToggle_;
  QComboBox *layoutCombo_;
  QComboBox *sizeCombo_;

  // Sidebar camera buttons
  QVBoxLayout *camerasListLayout_;

  // Hotkeys
  void setupHotkeys();
  void toggleCameraByIndex(int index);
  void toggleAllCameras();

  // Grid
  QGridLayout *gridLayout_;
  QWidget *gridContainer_;
  QVector<GridCell> cells_;

  // Known cameras from ROS
  CameraStatusList knownCameras_;

  void rebuildGrid(int rows, int cols);
  void updateCellDropdowns();
  void parseLayout(int index, int &rows, int &cols);

};

#endif // CAMERAWIDGET_H
