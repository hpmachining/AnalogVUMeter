#include "SkinImporter.h"

#include "VUMeterScale.h"
#include "VUMeterSkin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#if defined(ANALOGVU_HAS_LIBZIP) && (ANALOGVU_HAS_LIBZIP == 1)
#include <zip.h>
#endif

namespace {

QString sanitizedDirName(const QString& in) {
    QString out;
    out.reserve(in.size());
    for (const QChar ch : in) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    out = out.trimmed();
    if (out.isEmpty())
        out = QStringLiteral("Skin");
    return out;
}

QDir findAimpRootDir(const QDir& extractedRoot) {
    const QFileInfoList entries = extractedRoot.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    if (entries.size() == 1 && entries[0].isDir()) {
        return QDir(entries[0].absoluteFilePath());
    }
    return extractedRoot;
}

QMap<QString, QString> lowerNameToActualFile(const QDir& dir) {
    QMap<QString, QString> map;
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& fi : files) {
        map.insert(fi.fileName().toLower(), fi.fileName());
    }
    return map;
}

#if defined(ANALOGVU_HAS_LIBZIP) && (ANALOGVU_HAS_LIBZIP == 1)
bool isSafeZipEntryPath(const QString& entryName) {
    if (entryName.isEmpty())
        return false;
    if (entryName.startsWith('/') || entryName.startsWith('\\'))
        return false;

    const QString cleaned = QDir::cleanPath(entryName);
    if (cleaned == QStringLiteral(".") || cleaned.isEmpty())
        return false;
    if (cleaned.startsWith(QStringLiteral("../")) || cleaned == QStringLiteral(".."))
        return false;
    if (cleaned.contains(QStringLiteral("/../")))
        return false;

    // Avoid Windows drive paths like C:\...
    if (cleaned.size() >= 2 && cleaned[1] == ':')
        return false;

    return true;
}

class ZipReader {
  public:
    static bool extractAll(const QString& zipFilePath, const QDir& destDir, QString* errorOut) {
        if (!destDir.exists()) {
            if (!QDir().mkpath(destDir.absolutePath())) {
                if (errorOut)
                    *errorOut = QStringLiteral("Failed to create destination directory: %1").arg(destDir.absolutePath());
                return false;
            }
        }

        int err = 0;
        zip_t* za = zip_open(zipFilePath.toUtf8().constData(), ZIP_RDONLY, &err);
        if (!za) {
            if (errorOut) {
                zip_error_t ze;
                zip_error_init_with_code(&ze, err);
                const QString msg = QString::fromUtf8(zip_error_strerror(&ze));
                zip_error_fini(&ze);
                *errorOut = QStringLiteral("Failed to open ZIP: %1 (%2)").arg(zipFilePath, msg);
            }
            return false;
        }

        const zip_int64_t n = zip_get_num_entries(za, 0);
        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(n); ++i) {
            zip_stat_t st;
            if (zip_stat_index(za, i, 0, &st) != 0)
                continue;

            const QString entryName = QString::fromUtf8(st.name ? st.name : "");
            if (entryName.isEmpty())
                continue;

            if (!isSafeZipEntryPath(entryName)) {
                if (errorOut)
                    *errorOut = QStringLiteral("Unsafe ZIP entry path: %1").arg(entryName);
                zip_close(za);
                return false;
            }

            const QString outPath = destDir.filePath(entryName);
            if (entryName.endsWith('/')) {
                if (!QDir().mkpath(outPath)) {
                    if (errorOut)
                        *errorOut = QStringLiteral("Failed to create directory: %1").arg(outPath);
                    zip_close(za);
                    return false;
                }
                continue;
            }

            const QFileInfo outFi(outPath);
            QDir parentDir = outFi.dir();
            if (!parentDir.exists()) {
                if (!QDir().mkpath(parentDir.absolutePath())) {
                    if (errorOut)
                        *errorOut = QStringLiteral("Failed to create directory: %1").arg(parentDir.absolutePath());
                    zip_close(za);
                    return false;
                }
            }

            zip_file_t* zf = zip_fopen_index(za, i, 0);
            if (!zf) {
                if (errorOut)
                    *errorOut = QStringLiteral("Failed to open ZIP entry: %1").arg(entryName);
                zip_close(za);
                return false;
            }

            QFile out(outPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                zip_fclose(zf);
                if (errorOut)
                    *errorOut = QStringLiteral("Failed to write file: %1").arg(outPath);
                zip_close(za);
                return false;
            }

            char buf[64 * 1024];
            while (true) {
                const zip_int64_t bytes = zip_fread(zf, buf, sizeof(buf));
                if (bytes < 0) {
                    zip_fclose(zf);
                    if (errorOut)
                        *errorOut = QStringLiteral("Failed reading ZIP entry: %1").arg(entryName);
                    zip_close(za);
                    return false;
                }
                if (bytes == 0)
                    break;

                if (out.write(buf, bytes) != bytes) {
                    zip_fclose(zf);
                    if (errorOut)
                        *errorOut = QStringLiteral("Short write: %1").arg(outPath);
                    zip_close(za);
                    return false;
                }
            }

            zip_fclose(zf);
        }

