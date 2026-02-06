#include "SkinManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace {

bool loadPixmap(QPixmap* out, const QString& absPath, QStringList* warnings) {
    if (!out)
        return false;
    if (!QFileInfo::exists(absPath)) {
        if (warnings)
            warnings->push_back(QStringLiteral("Missing asset: %1").arg(absPath));
        return false;
    }
    if (!out->load(absPath)) {
        if (warnings)
            warnings->push_back(QStringLiteral("Failed to load image: %1").arg(absPath));
        return false;
    }
    return true;
}

bool isTopLevelFileName(const QString& rel) {
    return !rel.contains('/') && !rel.contains('\\') && !rel.contains(QStringLiteral(".."));
}

bool requireString(const QJsonObject& o, const QString& key, QString* out, QString* errorOut) {
    const QJsonValue v = o.value(key);
    if (!v.isString()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid string field: %1").arg(key);
        return false;
    }
    if (out)
        *out = v.toString();
    return true;
}

bool requireInt(const QJsonObject& o, const QString& key, int* out, QString* errorOut) {
    const QJsonValue v = o.value(key);
    if (!v.isDouble()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid int field: %1").arg(key);
        return false;
    }
    if (out)
        *out = v.toInt();
    return true;
}

bool requireReal(const QJsonObject& o, const QString& key, qreal* out, QString* errorOut) {
    const QJsonValue v = o.value(key);
    if (!v.isDouble()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid real field: %1").arg(key);
        return false;
    }
    if (out)
        *out = v.toDouble();
    return true;
}

bool parseCalibrationStrict(const QJsonObject& o, VUMeterCalibration* out, QString* errorOut) {
    if (!out)
        return false;

    VUMeterCalibration c;
    if (!requireInt(o, QStringLiteral("minAngle"), &c.minAngle, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("minLevel"), &c.minLevel, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("zeroAngle"), &c.zeroAngle, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("zeroLevel"), &c.zeroLevel, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("maxAngle"), &c.maxAngle, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("maxLevel"), &c.maxLevel, errorOut))
        return false;

    if (!requireInt(o, QStringLiteral("pivotX"), &c.pivotX, errorOut))
        return false;
    if (!requireInt(o, QStringLiteral("pivotY"), &c.pivotY, errorOut))
        return false;

    if (!requireReal(o, QStringLiteral("mobilityNegative"), &c.mobilityNegative, errorOut))
        return false;
    if (!requireReal(o, QStringLiteral("mobilityPositive"), &c.mobilityPositive, errorOut))
        return false;

    *out = c;
    return true;
}

bool parseScaleTableStrict(const QJsonValue& v, VUMeterScaleTable* out, QString* errorOut) {
    if (!out)
        return false;
    if (!v.isArray()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid scaleTable (must be an array)");
        return false;
    }

    const QJsonArray a = v.toArray();
    if (a.size() < 3) {
        if (errorOut)
            *errorOut = QStringLiteral("scaleTable must contain at least 3 entries (min/zero/max)");
        return false;
    }

    VUMeterScaleTable t;
    t.reserve(a.size());
    for (const QJsonValue& e : a) {
        if (!e.isObject()) {
            if (errorOut)
                *errorOut = QStringLiteral("scaleTable contains a non-object entry");
            return false;
        }
        const QJsonObject o = e.toObject();
        const QJsonValue angleV = o.value(QStringLiteral("angle"));
        const QJsonValue levelV = o.value(QStringLiteral("level"));
        if (!angleV.isDouble() || !levelV.isDouble()) {
            if (errorOut)
                *errorOut = QStringLiteral("scaleTable entries must contain numeric angle and level");
            return false;
        }
        t.push_back({static_cast<float>(levelV.toDouble()), static_cast<float>(angleV.toDouble())});
    }

    std::sort(t.begin(), t.end(), [](const auto& a0, const auto& a1) { return a0.first < a1.first; });
    *out = t;
    return true;
}

