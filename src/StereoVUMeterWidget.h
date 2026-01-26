#pragma once

#include <QWidget>

class QPaintEvent;
class QPainter;
class QRectF;
class QString;

class StereoVUMeterWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit StereoVUMeterWidget(QWidget* parent = nullptr);

    void setLevels(float leftVuDb, float rightVuDb);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    float left_ = -20.0f;
    float right_ = -20.0f;

    void drawMeter(QPainter& p, const QRectF& rect, float vuDb);
};
