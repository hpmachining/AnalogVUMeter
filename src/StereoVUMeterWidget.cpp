#include "StereoVUMeterWidget.h"

#include <algorithm>
#include <cmath>

#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <qnamespace.h>
#include <qpixmap.h>
#include <qtypes.h>

#include "VUMeterScale.h"

static constexpr float kPi = 3.14159265358979323846f;

static QPointF polarFromBottomPivot(const QPointF& pivot, float radius, float thetaDeg) {
    const float theta = thetaDeg * (kPi / 180.0f);
    const float sx = std::sin(theta);
    const float cy = std::cos(theta);
    return QPointF(pivot.x() + radius * sx, pivot.y() - radius * cy);
}

void StereoVUMeterWidget::setSkinPackage(const VUSkinPackage& skin) {
    skin_ = skin;
    update();
}

void StereoVUMeterWidget::clearSkin() {
    loadDefaultSkin();
    update();
}

StereoVUMeterWidget::StereoVUMeterWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    loadDefaultSkin();
    // Load the SONY logo font from resources
    int fontId = QFontDatabase::addApplicationFont(":/fonts/clarendon_regular.otf");
    if (fontId != -1) {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            sonyFontFamily_ = families.first();
        }
    }
}

void StereoVUMeterWidget::setStyle(VUMeterStyle style) {
    if (style_ != style) {
        style_ = style;
        update();
    }
}

StereoVUMeterWidget::StyleParams StereoVUMeterWidget::getStyleParams() const {
    StyleParams params;

    switch (style_) {
    case VUMeterStyle::Original:
    default:
        params.labelSizeFactor = 0.050;
        params.vuTextSizeFactor = 0.070;
        params.vuTextRadius = 1.29;
        params.singleVuText = false;
        params.faceColorTop = QColor(250, 246, 226);
        params.faceColorBottom = QColor(236, 230, 200);
        params.labelColor = QColor(0, 0, 0, 220);
        params.redZoneColor = QColor(170, 20, 20);
        break;

    case VUMeterStyle::Sony:
        params.labelSizeFactor = 0.065;
        params.vuTextSizeFactor = 0.095;
        params.vuTextRadius = 0.85;
        params.singleVuText = true;
        params.faceColorTop = QColor(235, 230, 200);
        params.faceColorBottom = QColor(220, 215, 185);
        params.labelColor = QColor(0, 0, 0, 230);
        params.redZoneColor = QColor(140, 20, 20);
        break;

    case VUMeterStyle::Vintage:
        params.labelSizeFactor = 0.070;
        params.vuTextSizeFactor = 0.080;
        params.vuTextRadius = 1.29;
        params.singleVuText = false;
        params.faceColorTop = QColor(255, 248, 220);
        params.faceColorBottom = QColor(240, 230, 195);
        params.labelColor = QColor(60, 40, 20, 230);
        params.redZoneColor = QColor(180, 50, 30);
        break;

    case VUMeterStyle::Modern:
        params.labelSizeFactor = 0.060;
        params.vuTextSizeFactor = 0.090;
        params.vuTextRadius = 0.85;
        params.singleVuText = true;
        params.faceColorTop = QColor(245, 245, 248);
        params.faceColorBottom = QColor(235, 235, 240);
        params.labelColor = QColor(40, 40, 45, 230);
        params.redZoneColor = QColor(220, 50, 50);
        break;

    case VUMeterStyle::Black:
        params.labelSizeFactor = 0.060;
        params.vuTextSizeFactor = 0.090;
        params.vuTextRadius = 0.85;
        params.singleVuText = true;
        params.faceColorTop = QColor(20, 20, 22);
        params.faceColorBottom = QColor(30, 30, 35);
        params.labelColor = QColor(235, 235, 240, 230);
        params.redZoneColor = QColor(220, 50, 50);
        break;

    case VUMeterStyle::Skin:
        // Skin mode ignores all vector-drawing parameters.
        params.labelSizeFactor = 0.0;
        params.vuTextSizeFactor = 0.0;
        params.vuTextRadius = 0.0;
        params.singleVuText = true;
        params.faceColorTop = Qt::black;
        params.faceColorBottom = Qt::black;
        params.labelColor = Qt::black;
        params.redZoneColor = Qt::red;
        break;
    }

    return params;
}

