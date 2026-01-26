#pragma once

#include <QMainWindow>

#include "AudioCapture.h"

class StereoVUMeterWidget;
class QCloseEvent;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(const AudioCapture::Options& options, QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    AudioCapture audio_;
    StereoVUMeterWidget* meter_ = nullptr;
};
