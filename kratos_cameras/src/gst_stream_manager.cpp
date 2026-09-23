#include "kratos_cameras/gst_stream_manager.hpp"

#include <sstream>

namespace kratos_cameras {

namespace {

void log_negotiation_hint_if_needed(const rclcpp::Logger &logger,
                                    const std::string &camera_device,
                                    const std::string &debug_info,
                                    const std::string &pipeline_description) {
  if (debug_info.find("not-negotiated") != std::string::npos) {
    RCLCPP_ERROR(logger,
                 "Likely caps negotiation failure for %s. Check camera "
                 "format/resolution/fps in config and plugin compatibility.",
                 camera_device.c_str());
    RCLCPP_ERROR(logger, "Active pipeline for %s: %s", camera_device.c_str(),
                 pipeline_description.c_str());
  }
}

} // namespace

GstStreamManager::GstStreamManager(const rclcpp::Logger &logger)
    : logger_(logger) {
  gst_init(nullptr, nullptr);
}

GstStreamManager::~GstStreamManager() { stop_all_streams(); }

bool GstStreamManager::start_stream(const GstCameraPipelineConfig &config,
                                    std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (streams_by_device_.find(config.camera_device) !=
      streams_by_device_.end()) {
    message = "Stream already active for device: " + config.camera_device;
    return true;
  }

  const std::string pipeline_description = build_pipeline_description(config);
  GError *error = nullptr;
  GstElement *pipeline = gst_parse_launch(pipeline_description.c_str(), &error);

  if (pipeline == nullptr) {
    const std::string gst_error =
        (error != nullptr && error->message != nullptr) ? error->message
                                                        : "unknown error";
    message = "Failed to create pipeline for " + config.camera_device + ": " +
              gst_error;
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }

  GstBus *bus = gst_element_get_bus(pipeline);
  const GstStateChangeReturn ret =
      gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    message = "Failed to set pipeline to PLAYING for " + config.camera_device;
    if (bus != nullptr) {
      gst_object_unref(bus);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return false;
  }

  streams_by_device_[config.camera_device] = {pipeline, bus,
                                              pipeline_description};
  message = "Stream started for " + config.camera_device;

  RCLCPP_INFO(logger_, "Started GStreamer pipeline for %s: %s",
              config.camera_device.c_str(), pipeline_description.c_str());
  return true;
}

bool GstStreamManager::stop_stream(const std::string &camera_device,
                                   std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = streams_by_device_.find(camera_device);
  if (it == streams_by_device_.end()) {
    message = "No active stream for device: " + camera_device;
    return false;
  }

  destroy_stream(it->second);
  streams_by_device_.erase(it);
  message = "Stream stopped for " + camera_device;
  return true;
}

void GstStreamManager::stop_all_streams() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &[camera_device, instance] : streams_by_device_) {
    (void)camera_device;
    destroy_stream(instance);
  }
  streams_by_device_.clear();
}

bool GstStreamManager::is_stream_active(
    const std::string &camera_device) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return streams_by_device_.find(camera_device) != streams_by_device_.end();
}

