#ifndef VIDEO_WORKER_H
#define VIDEO_WORKER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

class VideoWorker : public QObject {
  Q_OBJECT
public:
  explicit VideoWorker(QObject *parent = nullptr);
  ~VideoWorker() override;

signals:
  /// Emitted every time a decoded frame is ready.
  void newFrame(QImage frame);

  /// Emitted on pipeline errors.
  void pipelineError(QString errorMsg);

public slots:
  /// Build and start a GStreamer TCP client pipeline for AV1.
  void startPipeline(QString host, int port);

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
  int retryCount_ = 0;
  const int MAX_RETRIES = 5;
};

#endif // VIDEO_WORKER_H
