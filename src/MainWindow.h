#pragma once

#include <QMainWindow>
#include <QActionGroup>

#include "AudioCapture.h"

class StereoVUMeterWidget;
class QCloseEvent;
class QMenu;
class QAction;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(const AudioCapture::Options& options, QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private slots:
    void onDeviceSelected(QAction* action);
    void onReferenceSelected(QAction* action);
    void refreshDeviceMenu();
    void showAbout();

  private:
    void createMenuBar();
    void populateDeviceMenu();
    void populateReferenceMenu();

    AudioCapture audio_;
    StereoVUMeterWidget* meter_ = nullptr;
    
    // Menu components
    QMenu* audioMenu_ = nullptr;
    QMenu* deviceMenu_ = nullptr;
    QMenu* referenceMenu_ = nullptr;
    QActionGroup* deviceActionGroup_ = nullptr;
    QActionGroup* referenceActionGroup_ = nullptr;
};
