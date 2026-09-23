#include "joynodewidget.h"
#include "thrustpillarwidget.h"
#include "ui_joynodewidget.h"
#include "vectorradarwidget.h"

#include <QString>

JoyNodeWidget::JoyNodeWidget(QWidget *parent)
    : QWidget(parent), ui(new Ui::JoyNodeWidget) {
  ui->setupUi(this);

  // Initialize status to false (waiting for actual data)
  ui->joynodeStatusLabel->setText("JOYNODE: NOT RUNNING");
  ui->joynodeStatusLabel->setStyleSheet(
      "color: #FF5555; font-weight: bold; border: none;");
  
  ui->hardwareStatusLabel->setText("HW: DISCONNECTED");
  ui->hardwareStatusLabel->setStyleSheet(
      "color: #FF5555; font-weight: bold; border: none;");
}

JoyNodeWidget::~JoyNodeWidget() { delete ui; }


void JoyNodeWidget::updateJoystickData(QList<float> axes, QList<int> buttons) {
  // If we receive data, joynode is running
  ui->joynodeStatusLabel->setText("JOYNODE: NOMINAL");
  ui->joynodeStatusLabel->setStyleSheet(
      "color: #AAAAAA; font-weight: bold; border: none;");
      
  // For now, if axes/buttons are not completely empty, assume hardware is connected
  if (!axes.isEmpty() || !buttons.isEmpty()) {
    ui->hardwareStatusLabel->setText("HW: CONNECTED"); // Generic name instead of Thrustmaster
    ui->hardwareStatusLabel->setStyleSheet(
        "color: #AAAAAA; font-weight: bold; border: none;");
  }

  // Construct raw data string
  QString rawData;
  rawData += "<html><body>[ RAW /joy ]<br>axes: [";
  for (int i = 0; i < axes.size(); ++i) {
    rawData +=
        QString::number(axes[i], 'f', 2) + (i < axes.size() - 1 ? ", " : "");
  }
  rawData += "]<br>buttons: [";
  for (int i = 0; i < buttons.size(); ++i) {
    rawData +=
        QString::number(buttons[i]) + (i < buttons.size() - 1 ? ", " : "");

    // Update hardware mapping schematic (Dummy logic for btn1 and btn2)
    if (i == 0) {
      if (buttons[i] == 1)
        ui->btn1->setStyleSheet(
            "background-color: #FFFF55; color: #000000; font-weight: bold; "
            "border: 1px solid #FFFF55;");
      else
        ui->btn1->setStyleSheet("background-color: #1C1C1C; color: #555555; "
                                "border: 1px solid #333333;");
    } else if (i == 1) {
      if (buttons[i] == 1)
        ui->btn2->setStyleSheet(
            "background-color: #FFFF55; color: #000000; font-weight: bold; "
            "border: 1px solid #FFFF55;");
      else
        ui->btn2->setStyleSheet("background-color: #1C1C1C; color: #555555; "
                                "border: 1px solid #333333;");
    }
  }
  rawData += "]</body></html>";
  ui->rawDataText->setHtml(rawData);

  // Assuming axes[2] is throttle (Z-Axis Thrust) and axes[0], axes[1] are
  // steering (X/Y Radar)
  float thrustMapping = 0.0f;
  float xSteering = 0.0f;
  float ySteering = 0.0f;

  if (axes.size() >= 3) {
    xSteering = -axes[0]; // Inverted left/right
    ySteering = axes[1];
    ui->vectorRadar->setAxes(xSteering, ySteering);

    // Thrust axis mapping: map -1.0 to 1.0 -> 0 to 100
    thrustMapping = axes[2];
    int physicalThrust = static_cast<int>((thrustMapping + 1.0f) * 50.0f);
    ui->thrustPillar->setPhysicalValue(physicalThrust);

    // Calculate dynamic velocity vector (0 vel if thrust is -1.0 -> 0%)
    float throttleFactor = (thrustMapping + 1.0f) / 2.0f; // 0.0 to 1.0
    float vx = xSteering * throttleFactor;
    float vy = ySteering * throttleFactor;
    ui->vectorRadar->setVelocityAxes(vx, vy);
  }

  // Construct cmd data string
  QString cmdData;
  float cmdLinX = (thrustMapping + 1.0f) / 2.0f;
  cmdData += QString("<html><body>[ OUT cmd_vel ]<br>linear.x: "
                     "%1<br>angular.z: %2</body></html>")
                 .arg(cmdLinX, 0, 'f', 2)
                 .arg(xSteering, 0, 'f', 2);
  ui->cmdDataText->setHtml(cmdData);
}
