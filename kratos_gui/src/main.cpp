#include "mainwindow.h"
#include <QApplication>
#include <rclcpp/rclcpp.hpp>

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  QApplication a(argc, argv);
  // rclcpp owns SIGINT/SIGTERM; without this, Ctrl+C would stop ROS but leave
  // the window running. Runs on rclcpp's signal thread, hence the queue.
  rclcpp::on_shutdown(
      [&a]() { QMetaObject::invokeMethod(&a, "quit", Qt::QueuedConnection); });
  MainWindow w(&a);
  w.show();
  int result = a.exec();
  rclcpp::shutdown();
  return result;
}
