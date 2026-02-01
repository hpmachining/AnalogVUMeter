#include "SkinManager.h"

#include "VUMeterScale.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace {

VUMeterCalibration defaultCalibration() {
    VUMeterCalibration c;
    c.minAngle = -47;
    c.minLevel = -20;
    c.zeroAngle = 20;
    c.zeroLevel = 0;
    c.maxAngle = 47;
    c.maxLevel = 3;
    c.pivotX = 310;
    c.pivotY = 362;
    c.mobilityNeg = 0.05;
    c.mobilityPos = 0.10;
    return c;
}

int jsonInt(const QJsonObject& o, const QString& key, int fallback, QStringList* warnings) {
    const QJsonValue v = o.value(key);
    if (!v.isUndefined() && v.isDouble())
        return v.toInt();
    if (!v.isUndefined() && warnings)
        warnings->push_back(QStringLiteral("Invalid or missing int field: %1").arg(key));
    return fallback;
}

qreal jsonReal(const QJsonObject& o, const QString& key, qreal fallback, QStringList* warnings) {
    const QJsonValue v = o.value(key);
    if (!v.isUndefined() && v.isDouble())
        return v.toDouble();
    if (!v.isUndefined() && warnings)
        warnings->push_back(QStringLiteral("Invalid or missing real field: %1").arg(key));
    return fallback;
}

QString jsonString(const QJsonObject& o, const QString& key, const QString& fallback, QStringList* warnings) {
    const QJsonValue v = o.value(key);
    if (!v.isUndefined() && v.isString())
        return v.toString();
    if (!v.isUndefined() && warnings)
        warnings->push_back(QStringLiteral("Invalid or missing string field: %1").arg(key));
    return fallback;
}

VUMeterCalibration parseCalibration(const QJsonObject& o, QStringList* warnings) {
    VUMeterCalibration c = defaultCalibration();

    c.minAngle = jsonInt(o, QStringLiteral("minAngle"), c.minAngle, warnings);
    c.minLevel = jsonInt(o, QStringLiteral("minLevel"), c.minLevel, warnings);
    c.zeroAngle = jsonInt(o, QStringLiteral("zeroAngle"), c.zeroAngle, warnings);
    c.zeroLevel = jsonInt(o, QStringLiteral("zeroLevel"), c.zeroLevel, warnings);
    c.maxAngle = jsonInt(o, QStringLiteral("maxAngle"), c.maxAngle, warnings);
    c.maxLevel = jsonInt(o, QStringLiteral("maxLevel"), c.maxLevel, warnings);
    c.pivotX = jsonInt(o, QStringLiteral("pivotX"), c.pivotX, warnings);
    c.pivotY = jsonInt(o, QStringLiteral("pivotY"), c.pivotY, warnings);
    c.mobilityNeg = jsonReal(o, QStringLiteral("mobilityNeg"), c.mobilityNeg, warnings);
    c.mobilityPos = jsonReal(o, QStringLiteral("mobilityPos"), c.mobilityPos, warnings);

    return c;
}

VUMeterScaleTable scaleTableFromCalibration(const VUMeterCalibration& c) {
    return {{static_cast<float>(c.minLevel), static_cast<float>(c.minAngle)},
            {static_cast<float>(c.zeroLevel), static_cast<float>(c.zeroAngle)},
            {static_cast<float>(c.maxLevel), static_cast<float>(c.maxAngle)}};
}

VUMeterScaleTable parseScaleTable(const QJsonValue& v, const VUMeterCalibration& fallbackCalib, QStringList* warnings) {
    if (!v.isArray()) {
        if (!v.isUndefined() && warnings)
            warnings->push_back(QStringLiteral("scaleTable is not an array; falling back to calibration triple"));
        return scaleTableFromCalibration(fallbackCalib);
    }

    const QJsonArray a = v.toArray();
    VUMeterScaleTable t;
    t.reserve(a.size());

    for (const QJsonValue& e : a) {
        if (!e.isObject())
            continue;
        const QJsonObject o = e.toObject();

        const QJsonValue levelV = o.value(QStringLiteral("level"));
        const QJsonValue angleV = o.value(QStringLiteral("angle"));
        if (!levelV.isDouble() || !angleV.isDouble())
            continue;

        t.push_back({static_cast<float>(levelV.toDouble()), static_cast<float>(angleV.toDouble())});
    }

    if (t.isEmpty()) {
        if (warnings)
            warnings->push_back(QStringLiteral("scaleTable had no valid entries; falling back to calibration triple"));
        return scaleTableFromCalibration(fallbackCalib);
    }

    std::sort(t.begin(), t.end(), [](const auto& a0, const auto& a1) { return a0.first < a1.first; });
    return t;
}

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

