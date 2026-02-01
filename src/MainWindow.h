#pragma once

#include <QActionGroup>
#include <QMainWindow>

#include "AudioCapture.h"
#include "SkinManager.h"

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
    void onVectorStyleSelected(QAction* action);
    void onSkinSelected(QAction* action);
    void importSkin();
    void refreshDeviceMenu();
    void showAbout();

  private:
    void createMenuBar();
    void populateDeviceMenu();
    void populateReferenceMenu();
    void populateStyleMenu();

    AudioCapture audio_;
    StereoVUMeterWidget* meter_ = nullptr;

    SkinManager skinManager_;

    // Menu components
    QMenu* audioMenu_ = nullptr;
    QMenu* deviceMenu_ = nullptr;
    QMenu* referenceMenu_ = nullptr;
    QMenu* styleMenu_ = nullptr;
    QMenu* vectorStyleMenu_ = nullptr;
    QMenu* skinStyleMenu_ = nullptr;
    QActionGroup* deviceActionGroup_ = nullptr;
    QActionGroup* referenceActionGroup_ = nullptr;
    QActionGroup* vectorStyleActionGroup_ = nullptr;
    QActionGroup* skinStyleActionGroup_ = nullptr;
};
