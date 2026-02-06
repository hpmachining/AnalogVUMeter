#pragma once

#include <QPixmap>
#include <QtGlobal>

#include "VUMeterScale.h"

#include <QString>
#include <variant>

struct VUMeterCalibration {
    int minAngle;
    int minLevel;
    int zeroAngle;
    int zeroLevel;
    int maxAngle;
    int maxLevel;

    int pivotX;
    int pivotY;

    qreal mobilityNegative;
    qreal mobilityPositive;
};

struct VUMeterAssets {
    QPixmap face;
    QPixmap needle;
    QPixmap cap;
};

struct VUMeterSkin {
    VUMeterAssets assets;
    VUMeterCalibration calibration;
    VUMeterScaleTable scaleTable;
};

struct VUSkinSingleMeters {
    VUMeterSkin vu;
};

struct VUSkinStereoMeters {
    VUMeterSkin left;
    VUMeterSkin right;
};

struct VUSkinPackage {
    QString name;
    QString importedFrom;

    // schemaVersion=2 skin runtime model:
    // - single skins contain exactly one meter definition ("vu")
    // - stereo skins contain exactly two independent meter definitions ("left" / "right")
    // - scale tables are owned per meter; no mirroring or inferred duplication is permitted
    std::variant<VUSkinSingleMeters, VUSkinStereoMeters> meters;
};
