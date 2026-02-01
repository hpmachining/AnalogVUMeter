#pragma once

#include <QString>
#include <QStringList>

class SkinImporter {
  public:
    struct ImportResult {
        bool ok = false;
        QString skinName;
        QString skinDir;
        QString error;
        QStringList warnings;
    };

    ImportResult importAimpZip(const QString& zipFilePath) const;
};