bool parseMeterStrict(const QJsonObject& meterObj,
                      const QDir& skinDir,
                      const QString& expectedFace,
                      const QString& expectedNeedle,
                      const QString& expectedCap,
                      VUMeterSkin* out,
                      QString* errorOut,
                      QStringList* warnings) {
    if (!out)
        return false;

    const QJsonValue assetsV = meterObj.value(QStringLiteral("assets"));
    const QJsonValue calibV = meterObj.value(QStringLiteral("calibration"));
    const QJsonValue scaleV = meterObj.value(QStringLiteral("scaleTable"));

    if (!assetsV.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid assets object");
        return false;
    }
    if (!calibV.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing or invalid calibration object");
        return false;
    }

    QString faceRel, needleRel, capRel;
    const QJsonObject assets = assetsV.toObject();
    if (!requireString(assets, QStringLiteral("face"), &faceRel, errorOut))
        return false;
    if (!requireString(assets, QStringLiteral("needle"), &needleRel, errorOut))
        return false;
    if (!requireString(assets, QStringLiteral("cap"), &capRel, errorOut))
        return false;

    if (!isTopLevelFileName(faceRel) || !isTopLevelFileName(needleRel) || !isTopLevelFileName(capRel)) {
        if (errorOut)
            *errorOut = QStringLiteral("Asset filenames must be top-level files (no subdirectories)");
        return false;
    }

    if (faceRel != expectedFace || needleRel != expectedNeedle || capRel != expectedCap) {
        if (errorOut)
            *errorOut = QStringLiteral("Unexpected asset filenames; expected %1, %2, %3")
                            .arg(expectedFace, expectedNeedle, expectedCap);
        return false;
    }

    VUMeterCalibration calib;
    if (!parseCalibrationStrict(calibV.toObject(), &calib, errorOut))
        return false;

    VUMeterScaleTable scale;
    if (!parseScaleTableStrict(scaleV, &scale, errorOut))
        return false;

    const QString faceAbs = skinDir.filePath(faceRel);
    const QString needleAbs = skinDir.filePath(needleRel);
    const QString capAbs = skinDir.filePath(capRel);

    if (!loadPixmap(&out->assets.face, faceAbs, warnings) || !loadPixmap(&out->assets.needle, needleAbs, warnings) ||
        !loadPixmap(&out->assets.cap, capAbs, warnings)) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to load one or more required skin assets");
        return false;
    }

    out->calibration = calib;
    out->scaleTable = scale;
    return true;
}

} // namespace

SkinManager::SkinManager() = default;

QString SkinManager::skinsRootPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/skins");
}

void SkinManager::scan() {
    skins_.clear();

    const QDir root(skinsRootPath());
    if (!root.exists())
        return;

    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo& di : dirs) {
        const QDir skinDir(di.absoluteFilePath());
        const QString jsonPath = skinDir.filePath(QStringLiteral("skin.json"));
        if (!QFileInfo::exists(jsonPath))
            continue;

        QFile f(jsonPath);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        f.close();

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            continue;

        const QJsonObject rootObj = doc.object();

        const int schemaVersion = rootObj.value(QStringLiteral("schemaVersion")).toInt(0);
        if (schemaVersion != 2)
            continue;

        const QString name = rootObj.value(QStringLiteral("name")).toString();
        const QString type = rootObj.value(QStringLiteral("type")).toString();
        if (name.isEmpty())
            continue;
        if (type != QStringLiteral("single") && type != QStringLiteral("stereo"))
            continue;

        SkinInfo info;
        info.id = di.fileName();
        info.name = name;
        info.isStereo = (type == QStringLiteral("stereo"));
        info.skinDir = di.absoluteFilePath();
        skins_.push_back(info);
    }
}