std::vector<std::string> GstStreamManager::poll_and_cleanup_ended_streams() {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> ended_devices;
  for (auto it = streams_by_device_.begin(); it != streams_by_device_.end();) {
    bool should_remove = false;

    if (it->second.bus != nullptr) {
      while (GstMessage *msg = gst_bus_pop_filtered(
                 it->second.bus,
                 static_cast<GstMessageType>(
                     GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS |
                     GST_MESSAGE_STATE_CHANGED))) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
          GError *err = nullptr;
          gchar *debug_info = nullptr;
          gst_message_parse_error(msg, &err, &debug_info);

          const std::string source_name =
              (GST_MESSAGE_SRC(msg) != nullptr &&
               GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) != nullptr)
                  ? GST_OBJECT_NAME(GST_MESSAGE_SRC(msg))
                  : "unknown-source";
          const std::string error_message =
              (err != nullptr && err->message != nullptr) ? err->message
                                                          : "unknown";
          const std::string debug_message =
              (debug_info != nullptr) ? debug_info : "no-debug-info";

          RCLCPP_ERROR(
              logger_, "GStreamer error on %s from %s: %s (domain=%u, code=%d)",
              it->first.c_str(), source_name.c_str(), error_message.c_str(),
              (err != nullptr) ? err->domain : 0U,
              (err != nullptr) ? err->code : 0);
          RCLCPP_ERROR(logger_, "GStreamer debug on %s: %s", it->first.c_str(),
                       debug_message.c_str());
          RCLCPP_ERROR(logger_, "Pipeline context for %s: %s",
                       it->first.c_str(),
                       it->second.pipeline_description.c_str());

          log_negotiation_hint_if_needed(logger_, it->first, debug_message,
                                         it->second.pipeline_description);

          if (err != nullptr) {
            g_error_free(err);
          }
          if (debug_info != nullptr) {
            g_free(debug_info);
          }
          should_remove = true;
          break;
        }
        case GST_MESSAGE_WARNING: {
          GError *warn = nullptr;
          gchar *debug_info = nullptr;
          gst_message_parse_warning(msg, &warn, &debug_info);

          const std::string source_name =
              (GST_MESSAGE_SRC(msg) != nullptr &&
               GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) != nullptr)
                  ? GST_OBJECT_NAME(GST_MESSAGE_SRC(msg))
                  : "unknown-source";
          const std::string warning_message =
              (warn != nullptr && warn->message != nullptr) ? warn->message
                                                            : "unknown";

          RCLCPP_WARN(logger_, "GStreamer warning on %s from %s: %s",
                      it->first.c_str(), source_name.c_str(),
                      warning_message.c_str());
          if (debug_info != nullptr) {
            RCLCPP_WARN(logger_, "GStreamer warning debug on %s: %s",
                        it->first.c_str(), debug_info);
          }

          if (warn != nullptr) {
            g_error_free(warn);
          }
          if (debug_info != nullptr) {
            g_free(debug_info);
          }
          break;
        }
        case GST_MESSAGE_EOS:
          RCLCPP_WARN(logger_, "GStreamer stream ended (EOS) for %s",
                      it->first.c_str());
          should_remove = true;
          break;
        case GST_MESSAGE_STATE_CHANGED: {
          if (GST_MESSAGE_SRC(msg) == GST_OBJECT(it->second.pipeline)) {
            GstState old_state = GST_STATE_NULL;
            GstState new_state = GST_STATE_NULL;
            GstState pending = GST_STATE_VOID_PENDING;
            gst_message_parse_state_changed(msg, &old_state, &new_state,
                                            &pending);
            RCLCPP_DEBUG(
                logger_, "Pipeline state for %s: %s -> %s (pending=%s)",
                it->first.c_str(), gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state),
                gst_element_state_get_name(pending));
            if (new_state == GST_STATE_NULL) {
              RCLCPP_WARN(
                  logger_,
                  "GStreamer pipeline moved to NULL unexpectedly for %s",
                  it->first.c_str());
              should_remove = true;
            }
          }
          break;
        }
        default:
          break;
        }

        gst_message_unref(msg);
        if (should_remove) {
          break;
        }
      }
    }

    if (should_remove) {
      ended_devices.push_back(it->first);
      destroy_stream(it->second);
      it = streams_by_device_.erase(it);
    } else {
      ++it;
    }
  }

  return ended_devices;
}

std::string GstStreamManager::build_pipeline_description(
    const GstCameraPipelineConfig &config) const {
  std::ostringstream ss;
  ss << "v4l2src device=" << config.camera_device << " ! ";
  
  if (config.format == "MJPG") {
      ss << "image/jpeg,width=" << config.width << ",height=" << config.height
         << ",framerate=" << config.fps << "/1 ! "
         << "nvv4l2decoder mjpeg=1 ! "
         << "nvvidconv ! "
         << "video/x-raw(memory:NVMM),format=NV12 ! ";
  } else {
      ss << "video/x-raw,format=" << config.format << ",width=" << config.width << ",height=" << config.height
         << ",framerate=" << config.fps << "/1 ! "
         << "nvvidconv ! "
         << "video/x-raw(memory:NVMM),format=NV12 ! ";
  }

  if (config.encoder == "h265") {
    ss << "nvv4l2h265enc "
       << "bitrate=" << config.bitrate * 1000 << " "
       << "iframeinterval=" << config.fps << " "
       << "insert-sps-pps=1 "
       << "control-rate=1 "
       << "preset-level=1 "
       << "EnableTwopassCBR=0 ! "
       << "h265parse ! "
       << "rtph265pay pt=96 config-interval=1 mtu=1200 ! "
       << "udpsink host=" << config.destination_host
       << " port=" << config.destination_port << " sync=false async=false";
  } else {
    // Default to AV1
    ss << "nvv4l2av1enc "
       << "bitrate=" << config.bitrate * 1000 << " "
       << "control-rate=1 "
       << "iframeinterval=" << config.fps << " "
       << "insert-seq-hdr=1 ! "
       << "av1parse ! "
       << "matroskamux streamable=true ! "
       << "tcpserversink host=0.0.0.0"
       << " port=" << config.destination_port << " sync=false async=false";
  }

  return ss.str();
}

void GstStreamManager::destroy_stream(StreamInstance &instance) const {
  if (instance.pipeline != nullptr) {
    gst_element_set_state(instance.pipeline, GST_STATE_NULL);
  }
  if (instance.bus != nullptr) {
    gst_object_unref(instance.bus);
    instance.bus = nullptr;
  }
  if (instance.pipeline != nullptr) {
    gst_object_unref(instance.pipeline);
    instance.pipeline = nullptr;
  }
}

} // namespace kratos_cameras
