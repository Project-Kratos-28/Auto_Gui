#include "videoplayerwidget.h"
#include <QDebug>

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
      Q_ARG(QString, "192.168.1.10"), Q_ARG(int, port_));
}

void VideoPlayerWidget::stopStream() {
  streaming_ = false;
  QMetaObject::invokeMethod(videoWorker_, "stopPipeline",
                            Qt::QueuedConnection);

  videoLabel_->setText("NO VIDEO FEED");
  videoLabel_->setPixmap(QPixmap()); // Clear the image
}

void VideoPlayerWidget::switchStream(const QString &cameraName, int port) {
  if (streaming_) {
    stopStream();
  }
  cameraName_ = cameraName;
  port_ = port;
  startStream();
}

void VideoPlayerWidget::onNewFrame(QImage frame) {
  videoLabel_->setPixmap(QPixmap::fromImage(frame).scaled(
      videoLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VideoPlayerWidget::onPipelineError(QString errorMsg) {
  qWarning() << "Pipeline error for" << cameraName_ << ":" << errorMsg;
  videoLabel_->setText("VIDEO ERROR\n" + errorMsg);
}
