#pragma once

#include <QPair>
#include <QVector>

// Data-driven scale mapping: VU (dB) -> needle angle (degrees).
//
// The default table values represent the built-in calibration shipped with the app.
// TODO: Future work: load calibration tables from external files (skin packages).

using VUMeterScaleTable = QVector<QPair<float, float>>;

VUMeterScaleTable builtInDefaultScaleTable();

float vuToAngleDeg(float vuDb, const VUMeterScaleTable& table);
