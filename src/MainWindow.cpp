#include "MainWindow.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QTimer>

#include "StereoVUMeterWidget.h"

MainWindow::MainWindow(const AudioCapture::Options& options, QWidget* parent) : QMainWindow(parent), audio_(options) {
    setWindowTitle("Analog VU Meter");

    meter_ = new StereoVUMeterWidget(this);
    setCentralWidget(meter_);

    resize(820, 340);
    setMinimumSize(680, 280);

    QString err;
    if (!audio_.start(&err)) {
        // Show warning but continue - widget should still appear
        QMessageBox::warning(
            this, "Audio capture error",
            QString("Audio initialization failed: %1\n\nThe VU meter will be displayed but won't show audio levels.")
                .arg(err));
    }

    auto* timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this]() { meter_->setLevels(audio_.leftVuDb(), audio_.rightVuDb()); });
    timer->start();
}

MainWindow::~MainWindow() { audio_.stop(); }

void MainWindow::closeEvent(QCloseEvent* event) {
    audio_.stop();
    QMainWindow::closeEvent(event);
}
