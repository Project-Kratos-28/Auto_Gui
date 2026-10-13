#ifndef SPECTROMETERWIDGET_H
#define SPECTROMETERWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

QT_CHARTS_USE_NAMESPACE

class SpectrometerWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrometerWidget(QWidget *parent = nullptr);
    ~SpectrometerWidget();

public slots:
    void onSpectrometerData(const QVector<double>& data);

private slots:
    void toggleRecording();

private:
    QLineSeries *series_;
    QScatterSeries *scatter_;
    QPushButton *btn_record_;
    
    bool is_recording_ = false;
    QFile csv_file_;
    QTextStream csv_writer_;
    QVector<int> wavelengths_;
};

#endif // SPECTROMETERWIDGET_H