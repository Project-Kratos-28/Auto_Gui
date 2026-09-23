#include "video_worker.h"

#include <QDebug>
#include <gst/video/video.h>



VideoWorker::VideoWorker(QObject *parent) : QObject(parent) {
  // Ensure GStreamer is initialized (safe to call multiple times)
  if (!gst_is_initialized()) {
    gst_init(nullptr, nullptr);
  }

  retryTimer_ = new QTimer(this);
  retryTimer_->setSingleShot(true);
  connect(retryTimer_, &QTimer::timeout, this, &VideoWorker::retryPipeline);
}

VideoWorker::~VideoWorker() { stopPipeline(); }

// ─── Start Pipeline ────────────────────────────────────────────────

void VideoWorker::startPipeline(QString host, int port) {
  lastHost_ = host;
  lastPort_ = port;
  retryCount_ = 0;
  retryPipeline();
}

void VideoWorker::retryPipeline() {
  // Tear down any existing pipeline first
  stopPipeline();

  // H265 decoding over UDP
  // Jetson sender uses: nvv4l2h265enc ! h265parse ! rtph265pay ! udpsink
  // GUI receives via UDP, extracts RTP payload, decodes with avdec_h265 (CPU)

  QString pipelineStr =
      QString("udpsrc port=%1 "
              "! application/x-rtp,media=video,encoding-name=H265,payload=96 "
              "! rtph265depay "
              "! h265parse "
              "! avdec_h265 "
              "! videoconvert "
              "! videoscale "
              "! video/x-raw,width=1280,height=720,format=RGB "
              "! appsink name=sink emit-signals=true sync=false")
          .arg(lastPort_);

  GError *error = nullptr;
  pipeline_ = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

  if (error) {
    emit pipelineError(QString("Pipeline parse error: %1").arg(error->message));
    g_error_free(error);
    return;
  }

  // Get the appsink element and wire up the new-sample callback
  GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
  if (!sink) {
    emit pipelineError("Could not find appsink element 'sink'");
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return;
  }

  // Set appsink callbacks
  GstAppSinkCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.new_sample = &VideoWorker::onNewSample;
  gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
  gst_object_unref(sink);

  // Start the pipeline
  GstStateChangeReturn ret =
      gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    // Try to get the specific error from the bus
    GstBus *bus = gst_element_get_bus(pipeline_);
    GstMessage *msg = gst_bus_poll(bus, GST_MESSAGE_ERROR, 0);
    QString errorDetailed;
    if (msg) {
      GError *err = nullptr;
      gchar *debug_info = nullptr;
      gst_message_parse_error(msg, &err, &debug_info);
      errorDetailed = QString::fromUtf8(err->message);
      qWarning() << "GStreamer Error:" << err->message
                 << "\nDebug info:" << (debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      gst_message_unref(msg);
    } else {
      errorDetailed = "no specific error on bus";
    }
    gst_object_unref(bus);
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;

    if (retryCount_ < MAX_RETRIES) {
      retryCount_++;
      qDebug() << "Pipeline start failed (" << errorDetailed
               << "), retrying in 1 second... (" << retryCount_ << "/"
               << MAX_RETRIES << ")";
      retryTimer_->start(1000); // Retry in 1 second
    } else {
      emit pipelineError(
          QString("Failed to set pipeline to PLAYING after %1 retries: %2")
              .arg(MAX_RETRIES)
              .arg(errorDetailed));
    }
  } else {
    qDebug() << "Pipeline started successfully on port" << lastPort_;
  }
}

// ─── Stop Pipeline ─────────────────────────────────────────────────

void VideoWorker::stopPipeline() {
  if (retryTimer_ && retryTimer_->isActive()) {
    retryTimer_->stop();
  }
  if (pipeline_) {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
}

// ─── GStreamer Callback (static) ───────────────────────────────────

GstFlowReturn VideoWorker::onNewSample(GstAppSink *sink, gpointer userData) {
  auto *self = static_cast<VideoWorker *>(userData);

  GstSample *sample = gst_app_sink_pull_sample(sink);
  if (!sample) {
    return GST_FLOW_ERROR;
  }

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstCaps *caps = gst_sample_get_caps(sample);

  if (!buffer || !caps) {
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Extract width/height from caps
  GstVideoInfo info;
  gst_video_info_init(&info);
  if (!gst_video_info_from_caps(&info, caps)) {
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  int width = GST_VIDEO_INFO_WIDTH(&info);
  int height = GST_VIDEO_INFO_HEIGHT(&info);

  // Map the buffer to read pixel data
  GstMapInfo mapInfo;
  if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Create a deep-copied QImage (RGB888 matches format=RGB in pipeline)
  QImage frame(mapInfo.data, width, height,
               GST_VIDEO_INFO_PLANE_STRIDE(&info, 0), QImage::Format_RGB888);
  QImage copied = frame.copy(); // deep copy — data valid after unmap

  gst_buffer_unmap(buffer, &mapInfo);
  gst_sample_unref(sample);

  emit self->newFrame(copied);

  return GST_FLOW_OK;
}
