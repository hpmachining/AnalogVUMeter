#pragma once

#include <QtGlobal>
#include <QPixmap>

// Skin data types are intentionally lightweight and data-focused.
// The widget owns the built-in default skin and is responsible for drawing.
// TODO: Future work: load skin packages from disk/resources.

struct VUMeterCalibration {
    int minAngle;
    int minLevel;
    int zeroAngle;
    int zeroLevel;
    int maxAngle;
    int maxLevel;

    int pivotX;
    int pivotY;

    qreal mobilityNeg;
    qreal mobilityPos;
};

struct VUMeterSkin {
    QPixmap face;
    QPixmap needle;
    QPixmap cap;

    VUMeterCalibration calib;
};

struct VUSkinPackage {
    bool isStereo = false; // false = single meter, true = double meter

    // Single meter
    VUMeterSkin single;

    // Stereo meters
    VUMeterSkin left;
    VUMeterSkin right;
};
