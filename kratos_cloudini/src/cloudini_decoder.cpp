// cloudini_decoder: laptop half of the Cloudini link.
//
// Subscribes to a point_cloud_interfaces/msg/CompressedPointCloud2 topic that
// the Orin's encoder publishes, decompresses it with cloudini_lib and
// republishes a normal sensor_msgs/msg/PointCloud2 for the GUI's RViz.
//
// Why not cloudini_ros's own cloudini_topic_converter: it picks its QoS once,
// at startup, from whoever is already publishing. The GUI normally starts
// before the Orin's encoder, so it would guess, and a wrong guess means the
// laptop silently never receives anything. Here the QoS is set explicitly per
// topic, so start order doesn't matter.
//
// Like the converter, it works on the raw serialized (CDR) messages: the
// compressed message is never deserialized into a C++ struct and the output
// is written straight into a serialized PointCloud2.

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "cloudini_lib/ros_msg_utils.hpp"

class CloudiniDecoder : public rclcpp::Node {
public:
  CloudiniDecoder() : Node("cloudini_decoder") {
    const auto input = declare_parameter<std::string>("input_topic", "");
    const auto output = declare_parameter<std::string>("output_topic", "");
    // Input: best_effort accepts both reliable and best-effort publishers and
    // never stalls on retransmissions (right for the live scan over radio).
    // reliable only matches a reliable publisher but retransmits lost
    // fragments (right for the big, infrequent map).
    const bool reliable = declare_parameter<bool>("reliable", false);
    // Output: transient_local keeps the last cloud for subscribers that join
    // later (RViz reopened, display re-enabled). GLIM only publishes the map
    // when a submap is added, so without this a late RViz waits minutes.
    latched_ = declare_parameter<bool>("latch_output", false);
    stats_period_ = declare_parameter<double>("stats_period", 10.0);

    if (input.empty() || output.empty()) {
      throw std::runtime_error("input_topic and output_topic must both be set");
    }

    rclcpp::QoS in_qos(rclcpp::KeepLast(1));
    reliable ? in_qos.reliable() : in_qos.best_effort();
    in_qos.durability_volatile();

    rclcpp::QoS out_qos(rclcpp::KeepLast(1));
    out_qos.reliable();
    latched_ ? out_qos.transient_local() : out_qos.durability_volatile();

    sub_ = create_generic_subscription(
        input, "point_cloud_interfaces/msg/CompressedPointCloud2", in_qos,
        [this](std::shared_ptr<rclcpp::SerializedMessage> msg) { onCompressed(*msg); });
    pub_ = create_generic_publisher(output, "sensor_msgs/msg/PointCloud2", out_qos);

    RCLCPP_INFO(get_logger(), "%s (%s) -> %s (%s)", input.c_str(),
                reliable ? "reliable" : "best_effort", output.c_str(),
                latched_ ? "latched" : "volatile");

    if (stats_period_ > 0.0) {
      stats_timer_ = create_wall_timer(
          std::chrono::duration<double>(stats_period_), [this]() { logStats(); });
    }
  }

private:
  void onCompressed(const rclcpp::SerializedMessage &msg) {
    // A volatile output nobody reads isn't worth decompressing. A latched one
    // is: it must hold the latest cloud for whoever subscribes next.
    if (!latched_ && pub_->get_subscription_count() == 0) {
      return;
    }

    const auto &raw = msg.get_rcl_serialized_message();
    try {
      const auto info = cloudini_ros::getDeserializedPointCloudMessage(
          Cloudini::ConstBufferView(raw.buffer, raw.buffer_length));
      cloudini_ros::convertCompressedCloudToPointCloud2(info, decoded_);
    } catch (const std::exception &e) {
      // Most likely an encoder on a newer Cloudini than this laptop
      // (unsupported encoding version). Say so, but don't flood the log.
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000,
                            "cannot decompress: %s", e.what());
      ++failed_;
      return;
    }

    // Publish decoded_ in place instead of copying a multi-MB cloud. The
    // SerializedMessage would free the buffer on destruction, so hand it back
    // first, even if publish() throws.
    rclcpp::SerializedMessage out;
    auto &out_raw = out.get_rcl_serialized_message();
    struct Unborrow {
      rcl_serialized_message_t &m;
      ~Unborrow() {
        m.buffer = nullptr;
        m.buffer_length = 0;
        m.buffer_capacity = 0;
      }
    } unborrow{out_raw};
    out_raw.buffer = decoded_.data();
    out_raw.buffer_length = decoded_.size();
    out_raw.buffer_capacity = decoded_.size();
    pub_->publish(out);

    ++decoded_count_;
    compressed_bytes_ += raw.buffer_length;
    decoded_bytes_ += decoded_.size();
  }

  void logStats() {
    if (decoded_count_ == 0 && failed_ == 0) {
      return;
    }
    const std::string failures =
        failed_ ? ", " + std::to_string(failed_) + " failed" : "";
    RCLCPP_INFO(get_logger(),
                "%zu clouds in %.0f s, %.2f MB received -> %.2f MB decoded "
                "(ratio %.2f)%s",
                decoded_count_, stats_period_, compressed_bytes_ / 1e6,
                decoded_bytes_ / 1e6,
                decoded_bytes_ ? double(compressed_bytes_) / decoded_bytes_ : 0.0,
                failures.c_str());
    decoded_count_ = failed_ = 0;
    compressed_bytes_ = decoded_bytes_ = 0;
  }

  rclcpp::GenericSubscription::SharedPtr sub_;
  rclcpp::GenericPublisher::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  std::vector<uint8_t> decoded_; // reused so each cloud doesn't reallocate
  bool latched_ = false;
  double stats_period_ = 10.0;

  size_t decoded_count_ = 0;
  size_t failed_ = 0;
  size_t compressed_bytes_ = 0;
  size_t decoded_bytes_ = 0;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CloudiniDecoder>());
  rclcpp::shutdown();
  return 0;
}
