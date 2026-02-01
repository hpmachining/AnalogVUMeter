#pragma once

#include "VUMeterScale.h"
#include "VUMeterSkin.h"

#include <QList>
#include <QString>
#include <QStringList>

class SkinManager {
  public:
    struct SkinInfo {
        QString id;
        QString name;
        bool isStereo = false;
        QString skinDir;
    };

    struct LoadedSkin {
        bool ok = false;
        QString error;
        QStringList warnings;

        VUSkinPackage package;
        VUMeterScaleTable singleScale;
        VUMeterScaleTable leftScale;
        VUMeterScaleTable rightScale;
    };

    SkinManager();

    void scan();
    QList<SkinInfo> availableSkins() const { return skins_; }

    void setActiveSkinId(const QString& skinId) { activeSkinId_ = skinId; }
    QString activeSkinId() const { return activeSkinId_; }
    void clearActiveSkin() { activeSkinId_.clear(); }
    void reset() {
        skins_.clear();
        activeSkinId_.clear();
    }

    LoadedSkin loadSkin(const QString& skinId) const;

    static QString skinsRootPath();

  private:
    QList<SkinInfo> skins_;
    QString activeSkinId_;
};