        zip_close(za);
        return true;
    }
};
#endif

bool tryCopyFile(const QString& srcPath, const QString& dstPath, QString* errorOut) {
    if (QFileInfo(dstPath).exists()) {
        QFile::remove(dstPath);
    }
    if (!QFile::copy(srcPath, dstPath)) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to copy %1 -> %2").arg(srcPath, dstPath);
        return false;
    }
    return true;
}

int readIntAny(const QSettings& ini, const QStringList& keys, int fallback, QStringList* warnings) {
    for (const QString& k : keys) {
        const QVariant v = ini.value(k);
        if (!v.isValid())
            continue;
        bool ok = false;
        const int out = v.toInt(&ok);
        if (ok)
            return out;
        if (warnings)
            warnings->push_back(QStringLiteral("Invalid int for %1").arg(k));
    }
    return fallback;
}

qreal readRealAny(const QSettings& ini, const QStringList& keys, qreal fallback, QStringList* warnings) {
    for (const QString& k : keys) {
        const QVariant v = ini.value(k);
        if (!v.isValid())
            continue;
        bool ok = false;
        const qreal out = v.toDouble(&ok);
        if (ok)
            return out;
        if (warnings)
            warnings->push_back(QStringLiteral("Invalid real for %1").arg(k));
    }
    return fallback;
}

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

VUMeterCalibration parseCalibrationGroup(QSettings& ini, const QString& groupName, QStringList* warnings) {
    VUMeterCalibration c = defaultCalibration();

    ini.beginGroup(groupName);
    c.minAngle = readIntAny(ini, {"MinAngle"}, c.minAngle, warnings);
    c.minLevel = readIntAny(ini, {"MinLevel"}, c.minLevel, warnings);
    c.zeroAngle = readIntAny(ini, {"ZeroAngle"}, c.zeroAngle, warnings);
    c.zeroLevel = readIntAny(ini, {"ZeroLevel"}, c.zeroLevel, warnings);
    c.maxAngle = readIntAny(ini, {"MaxAngle"}, c.maxAngle, warnings);
    c.maxLevel = readIntAny(ini, {"MaxLevel"}, c.maxLevel, warnings);
    c.pivotX = readIntAny(ini, {"PivotPointX"}, c.pivotX, warnings);
    c.pivotY = readIntAny(ini, {"PivotPointY"}, c.pivotY, warnings);
    c.mobilityNeg = readRealAny(ini, {"MobilityNegative"}, c.mobilityNeg, warnings);
    c.mobilityPos = readRealAny(ini, {"MobilityPositive"}, c.mobilityPos, warnings);
    ini.endGroup();

    return c;
}

VUMeterScaleTable buildScaleTableFromIni(QSettings& ini, const QString& groupName, const VUMeterCalibration& calib) {
    (void)ini;
    (void)groupName;
    return {{static_cast<float>(calib.minLevel), static_cast<float>(calib.minAngle)},
            {static_cast<float>(calib.zeroLevel), static_cast<float>(calib.zeroAngle)},
            {static_cast<float>(calib.maxLevel), static_cast<float>(calib.maxAngle)}};
}

QJsonObject calibrationToJson(const VUMeterCalibration& c) {
    QJsonObject o;
    o.insert(QStringLiteral("minAngle"), c.minAngle);
    o.insert(QStringLiteral("minLevel"), c.minLevel);
    o.insert(QStringLiteral("zeroAngle"), c.zeroAngle);
    o.insert(QStringLiteral("zeroLevel"), c.zeroLevel);
    o.insert(QStringLiteral("maxAngle"), c.maxAngle);
    o.insert(QStringLiteral("maxLevel"), c.maxLevel);
    o.insert(QStringLiteral("pivotX"), c.pivotX);
    o.insert(QStringLiteral("pivotY"), c.pivotY);
    o.insert(QStringLiteral("mobilityNeg"), c.mobilityNeg);
    o.insert(QStringLiteral("mobilityPos"), c.mobilityPos);
    return o;
}

QJsonArray scaleTableToJson(const VUMeterScaleTable& t) {
    QJsonArray a;
    for (const auto& p : t) {
        QJsonObject o;
        o.insert(QStringLiteral("level"), p.first);
        o.insert(QStringLiteral("angle"), p.second);
        a.push_back(o);
    }
    return a;
}

