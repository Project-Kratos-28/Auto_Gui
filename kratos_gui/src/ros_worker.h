#ifndef ROS_WORKER_H
#define ROS_WORKER_H

#include <QImage>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <kratos_msgs/msg/camera_stream_list.hpp>
#include <kratos_msgs/srv/start_camera_stream.hpp>
#include <kratos_msgs/srv/stop_camera_stream.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <waypoint_interfaces/srv/add_waypoint.hpp>
#include <waypoint_interfaces/srv/get_waypoint.hpp>
#include <waypoint_interfaces/srv/list_waypoints.hpp>
#include <waypoint_interfaces/srv/save_waypoints.hpp>

// Struct to store camera info
struct CameraInfo {
  QString name;
  bool active;
  int port;
  QString encoder; // "h265" or "av1"

  bool operator==(const CameraInfo &other) const {
    return name == other.name;
  }
};

using CameraStatusList = QList<CameraInfo>;

class RosWorker : public QObject {
  Q_OBJECT
public:
  explicit RosWorker(QObject *parent = nullptr);
  ~RosWorker() override;

signals:
  void camerasUpdated(CameraStatusList cameras);
  void serviceResult(bool success, QString message);
  void serverStatusChanged(bool online);
  void joystickDataUpdated(QList<float> axes, QList<int> buttons);

  // GLIM waypoint_manager
  void waypointServiceStatusChanged(bool online);
  void waypointsListed(QStringList names); // raw names, may end in " (pending)"
  void waypointAdded(bool success, QString name, QString message);
  void waypointResolved(QString name, bool found, double x, double y, double z,
                        double yaw);
  void waypointsSaved(bool success, QString message);

  // rover_bridge
  void autoModeResult(bool requested, bool success, QString message);

public slots:
  void init();
  void callStartStream(QString cameraName);
  void callStopStream(QString cameraName);

  void callAddWaypoint(QString name);
  void callGetWaypoint(QString name);
  void callSaveWaypoints(QString path);
  void callSetAutoMode(bool autoMode);

private:
  void
  onCameraListReceived(const kratos_msgs::msg::CameraStreamList::SharedPtr msg);
  void onJoyReceived(const sensor_msgs::msg::Joy::SharedPtr msg);
  void pollWaypoints();

  rclcpp::Node::SharedPtr node_;
  QTimer *spinTimer_ = nullptr;
  QTimer *serverAliveTimer_ = nullptr;
  bool isServerOnline_ = false;

  rclcpp::Subscription<kratos_msgs::msg::CameraStreamList>::SharedPtr
      cameraSub_;
  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joySub_;

  rclcpp::Client<kratos_msgs::srv::StartCameraStream>::SharedPtr
      startStreamClient_;
  rclcpp::Client<kratos_msgs::srv::StopCameraStream>::SharedPtr
      stopStreamClient_;

  rclcpp::Client<waypoint_interfaces::srv::AddWaypoint>::SharedPtr
      addWaypointClient_;
  rclcpp::Client<waypoint_interfaces::srv::GetWaypoint>::SharedPtr
      getWaypointClient_;
  rclcpp::Client<waypoint_interfaces::srv::ListWaypoints>::SharedPtr
      listWaypointsClient_;
  rclcpp::Client<waypoint_interfaces::srv::SaveWaypoints>::SharedPtr
      saveWaypointsClient_;
  rclcpp::Client<std_srvs::srv::SetBool>::SharedPtr setAutoClient_;

  QTimer *waypointPollTimer_ = nullptr;
  bool listInFlight_ = false;
  QElapsedTimer listSentAt_;
  bool isWaypointServiceOnline_ = false;
};

// Declare to Qt meta type system
Q_DECLARE_METATYPE(CameraInfo)

#endif // ROS_WORKER_H
