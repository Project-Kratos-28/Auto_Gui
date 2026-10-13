#include "spectrometerwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

SpectrometerWidget::SpectrometerWidget(QWidget *parent) : QWidget(parent) {
    wavelengths_ = {410, 435, 460, 485, 510, 535, 560, 585, 610, 645, 680, 705, 730, 760, 810, 860, 900, 940};
    
    QChart *chart = new QChart();
    series_ = new QLineSeries();
    scatter_ = new QScatterSeries();
    scatter_->setMarkerSize(8.0);
    
    chart->addSeries(series_);
    chart->addSeries(scatter_);
    chart->setTitle("Real-Time Spectrum");

    QValueAxis *axisX = new QValueAxis();
    axisX->setTitleText("Wavelength (nm)");
    axisX->setRange(400, 960);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText("Absorbance (AU)");
    axisY->setRange(0, 3);

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);

    series_->attachAxis(axisX);
    series_->attachAxis(axisY);
    scatter_->attachAxis(axisX);
    scatter_->attachAxis(axisY);

    QChartView *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);

    btn_record_ = new QPushButton("Start Recording");
    connect(btn_record_, &QPushButton::clicked, this, &SpectrometerWidget::toggleRecording);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(chartView);
    mainLayout->addWidget(btn_record_);
}

SpectrometerWidget::~SpectrometerWidget() {
    if (is_recording_ && csv_file_.isOpen()) {
        csv_file_.close();
    }
}

void SpectrometerWidget::onSpectrometerData(const QVector<double>& data) {
    if (data.size() != 18) return;

    QVector<QPointF> points;
    for (int i = 0; i < 18; ++i) {
        points.append(QPointF(wavelengths_[i], data[i]));
    }
    series_->replace(points);
    scatter_->replace(points);

    if (is_recording_ && csv_file_.isOpen()) {
        csv_writer_ << QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        for (double val : data) {
            csv_writer_ << "," << val;
        }
        csv_writer_ << "\n";
        csv_writer_.flush();
    }
}

void SpectrometerWidget::toggleRecording() {
    if (!is_recording_) {
        QString filename = "spectrometer_data_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
        csv_file_.setFileName(filename);
        if (csv_file_.open(QIODevice::WriteOnly | QIODevice::Text)) {
            csv_writer_.setDevice(&csv_file_);
            
            csv_writer_ << "Time";
            for (int w : wavelengths_) {
                csv_writer_ << "," << w << "nm";
            }
            csv_writer_ << "\n";
            csv_writer_.flush();

            is_recording_ = true;
            btn_record_->setText("Stop Recording");
        }
    } else {
        is_recording_ = false;
        csv_file_.close();
        btn_record_->setText("Start Recording");
    }
}