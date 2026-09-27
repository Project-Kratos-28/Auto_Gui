#include "videoplayerwidget.h"
#include <QDebug>

// Rover (Jetson) address, needed for the AV1 streams' TCP connection.
// UDP (H.265) streams are pushed to us, so they don't use it.
static QString roverHost() {
  const QString env = qEnvironmentVariable("KRATOS_ROVER_HOST");
  return env.isEmpty() ? QStringLiteral("192.168.1.10") : env;
}

VideoPlayerWidget::VideoPlayerWidget(const QString &cameraName, int port,
                                     RosWorker *rosWorker, QWidget *parent)
    : QWidget(parent), cameraName_(cameraName), port_(port),
      rosWorker_(rosWorker) {

  // Setup UI
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);

  videoLabel_ = new QLabel("NO VIDEO FEED", this);
  videoLabel_->setAlignment(Qt::AlignCenter);
  videoLabel_->setStyleSheet(R"(
      QLabel { 
          background-color: #121212; 
          color: #555555; 
          font-family: 'Roboto', sans-serif; 
          font-size: 14px; 
      }
  )");
  layout_->addWidget(videoLabel_);

  // Setup generic Video Worker
  videoWorker_ = new VideoWorker();
  videoWorker_->moveToThread(&videoThread_);
  connect(&videoThread_, &QThread::finished, videoWorker_,
          &QObject::deleteLater);

  connect(videoWorker_, &VideoWorker::newFrame, this,
          &VideoPlayerWidget::onNewFrame, Qt::QueuedConnection);
  connect(videoWorker_, &VideoWorker::pipelineError, this,
          &VideoPlayerWidget::onPipelineError, Qt::QueuedConnection);

  videoThread_.start();
}

VideoPlayerWidget::~VideoPlayerWidget() {
  stopStream();

  videoThread_.quit();
  videoThread_.wait();
}

void VideoPlayerWidget::startStream() {
  videoLabel_->setText(QString("CONNECTING TO %1...").arg(cameraName_));
  streaming_ = true;

  QMetaObject::invokeMethod(
      videoWorker_, "startPipeline", Qt::QueuedConnection,
      Q_ARG(QString, roverHost()), Q_ARG(int, port_),
      Q_ARG(QString, encoder_));
}

void VideoPlayerWidget::stopStream() {
  streaming_ = false;
  QMetaObject::invokeMethod(videoWorker_, "stopPipeline",
                            Qt::QueuedConnection);

  videoLabel_->setText("NO VIDEO FEED");
  videoLabel_->setPixmap(QPixmap()); // Clear the image
}

void VideoPlayerWidget::switchStream(const QString &cameraName, int port,
                                     const QString &encoder) {
  if (streaming_) {
    stopStream();
  }
  cameraName_ = cameraName;
  port_ = port;
  encoder_ = encoder;
  startStream();
}

void VideoPlayerWidget::onNewFrame(QImage frame) {
  videoWorker_->frameConsumed();
  if (!streaming_) {
    return; // late frame after stop; keep the placeholder
  }
  videoLabel_->setPixmap(QPixmap::fromImage(frame).scaled(
      videoLabel_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void VideoPlayerWidget::onPipelineError(QString errorMsg) {
  qWarning() << "Pipeline error for" << cameraName_ << ":" << errorMsg;
  videoLabel_->setText("VIDEO ERROR\n" + errorMsg);
}