QJsonObject meterJson(const QString& face, const QString& needle, const QString& cap,
                      const VUMeterCalibration& calib, const VUMeterScaleTable& table) {
    QJsonObject assets;
    assets.insert(QStringLiteral("face"), face);
    assets.insert(QStringLiteral("needle"), needle);
    assets.insert(QStringLiteral("cap"), cap);

    QJsonObject o;
    o.insert(QStringLiteral("assets"), assets);
    o.insert(QStringLiteral("calibration"), calibrationToJson(calib));
    o.insert(QStringLiteral("scaleTable"), scaleTableToJson(table));
    return o;
}

} // namespace

SkinImporter::ImportResult SkinImporter::importAimpZip(const QString& zipFilePath) const {
    ImportResult result;

    const QFileInfo zipInfo(zipFilePath);
    if (!zipInfo.exists() || !zipInfo.isFile()) {
        result.error = QStringLiteral("ZIP not found: %1").arg(zipFilePath);
        return result;
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        result.error = QStringLiteral("Failed to create temp directory");
        return result;
    }

#if !defined(ANALOGVU_HAS_LIBZIP) || (ANALOGVU_HAS_LIBZIP == 0)
    result.error = QStringLiteral("Skin import is disabled because libzip was not found at build time.");
    return result;
#else
    QString zipError;
    if (!ZipReader::extractAll(zipFilePath, QDir(tmp.path()), &zipError)) {
        result.error = zipError;
        return result;
    }
#endif

    const QDir extractedRoot(tmp.path());
    const QDir aimpRoot = findAimpRootDir(extractedRoot);
    const QMap<QString, QString> fileMap = lowerNameToActualFile(aimpRoot);

    auto requireFile = [&](const QString& nameLower, QString* actualOut) -> bool {
        const auto it = fileMap.constFind(nameLower);
        if (it == fileMap.constEnd())
            return false;
        if (actualOut)
            *actualOut = it.value();
        return true;
    };

    QString iniActual;
    if (!requireFile(QStringLiteral("skin.ini"), &iniActual)) {
        result.error = QStringLiteral("skin.ini not found in ZIP");
        return result;
    }

    bool isStereo = false;
    QString s0, s1, s2;
    QString l0, l1, l2;
    QString r0, r1, r2;

    const bool hasSingle = requireFile(QStringLiteral("0.png"), &s0) &&
                           requireFile(QStringLiteral("1.png"), &s1) &&
                           requireFile(QStringLiteral("2.png"), &s2);

    const bool hasStereo = requireFile(QStringLiteral("l_0.png"), &l0) &&
                           requireFile(QStringLiteral("l_1.png"), &l1) &&
                           requireFile(QStringLiteral("l_2.png"), &l2) &&
                           requireFile(QStringLiteral("r_0.png"), &r0) &&
                           requireFile(QStringLiteral("r_1.png"), &r1) &&
                           requireFile(QStringLiteral("r_2.png"), &r2);

    if (hasStereo) {
        isStereo = true;
    } else if (hasSingle) {
        isStereo = false;
    } else {
        result.error = QStringLiteral("ZIP does not contain expected AIMP assets (0/1/2.png or L_*/R_* set)");
        return result;
    }

    const QString iniPath = aimpRoot.filePath(iniActual);
    QSettings ini(iniPath, QSettings::IniFormat);

    VUMeterCalibration singleCalib;
    VUMeterCalibration leftCalib;
    VUMeterCalibration rightCalib;

    if (isStereo) {
        leftCalib = parseCalibrationGroup(ini, QStringLiteral("VU_L"), &result.warnings);
        rightCalib = parseCalibrationGroup(ini, QStringLiteral("VU_R"), &result.warnings);
        singleCalib = leftCalib;
    } else {
        singleCalib = parseCalibrationGroup(ini, QStringLiteral("VU"), &result.warnings);
        leftCalib = singleCalib;
        rightCalib = singleCalib;
    }

    VUMeterScaleTable singleTable;
    VUMeterScaleTable leftTable;
    VUMeterScaleTable rightTable;

    if (isStereo) {
        leftTable = buildScaleTableFromIni(ini, QStringLiteral("VU_L"), leftCalib);
        rightTable = buildScaleTableFromIni(ini, QStringLiteral("VU_R"), rightCalib);
        singleTable = leftTable;
    } else {
        singleTable = buildScaleTableFromIni(ini, QStringLiteral("VU"), singleCalib);
        leftTable = singleTable;
        rightTable = singleTable;
    }

    const QString baseName = sanitizedDirName(zipInfo.completeBaseName());
    const QString skinsRootPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/skins");

    QDir skinsRoot(skinsRootPath);
    if (!skinsRoot.exists()) {
        if (!QDir().mkpath(skinsRoot.absolutePath())) {
            result.error = QStringLiteral("Failed to create skins directory: %1").arg(skinsRoot.absolutePath());
            return result;
        }
    }

    QString finalDirName = baseName;
    for (int i = 2; skinsRoot.exists(finalDirName); ++i) {
        finalDirName = QStringLiteral("%1-%2").arg(baseName).arg(i);
    }

    const QString skinDirPath = skinsRoot.filePath(finalDirName);
    QDir skinDir(skinDirPath);
    if (!QDir().mkpath(skinDirPath)) {
        result.error = QStringLiteral("Failed to create skin directory: %1").arg(skinDirPath);
        return result;
    }

    const QString singleDir = skinDir.filePath(QStringLiteral("single"));
    const QString stereoLeftDir = skinDir.filePath(QStringLiteral("stereo/left"));
    const QString stereoRightDir = skinDir.filePath(QStringLiteral("stereo/right"));

    if (!QDir().mkpath(singleDir) || !QDir().mkpath(stereoLeftDir) || !QDir().mkpath(stereoRightDir)) {
        result.error = QStringLiteral("Failed to create skin subdirectories");
        return result;
    }

    QString copyError;

    auto copySingleFrom = [&](const QString& faceName, const QString& needleName, const QString& capName) -> bool {
        if (!tryCopyFile(aimpRoot.filePath(faceName), QDir(singleDir).filePath(QStringLiteral("face.png")), &copyError))
            return false;
        if (!tryCopyFile(aimpRoot.filePath(needleName), QDir(singleDir).filePath(QStringLiteral("needle.png")), &copyError))
            return false;
        if (!tryCopyFile(aimpRoot.filePath(capName), QDir(singleDir).filePath(QStringLiteral("cap.png")), &copyError))
            return false;
        return true;
    };

    auto copyStereoSide = [&](const QString& faceName, const QString& needleName, const QString& capName, const QString& sideDir) -> bool {
        if (!tryCopyFile(aimpRoot.filePath(faceName), QDir(sideDir).filePath(QStringLiteral("face.png")), &copyError))
            return false;
        if (!tryCopyFile(aimpRoot.filePath(needleName), QDir(sideDir).filePath(QStringLiteral("needle.png")), &copyError))
            return false;
        if (!tryCopyFile(aimpRoot.filePath(capName), QDir(sideDir).filePath(QStringLiteral("cap.png")), &copyError))
            return false;
        return true;
    };

    if (isStereo) {
        if (!copyStereoSide(l0, l1, l2, stereoLeftDir) || !copyStereoSide(r0, r1, r2, stereoRightDir)) {
            result.error = copyError;
            return result;
        }
        if (!copySingleFrom(l0, l1, l2)) {
            result.error = copyError;
            return result;
        }
    } else {
        if (!copySingleFrom(s0, s1, s2)) {
            result.error = copyError;
            return result;
        }
        if (!copyStereoSide(s0, s1, s2, stereoLeftDir) || !copyStereoSide(s0, s1, s2, stereoRightDir)) {
            result.error = copyError;
            return result;
        }
    }

    QJsonObject meters;

    meters.insert(QStringLiteral("single"),
                  meterJson(QStringLiteral("single/face.png"), QStringLiteral("single/needle.png"), QStringLiteral("single/cap.png"),
                           singleCalib, singleTable));

    QJsonObject stereo;
    stereo.insert(QStringLiteral("left"),
                  meterJson(QStringLiteral("stereo/left/face.png"), QStringLiteral("stereo/left/needle.png"), QStringLiteral("stereo/left/cap.png"),
                           leftCalib, leftTable));
    stereo.insert(QStringLiteral("right"),
                  meterJson(QStringLiteral("stereo/right/face.png"), QStringLiteral("stereo/right/needle.png"), QStringLiteral("stereo/right/cap.png"),
                           rightCalib, rightTable));
    meters.insert(QStringLiteral("stereo"), stereo);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("name"), finalDirName);
    root.insert(QStringLiteral("type"), isStereo ? QStringLiteral("stereo") : QStringLiteral("single"));
    root.insert(QStringLiteral("meters"), meters);
    root.insert(QStringLiteral("importedFrom"), zipInfo.fileName());

    const QString jsonPath = skinDir.filePath(QStringLiteral("skin.json"));
    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("Failed to write skin.json");
        return result;
    }

    const QJsonDocument doc(root);
    jsonFile.write(doc.toJson(QJsonDocument::Indented));
    jsonFile.close();

    result.ok = true;
    result.skinName = finalDirName;
    result.skinDir = skinDirPath;
    return result;
}