bool parseMeter(const QJsonObject& meterObj, const QDir& skinDir, VUMeterSkin* outSkin, VUMeterScaleTable* outScale,
               QStringList* warnings) {
    if (!outSkin || !outScale)
        return false;

    const QJsonObject assets = meterObj.value(QStringLiteral("assets")).toObject();
    const QString faceRel = jsonString(assets, QStringLiteral("face"), QString(), warnings);
    const QString needleRel = jsonString(assets, QStringLiteral("needle"), QString(), warnings);
    const QString capRel = jsonString(assets, QStringLiteral("cap"), QString(), warnings);

    const QJsonObject calibObj = meterObj.value(QStringLiteral("calibration")).toObject();
    outSkin->calib = parseCalibration(calibObj, warnings);

    *outScale = parseScaleTable(meterObj.value(QStringLiteral("scaleTable")), outSkin->calib, warnings);

    const QString faceAbs = skinDir.filePath(faceRel);
    const QString needleAbs = skinDir.filePath(needleRel);
    const QString capAbs = skinDir.filePath(capRel);

    const bool okFace = loadPixmap(&outSkin->face, faceAbs, warnings);
    const bool okNeedle = loadPixmap(&outSkin->needle, needleAbs, warnings);
    const bool okCap = loadPixmap(&outSkin->cap, capAbs, warnings);

    return okFace && okNeedle && okCap;
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
        const QString name = rootObj.value(QStringLiteral("name")).toString(di.fileName());
        const QString type = rootObj.value(QStringLiteral("type")).toString(QStringLiteral("single"));

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
    const QString type = rootObj.value(QStringLiteral("type")).toString(QStringLiteral("single"));
    const bool isStereo = (type == QStringLiteral("stereo"));

    const QJsonObject meters = rootObj.value(QStringLiteral("meters")).toObject();
    const QJsonObject singleObj = meters.value(QStringLiteral("single")).toObject();
    const QJsonObject stereoObj = meters.value(QStringLiteral("stereo")).toObject();

    VUMeterSkin singleSkin;
    VUMeterSkin leftSkin;
    VUMeterSkin rightSkin;

    VUMeterScaleTable singleScale;
    VUMeterScaleTable leftScale;
    VUMeterScaleTable rightScale;

    bool ok = true;

    if (isStereo) {
        const QJsonObject leftObj = stereoObj.value(QStringLiteral("left")).toObject();
        const QJsonObject rightObj = stereoObj.value(QStringLiteral("right")).toObject();

        ok = parseMeter(leftObj, skinDir, &leftSkin, &leftScale, &out.warnings) &&
             parseMeter(rightObj, skinDir, &rightSkin, &rightScale, &out.warnings);

        if (!singleObj.isEmpty()) {
            parseMeter(singleObj, skinDir, &singleSkin, &singleScale, &out.warnings);
        } else {
            singleSkin = leftSkin;
            singleScale = leftScale;
        }
    } else {
        ok = parseMeter(singleObj, skinDir, &singleSkin, &singleScale, &out.warnings);
        leftSkin = singleSkin;
        rightSkin = singleSkin;
        leftScale = singleScale;
        rightScale = singleScale;
    }

    if (!ok) {
        out.error = QStringLiteral("Failed to load one or more required skin assets");
        return out;
    }

    out.package.isStereo = isStereo;
    out.package.single = singleSkin;
    out.package.left = leftSkin;
    out.package.right = rightSkin;

    out.singleScale = singleScale;
    out.leftScale = leftScale;
    out.rightScale = rightScale;

    out.ok = true;
    return out;
}
