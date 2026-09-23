#include "ros_worker.h"
#include <QDebug>

#include <cmath>

RosWorker::RosWorker(QObject *parent) : QObject(parent) {}

RosWorker::~RosWorker() {
  if (spinTimer_) {
    spinTimer_->stop();
  }
  cameraSub_.reset();
  joySub_.reset();
  addWaypointClient_.reset();
  getWaypointClient_.reset();
  listWaypointsClient_.reset();
  saveWaypointsClient_.reset();
  setAutoClient_.reset();
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

  // Served by GLIM's waypoint_manager extension (auto_fastlio2)
  addWaypointClient_ =
      node_->create_client<waypoint_interfaces::srv::AddWaypoint>(
          "/add_waypoint");
  getWaypointClient_ =
      node_->create_client<waypoint_interfaces::srv::GetWaypoint>(
          "/get_waypoint");
  listWaypointsClient_ =
      node_->create_client<waypoint_interfaces::srv::ListWaypoints>(
          "/list_waypoints");
  saveWaypointsClient_ =
      node_->create_client<waypoint_interfaces::srv::SaveWaypoints>(
          "/save_waypoints");

  // Served by kratos_nav's rover_bridge.py
  setAutoClient_ =
      node_->create_client<std_srvs::srv::SetBool>("/rover_bridge/set_auto");

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

  // Waypoint list refresh. Pending waypoints bind once their submap closes,
  // so the list changes without anyone calling /add_waypoint.
  waypointPollTimer_ = new QTimer(this);
  connect(waypointPollTimer_, &QTimer::timeout, this,
          &RosWorker::pollWaypoints);
  waypointPollTimer_->start(1000);
}

void RosWorker::onCameraListReceived(
    const kratos_msgs::msg::CameraStreamList::SharedPtr msg) {
  CameraStatusList cameras;
  for (const auto &cam : msg->camera_streams) {
    CameraInfo info;
    info.name = QString::fromStdString(cam.camera_name);
    info.active = cam.active;
    info.port = cam.port;
    info.encoder = QString::fromStdString(cam.encoder);
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

// ─── GLIM waypoint_manager ─────────────────────────────────────────

void RosWorker::pollWaypoints() {
  const bool online = listWaypointsClient_->service_is_ready();
  if (online != isWaypointServiceOnline_) {
    isWaypointServiceOnline_ = online;
    emit waypointServiceStatusChanged(online);
  }
  // A request GLIM never answered (e.g. it restarted) would block polling
  // forever, so give up on it after a few seconds.
  if (listInFlight_ && listSentAt_.elapsed() > 5000) {
    listWaypointsClient_->prune_pending_requests();
    listInFlight_ = false;
  }
  // Never queue a second request behind one GLIM hasn't answered yet
  if (!online || listInFlight_) {
    return;
  }

  listInFlight_ = true;
  listSentAt_.start();
  listWaypointsClient_->async_send_request(
      std::make_shared<waypoint_interfaces::srv::ListWaypoints::Request>(),
      [this](rclcpp::Client<waypoint_interfaces::srv::ListWaypoints>::SharedFuture
                 f) {
        listInFlight_ = false;
        QStringList names;
        for (const auto &n : f.get()->names) {
          names.append(QString::fromStdString(n));
        }
        emit waypointsListed(names);
      });
}

void RosWorker::callAddWaypoint(QString name) {
  if (!addWaypointClient_->service_is_ready()) {
    emit waypointAdded(false, name,
                       "/add_waypoint not available (is GLIM running?)");
    return;
  }
  auto req = std::make_shared<waypoint_interfaces::srv::AddWaypoint::Request>();
  req->name = name.toStdString();

  addWaypointClient_->async_send_request(
      req,
      [this, name](
          rclcpp::Client<waypoint_interfaces::srv::AddWaypoint>::SharedFuture f) {
        auto res = f.get();
        emit waypointAdded(res->success, name,
                           QString::fromStdString(res->message));
        pollWaypoints();
      });
}

void RosWorker::callGetWaypoint(QString name) {
  if (!getWaypointClient_->service_is_ready()) {
    emit waypointResolved(name, false, 0, 0, 0, 0);
    return;
  }
  auto req = std::make_shared<waypoint_interfaces::srv::GetWaypoint::Request>();
  req->name = name.toStdString();

  getWaypointClient_->async_send_request(
      req,
      [this, name](
          rclcpp::Client<waypoint_interfaces::srv::GetWaypoint>::SharedFuture f) {
        auto res = f.get();
        const auto &p = res->pose.pose.position;
        const auto &q = res->pose.pose.orientation;
        const double yaw = std::atan2(2.0 * (q.w * q.z + q.x * q.y),
                                      1.0 - 2.0 * (q.y * q.y + q.z * q.z));
        emit waypointResolved(name, res->found, p.x, p.y, p.z, yaw);
      });
}

void RosWorker::callSaveWaypoints(QString path) {
  if (!saveWaypointsClient_->service_is_ready()) {
    emit waypointsSaved(false,
                        "/save_waypoints not available (is GLIM running?)");
    return;
  }
  auto req =
      std::make_shared<waypoint_interfaces::srv::SaveWaypoints::Request>();
  req->path = path.toStdString();

  saveWaypointsClient_->async_send_request(
      req,
      [this](rclcpp::Client<waypoint_interfaces::srv::SaveWaypoints>::SharedFuture
                 f) {
        auto res = f.get();
        emit waypointsSaved(res->success, QString::fromStdString(res->message));
      });
}

// ─── rover_bridge ──────────────────────────────────────────────────

void RosWorker::callSetAutoMode(bool autoMode) {
  if (!setAutoClient_->service_is_ready()) {
    emit autoModeResult(
        autoMode, false,
        "/rover_bridge/set_auto not available (is rover_bridge.py running?)");
    return;
  }
  auto req = std::make_shared<std_srvs::srv::SetBool::Request>();
  req->data = autoMode;

  setAutoClient_->async_send_request(
      req, [this, autoMode](rclcpp::Client<std_srvs::srv::SetBool>::SharedFuture f) {
        auto res = f.get();
        emit autoModeResult(autoMode, res->success,
                            QString::fromStdString(res->message));
      });
}
