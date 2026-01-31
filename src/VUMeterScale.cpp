#include "VUMeterScale.h"

VUMeterScaleTable builtInDefaultScaleTable() {
    return {{-20, -47},
            {-10, -34},
            {-7, -25},
            {-6, -21},
            {-5, -16},
            {-4, -11},
            {-3, -5},
            {-2, 2},
            {-1, 9},
            {0, 18},
            {1, 27},
            {2, 38},
            {3, 47}};
}

float vuToAngleDeg(float vuDb, const VUMeterScaleTable& table) {
    if (table.isEmpty()) {
        return 0.0f;
    }

    // Clamp to table range
    if (vuDb <= table.first().first) {
        return table.first().second;
    }
    if (vuDb >= table.last().first) {
        return table.last().second;
    }

    // Find segment
    for (int i = 0; i < table.size() - 1; ++i) {
        const float v0 = table[i].first;
        const float a0 = table[i].second;
        const float v1 = table[i + 1].first;
        const float a1 = table[i + 1].second;

        if (vuDb >= v0 && vuDb <= v1) {
            const float t = (vuDb - v0) / (v1 - v0);
            return a0 + t * (a1 - a0);
        }
    }

    return table.last().second;
}
