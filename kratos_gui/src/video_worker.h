#ifndef VIDEO_WORKER_H
#define VIDEO_WORKER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

class VideoWorker : public QObject {
  Q_OBJECT
public:
  explicit VideoWorker(QObject *parent = nullptr);
  ~VideoWorker() override;

  /// Call (from the GUI thread) once a frame from newFrame() has been shown.
  /// Frames are dropped while too many are still waiting for the GUI.
  void frameConsumed() { --framesInFlight_; }

signals:
  /// Emitted every time a decoded frame is ready.
  void newFrame(QImage frame);

  /// Emitted on pipeline errors.
  void pipelineError(QString errorMsg);

public slots:
  /// Build and start a receive pipeline matching the rover's encoder:
  /// "h265" = RTP over UDP on `port`, "av1" = Matroska from the rover's
  /// tcpserversink at `host`:`port`.
  void startPipeline(QString host, int port, QString encoder);

  /// Stop and tear down the current pipeline.
  void stopPipeline();

private slots:
  /// Retry connecting the pipeline
  void retryPipeline();

private:
  static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);

  GstElement *pipeline_ = nullptr;
  QTimer *retryTimer_ = nullptr;
  QString lastHost_;
  int lastPort_ = 0;
  QString lastEncoder_;
  int retryCount_ = 0;
  std::atomic<int> framesInFlight_{0};
  const int MAX_RETRIES = 5;
};

#endif // VIDEO_WORKER_H
