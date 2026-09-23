#ifndef VIDEOPLAYERWIDGET_H
#define VIDEOPLAYERWIDGET_H

#include <QImage>
#include <QLabel>
#include <QString>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "ros_worker.h"
#include "video_worker.h"

class VideoPlayerWidget : public QWidget {
  Q_OBJECT

public:
  explicit VideoPlayerWidget(const QString &cameraName, int port,
                             RosWorker *rosWorker, QWidget *parent = nullptr);
  ~VideoPlayerWidget() override;

  QString getCameraName() const { return cameraName_; }
  bool isStreaming() const { return streaming_; }

  /// Switch this player to a different camera stream (stops old, starts new).
  void switchStream(const QString &cameraName, int port);

public slots:
  void startStream();
  void stopStream();

private slots:
  void onNewFrame(QImage frame);
  void onPipelineError(QString errorMsg);

private:
  QString cameraName_;
  int port_;
  RosWorker *rosWorker_;
  bool streaming_ = false;

  VideoWorker *videoWorker_;
  QThread videoThread_;

  QLabel *videoLabel_;
  QVBoxLayout *layout_;
};

#endif // VIDEOPLAYERWIDGET_H