void StereoVUMeterWidget::setLevels(float leftVuDb, float rightVuDb) {
    left_ = leftVuDb;
    right_ = rightVuDb;
    update();
}

void StereoVUMeterWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF r = rect();

    // --- Common layout calculations (shared by all styles) ---
    const qreal outerPad = std::max<qreal>(14.0, r.width() * 0.02);
    const QRectF inner = r.adjusted(outerPad, outerPad, -outerPad, -outerPad);

    // Default aspect ratio for vector-drawn styles
    qreal aspect = 1.75;

    // Skin mode uses the image's aspect ratio
    if (style_ == VUMeterStyle::Skin) {
        const VUMeterSkin* meter = nullptr;
        if (std::holds_alternative<VUSkinSingleMeters>(skin_.meters)) {
            meter = &std::get<VUSkinSingleMeters>(skin_.meters).vu;
        } else {
            meter = &std::get<VUSkinStereoMeters>(skin_.meters).left;
        }

        aspect = qreal(meter->assets.face.width()) / qreal(meter->assets.face.height());
    }

    const qreal gap =
        (style_ == VUMeterStyle::Skin && std::holds_alternative<VUSkinStereoMeters>(skin_.meters)) ? 0.0
                                                                                                    : std::max<qreal>(16.0, inner.width() * 0.03);

    qreal meterW = (inner.width() - gap) / 2.0;
    qreal meterH = meterW / aspect;

    if (meterH > inner.height()) {
        meterH = inner.height();
        meterW = meterH * aspect;
    }

    const qreal y = inner.center().y() - meterH / 2.0;

    const QRectF leftRect(inner.left(), y, meterW, meterH);
    const QRectF rightRect(leftRect.right() + gap, y, meterW, meterH);

    // --- Mode switch ---
    if (style_ != VUMeterStyle::Skin) {
        // All vector-drawn styles (Original, Sony, Vintage, Modern, Black)

        // Background gradient
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(20, 20, 22));
        bg.setColorAt(1.0, QColor(6, 6, 7));
        p.fillRect(r, bg);

        // Vector-drawn meters
        drawMeter(p, leftRect, left_);
        drawMeter(p, rightRect, right_);
    } else {
        // --- Skin mode ---
        p.fillRect(r, Qt::black);

        const VUMeterSkin* meters[2] = {nullptr, nullptr};
        if (std::holds_alternative<VUSkinSingleMeters>(skin_.meters)) {
            const VUMeterSkin& m = std::get<VUSkinSingleMeters>(skin_.meters).vu;
            meters[0] = &m;
            meters[1] = &m;
        } else {
            const VUSkinStereoMeters& ms = std::get<VUSkinStereoMeters>(skin_.meters);
            meters[0] = &ms.left;
            meters[1] = &ms.right;
        }

        const QRectF rects[2] = {leftRect, rightRect};
        const float vus[2] = {left_, right_};

        for (int i = 0; i < 2; ++i) {
            p.drawPixmap(rects[i], meters[i]->assets.face, meters[i]->assets.face.rect());
            drawMeterImageOnly(p, rects[i], vus[i], *meters[i]);
        }
    }
}

void StereoVUMeterWidget::drawMeterImageOnly(QPainter& p,
                                             const QRectF& rect,
                                             float vuDb,
                                             const VUMeterSkin& skin) {
    p.save();

    // --- Compute pivot in widget coordinates ---
    const qreal scaleX = rect.width() / skin.assets.face.width();
    const qreal scaleY = rect.height() / skin.assets.face.height();

    const QPointF pivot(rect.left() + skin.calibration.pivotX * scaleX, rect.top() + skin.calibration.pivotY * scaleY);

    // --- Compute rotation angle ---
    const float angleDeg = vuToAngleDeg(vuDb, skin.scaleTable);

    // --- Rotate ONLY the needle ---
    {
        p.save();

        // Rotate around pivot
        p.translate(pivot);
        p.rotate(angleDeg);
        p.translate(-pivot);

        // Draw needle exactly as before (same rect)
        p.drawPixmap(rect, skin.assets.needle, skin.assets.needle.rect());

        p.restore(); // <-- restores painter so cap is NOT rotated
    }

    // --- Draw cap overlay (no rotation) ---
    p.drawPixmap(rect, skin.assets.cap, skin.assets.cap.rect());

    p.restore();
}

