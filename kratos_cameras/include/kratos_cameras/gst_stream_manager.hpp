#ifndef KRATOS_CAMERAS__GST_STREAM_MANAGER_HPP_
#define KRATOS_CAMERAS__GST_STREAM_MANAGER_HPP_

#include <gst/gst.h>
#include <rclcpp/rclcpp.hpp>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kratos_cameras
{

struct GstCameraPipelineConfig
{
    std::string camera_device;
    std::string format;
    int width{0};
    int height{0};
    int fps{0};
    int bitrate{0};
    std::string destination_host;
    int destination_port{0};
    std::string encoder{"av1"};
};

class GstStreamManager
{
public:
    explicit GstStreamManager(const rclcpp::Logger & logger);
    ~GstStreamManager();

    bool start_stream(const GstCameraPipelineConfig & config, std::string & message);
    bool stop_stream(const std::string & camera_device, std::string & message);
    void stop_all_streams();

    bool is_stream_active(const std::string & camera_device) const;
    std::vector<std::string> poll_and_cleanup_ended_streams();

private:
    struct StreamInstance
    {
        GstElement * pipeline{nullptr};
        GstBus * bus{nullptr};
        std::string pipeline_description;
    };

    std::string build_pipeline_description(const GstCameraPipelineConfig & config) const;
    void destroy_stream(StreamInstance & instance) const;

    rclcpp::Logger logger_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StreamInstance> streams_by_device_;
};

}  // namespace kratos_cameras

#endif  // KRATOS_CAMERAS__GST_STREAM_MANAGER_HPP_
