#ifndef VECTORRADARWIDGET_H
#define VECTORRADARWIDGET_H

#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QWidget>

class VectorRadarWidget : public QWidget {
  Q_OBJECT
public:
  explicit VectorRadarWidget(QWidget *parent = nullptr);

  // Set X/Y steering inputs.
  // Assuming normalized input where:
  // x: -1.0 (left) to 1.0 (right)
  // y: -1.0 (down) to 1.0 (up)
  void setAxes(float x, float y);
  void setVelocityAxes(float vx, float vy);
  void setDeadzone(float deadzone);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  float x_ = 0.0f;
  float y_ = 0.0f;
  float vx_ = 0.0f;
  float vy_ = 0.0f;
  float deadzone_ = 0.1f;
};

#endif // VECTORRADARWIDGET_H