void StereoVUMeterWidget::drawMeter(QPainter& p, const QRectF& rect, float vuDb) {
    p.save();
    
    // Get style-dependent parameters
    const StyleParams sp = getStyleParams();

    // --- Frame ---
    const qreal frameRadius = std::min(rect.width(), rect.height()) * 0.06;

    QLinearGradient frameGrad(rect.topLeft(), rect.bottomRight());
    frameGrad.setColorAt(0.0, QColor(60, 62, 66));
    frameGrad.setColorAt(0.5, QColor(26, 27, 29));
    frameGrad.setColorAt(1.0, QColor(10, 10, 11));

    p.setPen(QPen(QColor(0, 0, 0, 160), 2.0));
    p.setBrush(frameGrad);
    p.drawRoundedRect(rect, frameRadius, frameRadius);

    // --- Face ---
    const qreal inset = std::max<qreal>(10.0, rect.width() * 0.04);
    const QRectF face = rect.adjusted(inset, inset, -inset, -inset);
    const qreal faceRadius = frameRadius * 0.75;

    QLinearGradient faceGrad(face.topLeft(), face.bottomLeft());
    faceGrad.setColorAt(0.0, sp.faceColorTop);
    faceGrad.setColorAt(1.0, sp.faceColorBottom);

    p.setPen(QPen(QColor(0, 0, 0, 90), 1.5));
    p.setBrush(faceGrad);
    p.drawRoundedRect(face, faceRadius, faceRadius);

    // --- Geometry ---
    const QPointF pivot(face.center().x(), face.bottom() + face.height() * 0.35);
    const qreal radius = std::min(face.width(), face.height()) * 1.00;

    const float theta = vuToAngleDeg(vuDb, singleScaleTable_);

    // --- Draw needle with clipping to face area ---
    // This makes the needle visible only within the face, hiding the pivot area
    {
        p.save();
        
        // Create clipping path for the face (rounded rectangle)
        QPainterPath clipPath;
        clipPath.addRoundedRect(face, faceRadius, faceRadius);
        p.setClipPath(clipPath);
        
        // --- Needle shadow ---
        const QPointF needleTip = polarFromBottomPivot(pivot, radius * 0.98, theta);
        const QPointF shadowTip = needleTip + QPointF(2.0, 2.0);

        // For Black style, use lighter shadow; for others, dark shadow
        QColor shadowColor = (style_ == VUMeterStyle::Black) ? QColor(0, 0, 0, 120) : QColor(0, 0, 0, 80);
        p.setPen(QPen(shadowColor, std::max<qreal>(3.0, rect.width() * 0.008), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(pivot + QPointF(2.0, 2.0), shadowTip);

        // --- Needle ---
        // For Black style, use white needle; for others, black needle
        QColor needleColor = (style_ == VUMeterStyle::Black) ? QColor(235, 235, 240) : QColor(10, 10, 10);
        p.setPen(QPen(needleColor, std::max<qreal>(3.0, rect.width() * 0.008), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(pivot, needleTip);
        
        p.restore();  // Restore clipping
    }

    // --- Bezel (drawn after needle so it appears on top) ---
    const qreal bezelInset = std::max<qreal>(6.0, rect.width() * 0.02);
    const QRectF bezel = rect.adjusted(bezelInset, bezelInset, -bezelInset, -bezelInset);
    p.setPen(QPen(QColor(0, 0, 0, 45), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bezel, frameRadius * 0.85, frameRadius * 0.85);

    // --- Tick radii ---
    const qreal tickR1 = radius * 0.98;
    const qreal tickR2Major = radius * 1.10;
    const qreal tickR2Minor = radius * 1.06;

    // --- Tick values (major + minor) ---
    const QList<float> labels = {-22.0f, -20.0f, -10.0f, -7.0f, -6.0f, -5.0f, -4.0f, -3.0f,
                                 -2.0f,  -1.0f,  -0.5f,  0.0f,  0.5f,  1.0f,  2.0f,  3.0f};

    // --- Arc geometry ---
    const qreal blackWidth = 2.0;
    const qreal redWidth = std::max<qreal>(3.0, rect.width() * 0.018);

    const qreal arcR = radius * 0.98;

    // Adjust red radius so INNER edge aligns with black arc
    const qreal redArcR = arcR - (blackWidth - redWidth) / 2.0;

    // Arc rectangles
    const QRectF blackRect(pivot.x() - arcR, pivot.y() - arcR, arcR * 2.0, arcR * 2.0);
    const QRectF redRect(pivot.x() - redArcR, pivot.y() - redArcR, redArcR * 2.0, redArcR * 2.0);

    // Scale angles
    const float aMin = -48.0f;
    const float a0 = vuToAngleDeg(0.0f, singleScaleTable_); // +18°
    const float a3 = vuToAngleDeg(3.0f, singleScaleTable_); // +47°

    auto arcStart = [](float logicalEndDeg) { return int((90.0f - logicalEndDeg) * 16.0f); };
    auto arcSpan = [](float logicalStartDeg, float logicalEndDeg) {
        return int((logicalEndDeg - logicalStartDeg) * 16.0f);
    };

    // --- Black arc: -48° → +18° (white for Black style) ---
    QColor arcColor = (style_ == VUMeterStyle::Black) ? QColor(235, 235, 240, 200) : QColor(0, 0, 0, 200);
    p.setPen(QPen(arcColor, blackWidth, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(blackRect, arcStart(a0), arcSpan(aMin, a0));

    // --- Red arc: +18° → +47° ---
    p.setPen(QPen(sp.redZoneColor, redWidth, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(redRect, arcStart(a3), arcSpan(a0, a3));
    
    // --- Tick marks ---
    for (float v : labels) {
        bool major = (v == -20.0f || v == -10.0f || v == -7.0f || v == -5.0f || v == -3.0f || v == -2.0f ||
                      v == -1.0f || v == 0.0f || v == 1.0f || v == 2.0f || v == 3.0f);

        const float a = vuToAngleDeg(v, singleScaleTable_);

        const QPointF p1 = polarFromBottomPivot(pivot, tickR1, a);
        const QPointF p2 = polarFromBottomPivot(pivot, major ? tickR2Major : tickR2Minor, a);

        QPen pen(sp.labelColor, major ? 2.2 : 1.4, Qt::SolidLine, Qt::RoundCap);
        if (v > 0.0f) {
            pen.setColor(sp.redZoneColor);
        }
        p.setPen(pen);
        p.drawLine(p1, p2);

        // --- Major tick labels only ---
        if (major) {
            QString t = (v > 0.0f) ? QString("+%1").arg(static_cast<int>(v)) : QString::number(static_cast<int>(v));

            QFont tf = p.font();
            tf.setBold(true);
            tf.setStretch(92);
            tf.setLetterSpacing(QFont::PercentageSpacing, 92);
            tf.setPointSizeF(rect.height() * sp.labelSizeFactor);
            p.setFont(tf);

            // Scale label bounding box with font size
            const qreal labelBoxScale = sp.labelSizeFactor / 0.033;  // Relative to original
            const QPointF pt = polarFromBottomPivot(pivot, radius * 1.17, a);
            const QRectF tr(pt.x() - 18.0 * labelBoxScale, pt.y() - 10.0 * labelBoxScale, 
                           36.0 * labelBoxScale, 20.0 * labelBoxScale);

            p.setPen(v > 0.0f ? sp.redZoneColor : sp.labelColor);
            p.drawText(tr, Qt::AlignCenter, t);
        }
    }

    // --- VU text ---
    auto drawVuTextAt = [&](float angleDeg, float radiusMul, qreal fontSize) {
        // Compute position on arc
        const qreal r = radius * radiusMul;
        const QPointF pos = polarFromBottomPivot(pivot, r, angleDeg);

        p.save();
        p.translate(pos);
        p.rotate(angleDeg);

        QFont vuFont = p.font();
        vuFont.setBold(true);
        vuFont.setStretch(90);
        vuFont.setLetterSpacing(QFont::PercentageSpacing, 95);
        vuFont.setPointSizeF(fontSize);
        p.setFont(vuFont);

        p.setPen(sp.labelColor);

        // Scale text rect with font size
        const qreal vuBoxScale = fontSize / (rect.height() * 0.045);
        const QRectF vuRect(-face.width() * 0.20 * vuBoxScale, -face.height() * 0.06 * vuBoxScale, 
                           face.width() * 0.40 * vuBoxScale, face.height() * 0.12 * vuBoxScale);

        p.drawText(vuRect, Qt::AlignCenter, "VU");
        p.restore();
    };

    const qreal vuFontSize = rect.height() * sp.vuTextSizeFactor;
    
    if (sp.singleVuText) {
        // Single centered VU text (Sony/Modern style)
        drawVuTextAt(0.0f, sp.vuTextRadius, vuFontSize);
    } else {
        // Two angled VU texts (Original/Vintage style)
        drawVuTextAt(-33.0f, sp.vuTextRadius, vuFontSize);
        drawVuTextAt(33.0f, sp.vuTextRadius, vuFontSize);
    }

    // --- SONY logo for Sony style ---
    if (style_ == VUMeterStyle::Sony && !sonyFontFamily_.isEmpty()) {
        p.save();
        
        QFont sonyFont(sonyFontFamily_);
        sonyFont.setPointSizeF(rect.height() * 0.075);  // Adjust size as needed
        sonyFont.setBold(false);
        p.setFont(sonyFont);
        p.setPen(sp.labelColor);
        
        // Position in top-left corner of face with some padding
        const qreal padding = face.width() * 0.04;
        const qreal sonyX = face.left() + padding;
        const qreal sonyY = face.top() + padding;
        
        // Apply vertical compression (0.80 = 20% shorter height)
        p.translate(sonyX, sonyY);
        p.scale(1.0, 0.80);
        
        // Draw at origin since we've already translated
        const QRectF sonyRect(0, 0, face.width() * 0.25, face.height() * 0.15 / 0.80);
        p.drawText(sonyRect, Qt::AlignLeft | Qt::AlignTop, "SONY");
        
        p.restore();
    }

    // --- Pivot cap (no longer needed since needle is clipped, but keep the line) ---
    const qreal capR = std::max<qreal>(10.0, rect.width() * 0.04);

    p.restore();
}

void StereoVUMeterWidget::loadDefaultSkin() {
    VUMeterSkin s;

    s.assets.face.load(":/images/model_702w/0.png");
    s.assets.needle.load(":/images/model_702w/1.png");
    s.assets.cap.load(":/images/model_702w/2.png");

    // Keep the built-in skin calibration consistent with the built-in scale table.
    // (The skin's scale table remains authoritative for needle angle mapping.)
    s.calibration.minAngle = -47;
    s.calibration.minLevel = -20;
    s.calibration.zeroAngle = 18;
    s.calibration.zeroLevel = 0;
    s.calibration.maxAngle = 47;
    s.calibration.maxLevel = 3;

    s.calibration.pivotX = 310;
    s.calibration.pivotY = 362;

    s.calibration.mobilityNegative = 0.05;
    s.calibration.mobilityPositive = 0.10;

    s.scaleTable = builtInDefaultScaleTable();

    skin_.name = QStringLiteral("Default");
    skin_.importedFrom.clear();
    skin_.meters = VUSkinSingleMeters{s};

    singleScaleTable_ = builtInDefaultScaleTable();
}
