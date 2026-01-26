#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "AudioCapture.h"
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("AnalogVUMeterQt");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Analog stereo VU meter (Qt 6 + PulseAudio)");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption listDevicesOpt(QStringList() << "list-devices", "List PulseAudio devices and exit.");
    QCommandLineOption deviceOpt(QStringList() << "device", "PulseAudio device index (legacy, unused).", "index");
    QCommandLineOption deviceNameOpt(QStringList() << "device-name", "PulseAudio device name (sink/source).", "name");
    QCommandLineOption deviceTypeOpt(QStringList() << "device-type",
                                     "Device type: 0=sink monitor (output), 1=source (mic).", "type", "0");
    QCommandLineOption refOpt(QStringList() << "ref-dbfs", "Reference dBFS for 0 VU.", "db", "-18");

    parser.addOption(listDevicesOpt);
    parser.addOption(deviceOpt);
    parser.addOption(deviceNameOpt);
    parser.addOption(deviceTypeOpt);
    parser.addOption(refOpt);

    parser.process(app);

    if (parser.isSet(listDevicesOpt)) {
        QTextStream(stdout) << AudioCapture::listDevicesString();
        return 0;
    }

    AudioCapture::Options options;

    // Legacy device index (unused in PulseAudio but kept for compatibility)
    if (parser.isSet(deviceOpt)) {
        bool ok = false;
        const int idx = parser.value(deviceOpt).toInt(&ok);
        if (ok) {
            options.deviceIndex = idx;
        }
    }

    // New PulseAudio-specific options
    if (parser.isSet(deviceNameOpt)) {
        options.deviceName = parser.value(deviceNameOpt);
    }

    if (parser.isSet(deviceTypeOpt)) {
        bool ok = false;
        const int type = parser.value(deviceTypeOpt).toInt(&ok);
        if (ok) {
            options.deviceType = type;
        }
    }

    if (parser.isSet(refOpt)) {
        bool ok = false;
        const double ref = parser.value(refOpt).toDouble(&ok);
        if (ok) {
            options.referenceDbfs = ref;
            options.referenceDbfsOverride = true;
        }
    }

    MainWindow w(options);
    w.show();

    return app.exec();
}
