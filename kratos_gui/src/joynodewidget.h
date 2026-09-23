#ifndef JOYNODEWIDGET_H
#define JOYNODEWIDGET_H

#include <QList>
#include <QWidget>

// Forward declarations for our custom widgets
class ThrustPillarWidget;
class VectorRadarWidget;

namespace Ui {
class JoyNodeWidget;
}

class JoyNodeWidget : public QWidget {
  Q_OBJECT

public:
  explicit JoyNodeWidget(QWidget *parent = nullptr);
  ~JoyNodeWidget() override;

  // Example functions to receive /joy data
  void updateJoystickData(QList<float> axes, QList<int> buttons);

private:
  Ui::JoyNodeWidget *ui;
};

#endif // JOYNODEWIDGET_H
