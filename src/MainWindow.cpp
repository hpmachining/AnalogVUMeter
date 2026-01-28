#include "MainWindow.h"

#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>

#include "StereoVUMeterWidget.h"
#include "version.h"

MainWindow::MainWindow(const AudioCapture::Options& options, QWidget* parent) : QMainWindow(parent), audio_(options) {
    setWindowTitle("Analog VU Meter");

    meter_ = new StereoVUMeterWidget(this);
    setCentralWidget(meter_);

    resize(820, 340);
    setMinimumSize(680, 280);

    // Create the menu bar
    createMenuBar();

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

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = this->menuBar();

    // Audio menu
    audioMenu_ = menuBar->addMenu(tr("&Audio"));
    
    // Input Device submenu
    deviceMenu_ = audioMenu_->addMenu(tr("&Input Device"));
    
    // Create action group for exclusive device selection
    deviceActionGroup_ = new QActionGroup(this);
    deviceActionGroup_->setExclusive(true);
    connect(deviceActionGroup_, &QActionGroup::triggered, this, &MainWindow::onDeviceSelected);
    
    // Populate the device menu
    populateDeviceMenu();
    
    // dBFS Reference submenu
    referenceMenu_ = audioMenu_->addMenu(tr("d&BFS Reference"));
    
    // Create action group for exclusive reference selection
    referenceActionGroup_ = new QActionGroup(this);
    referenceActionGroup_->setExclusive(true);
    connect(referenceActionGroup_, &QActionGroup::triggered, this, &MainWindow::onReferenceSelected);
    
    // Populate the reference menu
    populateReferenceMenu();
    
    // Add separator and refresh action
    audioMenu_->addSeparator();
    QAction* refreshAction = audioMenu_->addAction(tr("&Refresh Devices"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshDeviceMenu);

    // About action - Qt automatically moves this to the app menu on macOS
    QAction* aboutAction = new QAction(tr("About Analog VU Meter"), this);
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    audioMenu_->addAction(aboutAction);
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this,
        tr("About Analog VU Meter"),
        tr("<h3>Analog VU Meter</h3>"
           "<p>MacOS Version %1</p>"
           "<p>A real-time audio level meter with classic analog VU meter styling. "
           "Originally built for Linux by Paul Hentschel. "
           "Ported to MacOS by GitHub user jansenkarl.</p>"
           "<p><b>MIT License</b><br>"
           "Copyright (c) 2026 Paul Hentschel</p>")
            .arg(APP_VERSION));
}

void MainWindow::populateDeviceMenu() {
    // Clear existing actions from device menu
    deviceMenu_->clear();
    
    // Remove old actions from action group
    for (QAction* action : deviceActionGroup_->actions()) {
        deviceActionGroup_->removeAction(action);
    }
    
    // Get list of input devices
    QList<AudioCapture::DeviceInfo> devices = AudioCapture::enumerateInputDevices();
    
    QString currentUID = audio_.currentDeviceUID();
    
    for (const AudioCapture::DeviceInfo& device : devices) {
        QString displayName = device.name;
        if (device.isDefault) {
            displayName += tr(" (Default)");
        }
        
        QAction* action = deviceMenu_->addAction(displayName);
        action->setCheckable(true);
        action->setData(device.uid);  // Store UID in action data
        deviceActionGroup_->addAction(action);
        
        // Check if this is the currently selected device
        if (device.uid == currentUID) {
            action->setChecked(true);
        }
    }
    
    // If no device is checked (e.g., using default), check the default one
    if (!deviceActionGroup_->checkedAction()) {
        for (QAction* action : deviceActionGroup_->actions()) {
            // Find and check the default device
            for (const AudioCapture::DeviceInfo& device : devices) {
                if (device.uid == action->data().toString() && device.isDefault) {
                    action->setChecked(true);
                    break;
                }
            }
        }
    }
}

void MainWindow::populateReferenceMenu() {
    // dBFS reference values: +6 to -20 in 2 dB steps
    const int referenceValues[] = {6, 4, 2, 0, -2, -4, -6, -8, -10, -12, -14, -16, -18, -20};
    
    double currentRef = audio_.referenceDbfs();
    
    for (int value : referenceValues) {
        QString displayName = QString("%1 dB").arg(value);
        
        QAction* action = referenceMenu_->addAction(displayName);
        action->setCheckable(true);
        action->setData(value);  // Store the dB value
        referenceActionGroup_->addAction(action);
        
        // Check if this is the current reference value
        // Use a small epsilon for floating point comparison
        if (qAbs(static_cast<double>(value) - currentRef) < 0.5) {
            action->setChecked(true);
        }
    }
    
    // If nothing is checked, default to -14 dB (system output default)
    if (!referenceActionGroup_->checkedAction()) {
        for (QAction* action : referenceActionGroup_->actions()) {
            if (action->data().toInt() == -14) {
                action->setChecked(true);
                break;
            }
        }
    }
}

void MainWindow::onDeviceSelected(QAction* action) {
    QString deviceUID = action->data().toString();
    
    if (deviceUID.isEmpty()) {
        return;
    }
    
    // Don't switch if it's already the current device
    if (deviceUID == audio_.currentDeviceUID()) {
        return;
    }
    
    QString err;
    if (!audio_.switchDevice(deviceUID, &err)) {
        QMessageBox::warning(
            this, tr("Device Switch Failed"),
            tr("Failed to switch to device: %1\n\nError: %2").arg(action->text()).arg(err));
        
        // Refresh menu to restore correct selection
        refreshDeviceMenu();
    }
}

void MainWindow::onReferenceSelected(QAction* action) {
    int referenceDb = action->data().toInt();
    audio_.setReferenceDbfs(static_cast<double>(referenceDb));
}

void MainWindow::refreshDeviceMenu() {
    populateDeviceMenu();
}
