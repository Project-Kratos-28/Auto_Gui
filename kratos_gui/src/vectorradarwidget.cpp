#include "vectorradarwidget.h"
#include <cmath>

VectorRadarWidget::VectorRadarWidget(QWidget *parent) : QWidget(parent) {
  setMinimumSize(150, 150);
}

void VectorRadarWidget::setAxes(float x, float y) {
  x_ = qBound(-1.0f, x, 1.0f);
  y_ = qBound(-1.0f, y, 1.0f);
  update();
}

void VectorRadarWidget::setVelocityAxes(float vx, float vy) {
  vx_ = qBound(-1.0f, vx, 1.0f);
  vy_ = qBound(-1.0f, vy, 1.0f);
  update();
}

void VectorRadarWidget::setDeadzone(float deadzone) {
  deadzone_ = qBound(0.0f, deadzone, 1.0f);
  update();
}

void VectorRadarWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QRect rect = this->rect();
  int size = qMin(rect.width(), rect.height());
  // Center a square radar in the widget
  QRect radarRect((rect.width() - size) / 2, (rect.height() - size) / 2, size,
                  size);
  QPoint center = radarRect.center();
  int radius = size / 2;

  // Background Grid
  painter.fillRect(radarRect, QColor(28, 28, 28)); // #1C1C1C
  painter.setPen(QPen(QColor(51, 51, 51), 1));     // #333333

  // Draw cross grid lines
  painter.drawLine(center.x(), radarRect.top(), center.x(), radarRect.bottom());
  painter.drawLine(radarRect.left(), center.y(), radarRect.right(), center.y());

  // Deadzone Ring
  painter.setPen(
      QPen(QColor(170, 170, 170), 1, Qt::DashLine)); // #AAAAAA dashed
  painter.setBrush(Qt::NoBrush);
  int deadzoneRadius = radius * deadzone_;
  painter.drawEllipse(center, deadzoneRadius, deadzoneRadius);

  // Compute input coordinates geometry
  // y_ is positive UP, so we subtract from center.y()
  QPointF inputPoint(center.x() + (x_ * radius), center.y() - (y_ * radius));
  QPointF velocityPoint(center.x() + (vx_ * radius),
                        center.y() - (vy_ * radius));

  // Dynamic Vector Line (Raw Joystick position)
  painter.setPen(
      QPen(QColor(255, 255, 85, 100), 1, Qt::DashLine)); // #FFFF55 faded dashed
  painter.drawLine(center, inputPoint);

  // Velocity Vector Line
  painter.setPen(QPen(QColor(85, 255, 255, 200), 3)); // Cyan velocity line
  painter.drawLine(center, velocityPoint);

  // Crosshair / Dot for live axes (Joystick)
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(255, 255, 85, 100)); // Faded yellow
  painter.drawEllipse(inputPoint, 3, 3);

  // Crosshair / Dot for velocity
  painter.setBrush(QColor(85, 255, 255, 255)); // Bright cyan
  painter.drawEllipse(velocityPoint, 5, 5);
}
