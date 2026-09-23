#include "ros_worker.h"
#include <QDebug>

RosWorker::RosWorker(QObject *parent) : QObject(parent) {}

RosWorker::~RosWorker() {
  if (spinTimer_) {
    spinTimer_->stop();
  }
  cameraSub_.reset();
  joySub_.reset();
  stopStreamClient_.reset();
  node_.reset();
}

void RosWorker::init() {
  node_ = rclcpp::Node::make_shared("kratos_gui_node");

  cameraSub_ = node_->create_subscription<kratos_msgs::msg::CameraStreamList>(
      "/kratos/available_cameras", 10,
      std::bind(&RosWorker::onCameraListReceived, this, std::placeholders::_1));

  joySub_ = node_->create_subscription<sensor_msgs::msg::Joy>(
      "/joy", 10,
      std::bind(&RosWorker::onJoyReceived, this, std::placeholders::_1));

  startStreamClient_ =
      node_->create_client<kratos_msgs::srv::StartCameraStream>(
          "/kratos/cameras/start_stream");
  stopStreamClient_ = node_->create_client<kratos_msgs::srv::StopCameraStream>(
      "/kratos/cameras/stop_stream");

  // Spin ROS node without blocking Qt event loop
  spinTimer_ = new QTimer(this);
  connect(spinTimer_, &QTimer::timeout, this, [this]() {
    if (rclcpp::ok()) {
      rclcpp::spin_some(node_);
    }
  });
  spinTimer_->start(10); // 10ms ~ 100Hz

  // Server timeout detection
  serverAliveTimer_ = new QTimer(this);
  serverAliveTimer_->setSingleShot(true);
  connect(serverAliveTimer_, &QTimer::timeout, this, [this]() {
    if (isServerOnline_) {
      isServerOnline_ = false;
      emit serverStatusChanged(false);
      // Optional: Clear UI when server dies
      emit camerasUpdated(CameraStatusList());
    }
  });
  serverAliveTimer_->start(3000); // Expect a message every <3 seconds
}

void RosWorker::onCameraListReceived(
    const kratos_msgs::msg::CameraStreamList::SharedPtr msg) {
  CameraStatusList cameras;
  for (const auto &cam : msg->camera_streams) {
    CameraInfo info;
    info.name = QString::fromStdString(cam.camera_name);
    info.active = cam.active;
    info.port = cam.port;
    cameras.append(info);
  }

  // Auto-restart timer since we received a list
  serverAliveTimer_->start(3000);
  if (!isServerOnline_) {
    isServerOnline_ = true;
    emit serverStatusChanged(true);
  }

  // Emit the new list of camera statuses
  emit camerasUpdated(cameras);
}

void RosWorker::onJoyReceived(const sensor_msgs::msg::Joy::SharedPtr msg) {
  QList<float> axes(msg->axes.begin(), msg->axes.end());
  QList<int> buttons(msg->buttons.begin(), msg->buttons.end());
  emit joystickDataUpdated(axes, buttons);
}

void RosWorker::callStartStream(QString cameraName) {
  if (!startStreamClient_->wait_for_service(std::chrono::seconds(2))) {
    emit serviceResult(false, "start_stream service not available");
    return;
  }
  auto req = std::make_shared<kratos_msgs::srv::StartCameraStream::Request>();
  req->camera_name = cameraName.toStdString();

  startStreamClient_->async_send_request(
      req,
      [this](
          rclcpp::Client<kratos_msgs::srv::StartCameraStream>::SharedFuture f) {
        auto res = f.get();
        emit serviceResult(res->success, QString::fromStdString(res->message));
      });
}

void RosWorker::callStopStream(QString cameraName) {
  if (!stopStreamClient_->wait_for_service(std::chrono::seconds(2))) {
    emit serviceResult(false, "stop_stream service not available");
    return;
  }
  auto req = std::make_shared<kratos_msgs::srv::StopCameraStream::Request>();
  req->camera_name = cameraName.toStdString();

  stopStreamClient_->async_send_request(
      req,
      [this](
          rclcpp::Client<kratos_msgs::srv::StopCameraStream>::SharedFuture f) {
        auto res = f.get();
        emit serviceResult(res->success, QString::fromStdString(res->message));
      });
}
