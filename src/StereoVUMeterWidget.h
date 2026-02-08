#pragma once

#include <QFont>
#include <QWidget>

#include "VUMeterScale.h"
#include "VUMeterSkin.h"

class QPaintEvent;
class QPainter;
class QRectF;
class QString;

// VU Meter visual styles
enum class VUMeterStyle {
    Original, // Current/default style
    Vintage,  // Warm vintage colors
    Modern,   // Clean modern look
    Black,    // Modern style with inverted black/white colors
    Skin      // Image based graphics
};

class StereoVUMeterWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit StereoVUMeterWidget(QWidget* parent = nullptr);
    void setLevels(float leftVuDb, float rightVuDb);
    void setStyle(VUMeterStyle style);
    VUMeterStyle style() const { return style_; }

    void clearSkin();

    void setSkinPackage(const VUSkinPackage& skin);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    float left_ = -20.0f;
    float right_ = -20.0f;
    VUMeterStyle style_ = VUMeterStyle::Skin;

    void drawMeter(QPainter& p, const QRectF& rect, float vuDb);
    void drawMeterImageOnly(QPainter& p, const QRectF& rect, float vuDb, const VUMeterSkin& skin);
    VUMeterScaleTable singleScaleTable_;

    // Style-dependent parameters
    struct StyleParams {
        qreal labelSizeFactor;  // Font size factor for tick labels
        qreal vuTextSizeFactor; // Font size factor for "VU" text
        qreal vuTextRadius;     // Radius multiplier for VU text position
        bool singleVuText;      // Draw single centered VU vs two angled
        QColor faceColorTop;    // Face gradient top color
        QColor faceColorBottom; // Face gradient bottom color
        QColor labelColor;      // Color for labels (non-red zone)
        QColor redZoneColor;    // Color for red zone elements
    };

    StyleParams getStyleParams() const;

    VUSkinPackage skin_;
    void loadDefaultSkin();
};
