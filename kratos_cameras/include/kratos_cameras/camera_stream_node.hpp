#ifndef KRATOS_CAMERAS__CAMERA_STREAM_NODE_HPP_
#define KRATOS_CAMERAS__CAMERA_STREAM_NODE_HPP_

#include <rclcpp/rclcpp.hpp>

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>

#include "kratos_msgs/msg/camera_stream.hpp"
#include "kratos_msgs/msg/camera_stream_list.hpp"
#include "kratos_msgs/srv/start_camera_stream.hpp"
#include "kratos_msgs/srv/stop_camera_stream.hpp"
#include "kratos_msgs/srv/start_all_cameras.hpp"
#include "kratos_msgs/srv/stop_all_cameras.hpp"

#include "kratos_cameras/gst_stream_manager.hpp"


using namespace std::chrono_literals;
namespace kratos_cameras
{

struct Resolution
{
    int width{0};
    int height{0};
};

struct StreamProfile
{
    int bitrate{0};
    Resolution resolution;
    int fps{0};
    std::string format;
    std::string encoder{"av1"};
};

struct CameraStreamConfig
{
    std::string name;
    std::string device;
    int port{0};
    int fps{0};
    Resolution resolution;
    bool active;
    int bitrate{0};
    std::string format;
    std::string encoder{"av1"};
};

class CameraStreamNode : public rclcpp::Node
{
public:
    explicit CameraStreamNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
    void on_device_changed();
    void update_available_devices();
    void publish_all_cameras();
    void sync_stream_states_from_gst();
    CameraStreamConfig build_config_for_device(const std::string & device) const;
    void load_stream_profiles();
    StreamProfile get_profile_for_camera_name(const std::string & camera_name) const;
    bool is_real_video_device(const std::string & device) const;
    std::pair<std::string, int> get_camera_name_and_port(const std::string & device) const;
    int get_lowest_available_port() const;

    void handle_start_camera_stream(
        const std::shared_ptr<kratos_msgs::srv::StartCameraStream::Request> request,
        std::shared_ptr<kratos_msgs::srv::StartCameraStream::Response> response);
    void handle_stop_camera_stream(
        const std::shared_ptr<kratos_msgs::srv::StopCameraStream::Request> request,
        std::shared_ptr<kratos_msgs::srv::StopCameraStream::Response> response);
    void handle_start_all_cameras(
        const std::shared_ptr<kratos_msgs::srv::StartAllCameras::Request> request,
        std::shared_ptr<kratos_msgs::srv::StartAllCameras::Response> response);
    void handle_stop_all_cameras(
        const std::shared_ptr<kratos_msgs::srv::StopAllCameras::Request> request,
        std::shared_ptr<kratos_msgs::srv::StopAllCameras::Response> response);

    rclcpp::Publisher<kratos_msgs::msg::CameraStreamList>::SharedPtr camera_info_pub_;
    rclcpp::Service<kratos_msgs::srv::StartCameraStream>::SharedPtr start_camera_stream_srv_;
    rclcpp::Service<kratos_msgs::srv::StopCameraStream>::SharedPtr stop_camera_stream_srv_;
    rclcpp::Service<kratos_msgs::srv::StartAllCameras>::SharedPtr start_all_cameras_srv_;
    rclcpp::Service<kratos_msgs::srv::StopAllCameras>::SharedPtr stop_all_cameras_srv_;
    rclcpp::TimerBase::SharedPtr pub_timer_;
    rclcpp::TimerBase::SharedPtr device_discovery_timer_;
    std::unordered_map<std::string, CameraStreamConfig> cameras_by_name_;
    StreamProfile normal_profile_;
    StreamProfile zed_profile_;
    StreamProfile gimbal_profile_;
    std::string stream_destination_host_;
    std::unique_ptr<GstStreamManager> gst_stream_manager_;
    
    mutable std::mutex cameras_mutex_;
};

}  // namespace kratos_cameras

#endif  // KRATOS_CAMERAS__CAMERA_STREAM_NODE_HPP_