SkinManager::LoadedSkin SkinManager::loadSkin(const QString& skinId) const {
    LoadedSkin out;

    const auto it = std::find_if(skins_.begin(), skins_.end(), [&](const SkinInfo& i) { return i.id == skinId; });
    if (it == skins_.end()) {
        out.error = QStringLiteral("Unknown skin id: %1").arg(skinId);
        return out;
    }

    const QDir skinDir(it->skinDir);
    const QString jsonPath = skinDir.filePath(QStringLiteral("skin.json"));

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("Failed to open skin.json");
        return out;
    }
    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        out.error = QStringLiteral("skin.json is not a JSON object");
        return out;
    }

    const QJsonObject rootObj = doc.object();

    // schemaVersion 2 is a clean break: no legacy schema support is provided.
    const int schemaVersion = rootObj.value(QStringLiteral("schemaVersion")).toInt(0);
    if (schemaVersion != 2) {
        out.error = QStringLiteral("Unsupported schemaVersion (expected 2)");
        return out;
    }

    const QString type = rootObj.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("single") && type != QStringLiteral("stereo")) {
        out.error = QStringLiteral("Invalid skin type (expected 'single' or 'stereo')");
        return out;
    }

    const QString name = rootObj.value(QStringLiteral("name")).toString();
    const QString importedFrom = rootObj.value(QStringLiteral("importedFrom")).toString();
    if (name.isEmpty()) {
        out.error = QStringLiteral("Missing or invalid name");
        return out;
    }
    if (importedFrom.isEmpty()) {
        out.error = QStringLiteral("Missing or invalid importedFrom");
        return out;
    }

    const QJsonValue metersV = rootObj.value(QStringLiteral("meters"));
    if (!metersV.isObject()) {
        out.error = QStringLiteral("Missing or invalid meters object");
        return out;
    }
    const QJsonObject metersObj = metersV.toObject();

    VUSkinPackage pkg;
    pkg.name = name;
    pkg.importedFrom = importedFrom;

    QString parseError;

    if (type == QStringLiteral("single")) {
        if (metersObj.size() != 1 || !metersObj.contains(QStringLiteral("vu"))) {
            out.error = QStringLiteral("Single skin must contain exactly one meter entry: meters.vu");
            return out;
        }
        const QJsonValue vuV = metersObj.value(QStringLiteral("vu"));
        if (!vuV.isObject()) {
            out.error = QStringLiteral("Single skin must contain meters.vu");
            return out;
        }
        VUMeterSkin vu;
        if (!parseMeterStrict(vuV.toObject(),
                              skinDir,
                              QStringLiteral("face.png"),
                              QStringLiteral("needle.png"),
                              QStringLiteral("cap.png"),
                              &vu,
                              &parseError,
                              &out.warnings)) {
            out.error = parseError;
            return out;
        }
        pkg.meters = VUSkinSingleMeters{vu};
    } else {
        if (metersObj.size() != 2 || !metersObj.contains(QStringLiteral("left")) || !metersObj.contains(QStringLiteral("right"))) {
            out.error = QStringLiteral("Stereo skin must contain exactly two meter entries: meters.left and meters.right");
            return out;
        }
        const QJsonValue leftV = metersObj.value(QStringLiteral("left"));
        const QJsonValue rightV = metersObj.value(QStringLiteral("right"));
        if (!leftV.isObject() || !rightV.isObject()) {
            out.error = QStringLiteral("Stereo skin must contain meters.left and meters.right");
            return out;
        }

        VUMeterSkin left;
        VUMeterSkin right;
        if (!parseMeterStrict(leftV.toObject(),
                              skinDir,
                              QStringLiteral("L_face.png"),
                              QStringLiteral("L_needle.png"),
                              QStringLiteral("L_cap.png"),
                              &left,
                              &parseError,
                              &out.warnings)) {
            out.error = parseError;
            return out;
        }
        if (!parseMeterStrict(rightV.toObject(),
                              skinDir,
                              QStringLiteral("R_face.png"),
                              QStringLiteral("R_needle.png"),
                              QStringLiteral("R_cap.png"),
                              &right,
                              &parseError,
                              &out.warnings)) {
            out.error = parseError;
            return out;
        }

        pkg.meters = VUSkinStereoMeters{left, right};
    }

    out.package = pkg;

    out.ok = true;
    return out;
}
