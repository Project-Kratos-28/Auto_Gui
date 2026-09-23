#include "kratos_cameras/camera_stream_node.hpp"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <chrono>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace kratos_cameras
{

CameraStreamNode::CameraStreamNode(const rclcpp::NodeOptions & options)
: Node("camera_stream_node", options)
{
    RCLCPP_INFO(this->get_logger(), "Camera Node Started");
    stream_destination_host_ = this->declare_parameter<std::string>("stream.destination_host", "192.168.1.69");
    gst_stream_manager_ = std::make_unique<GstStreamManager>(this->get_logger());

    camera_info_pub_ = this->create_publisher<kratos_msgs::msg::CameraStreamList>("/kratos/available_cameras", 10);
    start_camera_stream_srv_ = this->create_service<kratos_msgs::srv::StartCameraStream>(
        "/kratos/cameras/start_stream",
        std::bind(&CameraStreamNode::handle_start_camera_stream, this, std::placeholders::_1, std::placeholders::_2));
    stop_camera_stream_srv_ = this->create_service<kratos_msgs::srv::StopCameraStream>(
        "/kratos/cameras/stop_stream",
        std::bind(&CameraStreamNode::handle_stop_camera_stream, this, std::placeholders::_1, std::placeholders::_2));
    start_all_cameras_srv_ = this->create_service<kratos_msgs::srv::StartAllCameras>(
        "/kratos/cameras/start_all",
        std::bind(&CameraStreamNode::handle_start_all_cameras, this, std::placeholders::_1, std::placeholders::_2));
    stop_all_cameras_srv_ = this->create_service<kratos_msgs::srv::StopAllCameras>(
        "/kratos/cameras/stop_all",
        std::bind(&CameraStreamNode::handle_stop_all_cameras, this, std::placeholders::_1, std::placeholders::_2));

    load_stream_profiles();

    // Initial discovery
    update_available_devices();
    publish_all_cameras();

    pub_timer_ = this->create_wall_timer(500ms, std::bind(&CameraStreamNode::publish_all_cameras, this));
    device_discovery_timer_ = this->create_wall_timer(500ms, std::bind(&CameraStreamNode::on_device_changed, this));
}

void CameraStreamNode::on_device_changed()
{
    update_available_devices();
}

void CameraStreamNode::update_available_devices()
{
    const std::filesystem::path dev_path("/dev");
    if (!std::filesystem::exists(dev_path))
    {
        RCLCPP_WARN(this->get_logger(), "/dev does not exist, no devices to discover");
        return;
    }

    // Step 1: Scan /dev for currently available video devices
    std::set<std::string> currently_available;
    const std::regex video_regex("^video\\d+$");
    for (const auto & entry : std::filesystem::directory_iterator(dev_path))
    {
        const auto filename = entry.path().filename().string();
        if (!std::regex_match(filename, video_regex))
        {
            continue;
        }

        const std::string device_path = entry.path().string();
        
        // Check if it's a real video device
        if (is_real_video_device(device_path))
        {
            currently_available.insert(device_path);
        }
    }

    std::lock_guard<std::mutex> lock(cameras_mutex_);

    // Step 2: Remove cameras whose devices are no longer available
    std::vector<std::string> to_remove;
    for (const auto & [camera_name, config] : cameras_by_name_)
    {
        if (currently_available.find(config.device) == currently_available.end())
        {
            to_remove.push_back(camera_name);
        }
    }

    for (const auto & camera_name : to_remove)
    {
        const auto & config = cameras_by_name_.at(camera_name);
        std::string stop_message;
        (void)gst_stream_manager_->stop_stream(config.device, stop_message);
        RCLCPP_WARN(this->get_logger(), "Camera device no longer available: %s (%s)", camera_name.c_str(), config.device.c_str());
        cameras_by_name_.erase(camera_name);
    }

    // Step 3: Add newly discovered devices
    for (const auto & device_path : currently_available)
    {
        // Check if any existing camera already uses this device
        bool device_exists = false;
        for (const auto & [camera_name, config] : cameras_by_name_)
        {
            if (config.device == device_path)
            {
                device_exists = true;
                break;
            }
        }
        if (!device_exists)
        {
            auto config = build_config_for_device(device_path);
            RCLCPP_INFO(this->get_logger(), "Discovered device: %s, Camera: %s, Port: %d",
                config.device.c_str(), config.name.c_str(), config.port);
            cameras_by_name_[config.name] = config;
        }
    }
}

void CameraStreamNode::publish_all_cameras()
{
    sync_stream_states_from_gst();

    std::lock_guard<std::mutex> lock(cameras_mutex_);
    kratos_msgs::msg::CameraStreamList msg;
    for (const auto & [camera_name, config] : cameras_by_name_)
    {
        kratos_msgs::msg::CameraStream stream;
        stream.camera_device = config.device;
        stream.camera_name = config.name;
        stream.resolution = std::to_string(config.resolution.width) + "x" + std::to_string(config.resolution.height);
        stream.fps = config.fps;
        stream.port = config.port;
        stream.active = config.active;
        msg.camera_streams.push_back(stream);
    }
    camera_info_pub_->publish(msg);
}

void CameraStreamNode::sync_stream_states_from_gst()
{
    const auto ended_streams = gst_stream_manager_->poll_and_cleanup_ended_streams();

    std::lock_guard<std::mutex> lock(cameras_mutex_);
    for (const auto & device : ended_streams)
    {
        // Find camera by device path
        for (auto & [camera_name, config] : cameras_by_name_)
        {
            if (config.device == device)
            {
                config.active = false;
                RCLCPP_WARN(this->get_logger(), "Detected ended stream for %s (%s), cleaned up state", camera_name.c_str(), device.c_str());
                break;
            }
        }
    }

    for (auto & [camera_name, config] : cameras_by_name_)
    {
        config.active = gst_stream_manager_->is_stream_active(config.device);
    }
}

CameraStreamConfig CameraStreamNode::build_config_for_device(const std::string & device) const
{
    auto [name, port] = get_camera_name_and_port(device);

    const auto profile = get_profile_for_camera_name(name);
    Resolution resolution{profile.resolution.width, profile.resolution.height};

    return {name, device, port, profile.fps, resolution, false, profile.bitrate, profile.format, profile.encoder};
}

void CameraStreamNode::load_stream_profiles()
{
    const auto load_profile = [this](const std::string & prefix) {
        StreamProfile profile;
        profile.bitrate = this->declare_parameter<int>(prefix + ".bitrate");
        profile.resolution.width = this->declare_parameter<int>(prefix + ".width");
        profile.resolution.height = this->declare_parameter<int>(prefix + ".height");
        profile.fps = this->declare_parameter<int>(prefix + ".fps");
        profile.format = this->declare_parameter<std::string>(prefix + ".format");
        profile.encoder = this->declare_parameter<std::string>(prefix + ".encoder", "av1");
        return profile;
    };

    normal_profile_ = load_profile("profiles.normal");
    zed_profile_ = load_profile("profiles.zed");
    gimbal_profile_ = load_profile("profiles.gimbal");

    RCLCPP_INFO(
        this->get_logger(),
        "Loaded stream profiles: normal(%dx%d@%d, %s, %d bps), zed(%dx%d@%d, %s, %d bps), gimbal(%dx%d@%d, %s, %d bps)",
        normal_profile_.resolution.width,
        normal_profile_.resolution.height,
        normal_profile_.fps,
        normal_profile_.format.c_str(),
        normal_profile_.bitrate,
        zed_profile_.resolution.width,
        zed_profile_.resolution.height,
        zed_profile_.fps,
        zed_profile_.format.c_str(),
        zed_profile_.bitrate,
        gimbal_profile_.resolution.width,
        gimbal_profile_.resolution.height,
        gimbal_profile_.fps,
        gimbal_profile_.format.c_str(),
        gimbal_profile_.bitrate);
}

StreamProfile CameraStreamNode::get_profile_for_camera_name(const std::string & camera_name) const
{
    if (camera_name == "ZED")
    {
        return zed_profile_;
    }
    if (camera_name == "GIMBAL")
    {
        return gimbal_profile_;
    }
    return normal_profile_;
}

bool CameraStreamNode::is_real_video_device(const std::string & device) const
{
    const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        return false;
    }

    v4l2_capability caps{};
    const int result = ioctl(fd, VIDIOC_QUERYCAP, &caps);
    close(fd);

    if (result < 0)
    {
        return false;
    }

    const uint32_t capabilities = caps.device_caps ? caps.device_caps : caps.capabilities;
    return (capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0u;
}

std::pair<std::string, int> CameraStreamNode::get_camera_name_and_port(const std::string & device) const
{
    // Check for special cameras first
    if (device.find("video") != std::string::npos)
    {
        const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
        {
            v4l2_capability caps{};
            if (ioctl(fd, VIDIOC_QUERYCAP, &caps) >= 0)
            {
                const std::string driver_name(reinterpret_cast<const char *>(caps.driver));
                const std::string card_name(reinterpret_cast<const char *>(caps.card));

                if (driver_name.find("zed") != std::string::npos || card_name.find("ZED") != std::string::npos)
                {
                    close(fd);
                    return {"ZED", 8998};
                }

                if (driver_name.find("obsbot") != std::string::npos || card_name.find("OBSBOT") != std::string::npos || 
                    card_name.find("Gimbal") != std::string::npos)
                {
                    close(fd);
                    return {"GIMBAL", 8999};
                }
            }
            close(fd);
        }
    }

    // For generic cameras, find the lowest available port
    const int port = get_lowest_available_port();
    const int camera_index = port - 9000 + 1;
    return {"Cam" + std::to_string(camera_index), port};
}

int CameraStreamNode::get_lowest_available_port() const
{
    // Collect all used generic camera ports (9000+)
    std::set<int> used_ports;
    for (const auto & [camera_name, config] : cameras_by_name_)
    {
        // Only track generic camera ports (9000+)
        if (config.port >= 9000)
        {
            used_ports.insert(config.port);
        }
    }

    // Find the lowest available port starting from 9000
    int port = 9000;
    while (used_ports.find(port) != used_ports.end())
    {
        ++port;
    }
    return port;
}

void CameraStreamNode::handle_start_camera_stream(
    const std::shared_ptr<kratos_msgs::srv::StartCameraStream::Request> request,
    std::shared_ptr<kratos_msgs::srv::StartCameraStream::Response> response)
{
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    const auto it = cameras_by_name_.find(request->camera_name);
    if (it == cameras_by_name_.end())
    {
        response->success = false;
        response->message = "Camera not found: " + request->camera_name;
        RCLCPP_WARN(this->get_logger(), "%s", response->message.c_str());
        return;
    }

    GstCameraPipelineConfig pipeline_config;
    pipeline_config.camera_device = it->second.device;
    pipeline_config.format = it->second.format;
    pipeline_config.width = it->second.resolution.width;
    pipeline_config.height = it->second.resolution.height;
    pipeline_config.fps = it->second.fps;
    pipeline_config.bitrate = it->second.bitrate;
    pipeline_config.destination_host = stream_destination_host_;
    pipeline_config.destination_port = it->second.port;
    pipeline_config.encoder = it->second.encoder;

    std::string manager_message;
    const bool started = gst_stream_manager_->start_stream(pipeline_config, manager_message);
    it->second.active = started;
    response->success = started;
    response->message = manager_message;
    RCLCPP_INFO(this->get_logger(), "%s", response->message.c_str());
}

void CameraStreamNode::handle_stop_camera_stream(
    const std::shared_ptr<kratos_msgs::srv::StopCameraStream::Request> request,
    std::shared_ptr<kratos_msgs::srv::StopCameraStream::Response> response)
{
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    const auto it = cameras_by_name_.find(request->camera_name);
    if (it == cameras_by_name_.end())
    {
        response->success = false;
        response->message = "Camera not found: " + request->camera_name;
        RCLCPP_WARN(this->get_logger(), "%s", response->message.c_str());
        return;
    }

    std::string manager_message;
    const bool stopped = gst_stream_manager_->stop_stream(it->second.device, manager_message);
    it->second.active = false;
    response->success = stopped;
    response->message = manager_message;
    RCLCPP_INFO(this->get_logger(), "%s", response->message.c_str());
}

void CameraStreamNode::handle_start_all_cameras(
    const std::shared_ptr<kratos_msgs::srv::StartAllCameras::Request> request,
    std::shared_ptr<kratos_msgs::srv::StartAllCameras::Response> response)
{
    (void)request;
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    bool all_ok = true;
    int started_count = 0;
    for (auto & [camera_name, config] : cameras_by_name_)
    {
        GstCameraPipelineConfig pipeline_config;
        pipeline_config.camera_device = config.device;
        pipeline_config.format = config.format;
        pipeline_config.width = config.resolution.width;
        pipeline_config.height = config.resolution.height;
        pipeline_config.fps = config.fps;
        pipeline_config.bitrate = config.bitrate;
        pipeline_config.destination_host = stream_destination_host_;
        pipeline_config.destination_port = config.port;
        pipeline_config.encoder = config.encoder;

        std::string manager_message;
        const bool started = gst_stream_manager_->start_stream(pipeline_config, manager_message);
        config.active = started;
        if (started)
        {
            ++started_count;
        }
        else
        {
            all_ok = false;
            RCLCPP_ERROR(this->get_logger(), "Failed to start %s (%s): %s", camera_name.c_str(), config.device.c_str(), manager_message.c_str());
        }
    }

    response->success = all_ok;
    response->message = "Started " + std::to_string(started_count) + " camera stream(s)";
    RCLCPP_INFO(this->get_logger(), "%s", response->message.c_str());
}

void CameraStreamNode::handle_stop_all_cameras(
    const std::shared_ptr<kratos_msgs::srv::StopAllCameras::Request> request,
    std::shared_ptr<kratos_msgs::srv::StopAllCameras::Response> response)
{
    (void)request;
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    gst_stream_manager_->stop_all_streams();
    for (auto & [camera_name, config] : cameras_by_name_)
    {
        (void)camera_name;
        config.active = false;
    }

    response->success = true;
    response->message = "Stopped all camera streams";
    RCLCPP_INFO(this->get_logger(), "%s", response->message.c_str());
}

}  // namespace kratos_cameras



