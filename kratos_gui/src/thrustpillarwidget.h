#ifndef THRUSTPILLARWIDGET_H
#define THRUSTPILLARWIDGET_H

#include <QPaintEvent>
#include <QPainter>
#include <QWidget>

class ThrustPillarWidget : public QWidget {
  Q_OBJECT
public:
  explicit ThrustPillarWidget(QWidget *parent = nullptr);

  // Set the physical value from 0 (bottom) to 100 (top)
  void setPhysicalValue(int value);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  int physicalValue_ = 50; // Neutral default
};

#endif // THRUSTPILLARWIDGET_H
