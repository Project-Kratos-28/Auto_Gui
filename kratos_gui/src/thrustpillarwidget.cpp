#include "thrustpillarwidget.h"
#include <QRect>
#include <cmath>

ThrustPillarWidget::ThrustPillarWidget(QWidget *parent) : QWidget(parent) {
  setMinimumSize(40, 200); // Taller than wide
}

void ThrustPillarWidget::setPhysicalValue(int value) {
  physicalValue_ = qBound(0, value, 100);
  update();
}

void ThrustPillarWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QRect rect = this->rect();
  int w = rect.width();
  int h = rect.height();
  // int midY = h / 2;

  // Background track
  painter.fillRect(rect, QColor(28, 28, 28)); // Dark grey track (#1C1C1C)

  // Provide a smaller progress bar feel (a standard bottom-up gauge)
  int fillHeight = (physicalValue_ * h) / 100;

  // Command Fill Bar (Yellow)
  QRect fillRect(0, h - fillHeight, w, fillHeight);
  painter.fillRect(fillRect, QColor(255, 255, 85, 200));

  // Outline/border
  painter.setPen(QPen(QColor(85, 85, 85), 1));
  painter.drawRect(0, 0, w - 1, h - 1);
}
