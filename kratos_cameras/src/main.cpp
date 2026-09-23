#include <rclcpp/rclcpp.hpp>
#include "kratos_cameras/camera_stream_node.hpp"

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<kratos_cameras::CameraStreamNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
